#define _GNU_SOURCE
#include "thread_cpu_quota.h"

#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define CPU_PERIOD 100000U
#define POLL_NS 250000000L

struct sd_bus;
struct sd_bus_message;
struct sd_bus_error { const char *name; const char *message; int need_free; };
struct sd_api {
    void *library;
    int (*open_user)(struct sd_bus **);
    int (*bus_new)(struct sd_bus **);
    int (*bus_set_address)(struct sd_bus *, const char *);
    int (*bus_start)(struct sd_bus *);
    struct sd_bus *(*bus_unref)(struct sd_bus *);
    int (*message_new_method_call)(struct sd_bus *, struct sd_bus_message **, const char *, const char *, const char *, const char *);
    int (*message_append)(struct sd_bus_message *, const char *, ...);
    int (*message_open_container)(struct sd_bus_message *, char, const char *);
    int (*message_close_container)(struct sd_bus_message *);
    struct sd_bus_message *(*message_unref)(struct sd_bus_message *);
    int (*bus_call)(struct sd_bus *, struct sd_bus_message *, uint64_t, struct sd_bus_error *, struct sd_bus_message **);
    void (*error_free)(struct sd_bus_error *);
};

static bool symbol(void *lib, const char *name, void *out, size_t size)
{ void *p = dlsym(lib, name); if (!p || size != sizeof(p)) return false; memcpy(out, &p, size); return true; }
static void api_close(struct sd_api *api) { if (api->library) (void)dlclose(api->library); memset(api, 0, sizeof(*api)); }
static bool api_open(struct sd_api *api)
{
    void *lib = dlopen("libsystemd.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return false;
    memset(api, 0, sizeof(*api)); api->library = lib;
#define LOAD(member, name) if (!symbol(lib, name, &api->member, sizeof(api->member))) goto fail
    LOAD(open_user, "sd_bus_open_user");
    LOAD(bus_new, "sd_bus_new");
    LOAD(bus_set_address, "sd_bus_set_address");
    LOAD(bus_start, "sd_bus_start");
    LOAD(bus_unref, "sd_bus_unref");
    LOAD(message_new_method_call, "sd_bus_message_new_method_call");
    LOAD(message_append, "sd_bus_message_append");
    LOAD(message_open_container, "sd_bus_message_open_container");
    LOAD(message_close_container, "sd_bus_message_close_container");
    LOAD(message_unref, "sd_bus_message_unref");
    LOAD(bus_call, "sd_bus_call");
    LOAD(error_free, "sd_bus_error_free");
#undef LOAD
    return true;
fail: api_close(api); return false;
}

static bool open_user_bus(const struct sd_api *api, struct sd_bus **bus)
{
    char address[80];

    if (api->open_user(bus) >= 0) return true;
    if (*bus) { api->bus_unref(*bus); *bus = 0; }
    if (snprintf(address, sizeof(address), "unix:path=/run/user/%ju/bus",
                 (uintmax_t)getuid()) < 0 ||
        api->bus_new(bus) < 0 || !*bus ||
        api->bus_set_address(*bus, address) < 0 || api->bus_start(*bus) < 0) {
        if (*bus) { api->bus_unref(*bus); *bus = 0; }
        return false;
    }
    return true;
}

static bool write_text(const char *path, const char *text)
{ FILE *f = fopen(path, "we"); if (!f) return false; return fputs(text, f) >= 0 && fclose(f) == 0; }
static void record_failure(struct frame_pacer_thread_cpu_quota *q, const char *stage, int error)
{
    char message[192];
    void (*log)(const char *);

    if (!q || !stage) return;
    if (q->last_error == error && !strcmp(q->failure_stage, stage)) return;
    q->last_error = error;
    (void)snprintf(q->failure_stage, sizeof(q->failure_stage), "%s", stage);
    (void)snprintf(message, sizeof(message), "frame-pacer: thread CPU ceiling %s: %s\n",
                   stage, error ? strerror(error) : "unavailable");
    (void)pthread_mutex_lock(&q->mutex);
    log = q->log;
    (void)pthread_mutex_unlock(&q->mutex);
    if (log) log(message);
}
static void record_state(struct frame_pacer_thread_cpu_quota *q, const char *stage)
{
    char message[128];
    void (*log)(const char *);

    if (!q || !stage) return;
    if (!strcmp(q->failure_stage, stage) && q->last_error == INT_MIN) return;
    q->last_error = INT_MIN;
    (void)snprintf(q->failure_stage, sizeof(q->failure_stage), "%s", stage);
    (void)snprintf(message, sizeof(message), "frame-pacer: thread CPU ceiling %s\n", stage);
    (void)pthread_mutex_lock(&q->mutex);
    log = q->log;
    (void)pthread_mutex_unlock(&q->mutex);
    if (log) log(message);
}
static bool join_path(char *out, size_t n, const char *base, const char *child, const char *file)
{ int r = snprintf(out, n, "%s%s%s%s%s", base, child ? "/" : "", child ? child : "", file ? "/" : "", file ? file : ""); return r >= 0 && (size_t)r < n; }
static bool write_tid(const char *path, uint32_t tid)
{ char b[32]; int n = snprintf(b, sizeof(b), "%u", tid); return n > 0 && (size_t)n < sizeof(b) && write_text(path, b); }

static bool host_pid_visible(void)
{
    char line[256]; FILE *f = fopen("/proc/self/status", "re"); bool ok = false;
    if (!f) return false;
    while (fgets(line, sizeof(line), f)) if (!strncmp(line, "NSpid:", 6)) {
        char *p = line + 6; while (*p == ' ' || *p == '\t') ++p;
        ok = *p && !strchr(p, ' ') && !strchr(p, '\t'); break;
    }
    (void)fclose(f); return ok;
}
static bool identity(char *scope, size_t n)
{
    char boot[64] = {0}; FILE *f = fopen("/proc/sys/kernel/random/boot_id", "re"); int r;
    if (!f || !fgets(boot, sizeof(boot), f)) { if (f) fclose(f); return false; }
    (void)fclose(f); boot[strcspn(boot, "\r\n")] = 0;
    r = snprintf(scope, n, "frame-pacer-thread-cpu-u%ju-b%.12s-p%ju.scope", (uintmax_t)getuid(), boot, (uintmax_t)getpid());
    return r > 0 && (size_t)r < n;
}
static bool property_prefix(const struct sd_api *a, struct sd_bus_message *m, const char *name, const char *sig)
{ return a->message_open_container(m, 'r', "sv") >= 0 && a->message_append(m, "s", name) >= 0 && a->message_open_container(m, 'v', sig) >= 0; }
static bool property_bool(const struct sd_api *a, struct sd_bus_message *m, const char *name, int v)
{ return property_prefix(a,m,name,"b") && a->message_append(m,"b",v)>=0 && a->message_close_container(m)>=0 && a->message_close_container(m)>=0; }
static bool property_string(const struct sd_api *a, struct sd_bus_message *m, const char *name, const char *v)
{ return property_prefix(a,m,name,"s") && a->message_append(m,"s",v)>=0 && a->message_close_container(m)>=0 && a->message_close_container(m)>=0; }
static bool property_pid(const struct sd_api *a, struct sd_bus_message *m)
{ uint32_t pid=(uint32_t)getpid(); return property_prefix(a,m,"PIDs","au") && a->message_open_container(m,'a',"u")>=0 && a->message_append(m,"u",pid)>=0 && a->message_close_container(m)>=0 && a->message_close_container(m)>=0 && a->message_close_container(m)>=0; }
static bool start_scope(struct sd_api *a, struct sd_bus *bus, const char *name)
{
    struct sd_bus_message *q=0,*reply=0; struct sd_bus_error e={0}; bool ok=false;
    if (a->message_new_method_call(bus,&q,"org.freedesktop.systemd1","/org/freedesktop/systemd1","org.freedesktop.systemd1.Manager","StartTransientUnit")>=0 &&
        a->message_append(q,"ss",name,"fail")>=0 && a->message_open_container(q,'a',"(sv)")>=0 && property_pid(a,q) && property_bool(a,q,"Delegate",1) && property_bool(a,q,"CPUAccounting",1) && property_string(a,q,"CollectMode","inactive-or-failed") && a->message_close_container(q)>=0 && a->message_open_container(q,'a',"(sa(sv))")>=0 && a->message_close_container(q)>=0 && a->bus_call(bus,q,500000,&e,&reply)>=0) ok=true;
    a->error_free(&e); if(reply) a->message_unref(reply); if(q) a->message_unref(q); return ok;
}
static bool property_exec(const struct sd_api *a, struct sd_bus_message *m,
                          const char *path, const char *const argv[])
{
    unsigned int i;
    if (!property_prefix(a,m,"ExecStart","a(sasb)") ||
        a->message_open_container(m,'a',"(sasb)") < 0 ||
        a->message_open_container(m,'r',"sasb") < 0 ||
        a->message_append(m,"s",path) < 0 ||
        a->message_open_container(m,'a',"s") < 0) return false;
    for (i=0; argv[i]; ++i) if (a->message_append(m,"s",argv[i]) < 0) return false;
    return a->message_close_container(m)>=0 && a->message_append(m,"b",0)>=0 &&
           a->message_close_container(m)>=0 && a->message_close_container(m)>=0 &&
           a->message_close_container(m)>=0 && a->message_close_container(m)>=0;
}
static const char helper_anchor;
static bool helper_path(char *out, size_t n)
{
    Dl_info info; const char *build; size_t prefix;
    if (!dladdr(&helper_anchor,&info) || !info.dli_fname ||
        !(build=strstr(info.dli_fname,"/build/"))) return false;
    prefix=(size_t)(build-info.dli_fname)+strlen("/build");
    return prefix + strlen("/frame-pacer-thread-cpu-controller") < n &&
           snprintf(out,n,"%.*s/frame-pacer-thread-cpu-controller",(int)prefix,info.dli_fname)>0 &&
           !access(out,X_OK);
}
static bool write_external_state(const char *path, bool enabled, uint32_t quota)
{
    char text[32]; int fd,n;
    n=snprintf(text,sizeof(text),enabled?"on %u\n":"off\n",quota);
    if(n<0 || (size_t)n>=sizeof(text) ||
       (fd=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600))<0) return false;
    if (write(fd,text,(size_t)n)!=(ssize_t)n || fchmod(fd,0600) || fsync(fd)) { (void)close(fd); return false; }
    (void)close(fd); return true;
}
static bool external_confirmed(const char *state, uint32_t quota)
{
    char status[1240],line[64]; FILE *f; unsigned int got;
    if(snprintf(status,sizeof(status),"%s.status",state)<0 || !(f=fopen(status,"re")))return false;
    if(!fgets(line,sizeof(line),f)){fclose(f);return false;} fclose(f);
    return sscanf(line,"confirmed %u",&got)==1 && got==quota;
}
static bool start_external(struct frame_pacer_thread_cpu_quota *q,
                           struct sd_api *a, struct sd_bus *bus, const char *scope_name)
{
    struct sd_bus_message *m=0,*reply=0; struct sd_bus_error e={0}; char helper[1200], unit[180], directory[1100], pid[32], scope[1200];
    const char *argv[7]; const char *home=getenv("HOME"); bool ok=false;
    if(!home || !*home || !helper_path(helper,sizeof(helper)) ||
       snprintf(directory,sizeof(directory),"%s/.local/state/frame-pacer",home)>=(int)sizeof(directory) ||
       (mkdir(directory,0700) && errno!=EEXIST) ||
       snprintf(q->external_state,sizeof(q->external_state),"%s/thread-cpu-%s",directory,scope_name)>=(int)sizeof(q->external_state) ||
       !write_external_state(q->external_state,true,q->requested) ||
       snprintf(unit,sizeof(unit),"frame-pacer-thread-cpu-controller-u%ju-p%ju.service",(uintmax_t)getuid(),(uintmax_t)getpid())>=(int)sizeof(unit) ||
       snprintf(pid,sizeof(pid),"%ju",(uintmax_t)getpid())>=(int)sizeof(pid)) return false;
    if (strlen(q->cgroup_proc) <= strlen("/frame-pacer-thread-cpu") ||
        snprintf(scope,sizeof(scope),"%.*s",(int)(strlen(q->cgroup_proc)-strlen("/frame-pacer-thread-cpu")),q->cgroup_proc)<0)
        return false;
    argv[0]=helper; argv[1]="--pid"; argv[2]=pid; argv[3]="--scope"; argv[4]=scope; argv[5]=q->external_state; argv[6]=0;
    if(a->message_new_method_call(bus,&m,"org.freedesktop.systemd1","/org/freedesktop/systemd1","org.freedesktop.systemd1.Manager","StartTransientUnit")>=0 &&
       a->message_append(m,"ss",unit,"fail")>=0 && a->message_open_container(m,'a',"(sv)")>=0 &&
       property_exec(a,m,helper,argv) && property_string(a,m,"Type","exec") &&
       property_string(a,m,"CollectMode","inactive-or-failed") && a->message_close_container(m)>=0 &&
       a->message_open_container(m,'a',"(sa(sv))")>=0 && a->message_close_container(m)>=0 &&
       a->bus_call(bus,m,500000,&e,&reply)>=0) ok=true;
    a->error_free(&e); if(reply)a->message_unref(reply); if(m)a->message_unref(m);
    /* A matching service can already exist after an `off` -> quota live
     * transition.  The state path is per boot, UID, and host PID; treating
     * it as a candidate is safe because HUD confirmation still requires its
     * exact `confirmed <quota>` response. */
    if (ok || !access(q->external_state, F_OK)) {
        q->external=true;
        return true;
    }
    return false;
}
static bool mount_is_rw(const char *path)
{
    char line[2048]; FILE *f = fopen(path, "re"); bool ok = false;
    if (!f) return false;
    while (fgets(line, sizeof(line), f)) {
        char *mount = strstr(line, " /sys/fs/cgroup ");
        char *dash = strstr(line, " - ");
        if (mount && dash && mount < dash && !strncmp(mount + 16, "rw,", 3)) {
            ok = true;
            break;
        }
    }
    (void)fclose(f);
    return ok;
}
static bool cgroup_root(char *out, size_t n)
{
    pid_t pid = getppid(); unsigned int i;

    if (mount_is_rw("/proc/self/mountinfo"))
        return snprintf(out, n, "/sys/fs/cgroup") > 0;
    /* pressure-vessel makes cgroupfs read-only for the game, but its direct
     * launcher parent remains in the caller's writable user mount namespace. */
    for (i = 0; i < 24 && pid > 1; ++i) {
        char status[64], mountinfo[64], line[256]; FILE *f; pid_t parent = 0;
        if (snprintf(status, sizeof(status), "/proc/%ld/status", (long)pid) < 0 ||
            !(f = fopen(status, "re"))) break;
        while (fgets(line, sizeof(line), f)) {
            unsigned long uid, value;
            if (sscanf(line, "Uid:\t%lu", &uid) == 1 && uid != (unsigned long)getuid()) {
                (void)fclose(f); return false;
            }
            if (sscanf(line, "PPid:\t%lu", &value) == 1) parent = (pid_t)value;
        }
        (void)fclose(f);
        if (snprintf(mountinfo, sizeof(mountinfo), "/proc/%ld/mountinfo", (long)pid) < 0)
            return false;
        if (mount_is_rw(mountinfo) &&
            snprintf(out, n, "/proc/%ld/root/sys/fs/cgroup", (long)pid) > 0 &&
            access(out, R_OK | X_OK) == 0)
            return true;
        pid = parent;
    }
    return false;
}
static bool scope_path(struct frame_pacer_thread_cpu_quota *q, const char *root,
                       const char *scope)
{
    unsigned int i; char line[2048], marker[256];
    for (i=0;i<50;++i) { FILE *f=fopen("/proc/self/cgroup","re");
        if (f && fgets(line,sizeof(line),f) && !strncmp(line,"0::/",4)) { char *p=line+3; p[strcspn(p,"\r\n")]=0;
            if (!strcmp(strrchr(p,'/') ? strrchr(p,'/')+1 : p, scope) &&
                snprintf(q->scope,sizeof(q->scope),"%s%s",root,p)<(int)sizeof(q->scope) &&
                snprintf(q->cgroup_proc,sizeof(q->cgroup_proc),"%s/frame-pacer-thread-cpu",p)<(int)sizeof(q->cgroup_proc)) {
                fclose(f); return true;
            }}
        /* A second backend can initialize after the controller moved this
         * process into its own threaded child.  Accept only the exact scope
         * identity and a numeric owned child; this joins that controller and
         * never claims a sibling hierarchy. */
        if (f && snprintf(marker,sizeof(marker),"/%s/frame-pacer-thread-cpu/t-",scope)>0) {
            char *owned=strstr(line+3,marker), *tid;
            if (owned) tid=owned+strlen(marker);
            if (owned && tid[strspn(tid,"0123456789")]==0) {
                size_t n=(size_t)(owned-(line+3))+1+strlen(scope);
                char saved=(line+3)[n];
                (line+3)[n]=0;
                if (snprintf(q->scope,sizeof(q->scope),"%s%s",root,line+3)<(int)sizeof(q->scope) &&
                    snprintf(q->cgroup_proc,sizeof(q->cgroup_proc),"%s/frame-pacer-thread-cpu",line+3)<(int)sizeof(q->cgroup_proc)) {
                    (line+3)[n]=saved; fclose(f); return true;
                }
                (line+3)[n]=saved;
            }
        }
        if (f) fclose(f);
        { struct timespec t={.tv_nsec=10000000}; (void)nanosleep(&t,0); }
    }
    return false;
}
static uint32_t collect(uint32_t out[FRAME_PACER_THREAD_CPU_QUOTA_TIDS_MAX], bool *overflow)
{
    DIR *d=opendir("/proc/self/task"); struct dirent *e; uint32_t n=0; *overflow=false; if(!d) return 0;
    while ((e=readdir(d))) { char *end; unsigned long v; if(!isdigit((unsigned char)e->d_name[0])) continue; v=strtoul(e->d_name,&end,10); if(*end || !v || v>UINT32_MAX) continue; if(n==FRAME_PACER_THREAD_CPU_QUOTA_TIDS_MAX) *overflow=true; else out[n++]=(uint32_t)v; }
    (void)closedir(d); return n;
}
static bool tid_in(const uint32_t *t, uint32_t n, uint32_t tid) { uint32_t i; for(i=0;i<n;++i) if(t[i]==tid) return true; return false; }
static bool tid_path_is(uint32_t tid, const char *expected)
{ char p[128],line[2048]; FILE *f; int n=snprintf(p,sizeof(p),"/proc/self/task/%u/cgroup",tid); if(n<0||(size_t)n>=sizeof(p)||(f=fopen(p,"re"))==0)return false; if(!fgets(line,sizeof(line),f)){fclose(f);return false;} fclose(f); line[strcspn(line,"\r\n")]=0; return !strncmp(line,"0::",3)&&!strcmp(line+3,expected); }
static bool tid_exists(uint32_t tid)
{ char path[64]; int n = snprintf(path, sizeof(path), "/proc/self/task/%u", tid); return n > 0 && (size_t)n < sizeof(path) && !access(path, F_OK); }
static bool file_is(const char *path, const char *wanted)
{ char b[64]; FILE *f=fopen(path,"re"); bool ok=false; if(f&&fgets(b,sizeof(b),f)){b[strcspn(b,"\r\n")]=0;ok=!strcmp(b,wanted);} if(f)fclose(f);return ok; }

static void cleanup(struct frame_pacer_thread_cpu_quota *q)
{
    uint32_t now[FRAME_PACER_THREAD_CPU_QUOTA_TIDS_MAX], n,i; bool over; char p[1400], expected[1400], child[64];
    if (q->external) {
        (void)write_external_state(q->external_state,false,0);
        q->external=false; q->external_state[0]=0; q->cgroup_proc[0]=0;
        return;
    }
    if (!q->owner || !q->cgroup[0]) return;
    record_state(q, "remove owned threaded topology");
    n=collect(now,&over); (void)over;
    for(i=0;i<n;++i) {
        char scope_threads[1400];

        if (!tid_in(q->tids,q->observed_threads,now[i])) continue;
        if (snprintf(child,sizeof(child),"t-%u",now[i]) > 0 &&
            join_path(p,sizeof(p),q->cgroup,child,0) &&
            join_path(expected,sizeof(expected),q->cgroup_proc,child,0) &&
            tid_path_is(now[i], expected) &&
            join_path(scope_threads,sizeof(scope_threads),q->scope,0,"cgroup.threads"))
            (void)write_tid(scope_threads,now[i]);
    }
    for(i=0;i<q->observed_threads;++i) { if(snprintf(child,sizeof(child),"t-%u",q->tids[i])>0 && join_path(p,sizeof(p),q->cgroup,child,0)) (void)rmdir(p); }
    (void)rmdir(q->cgroup);
    if (join_path(p,sizeof(p),q->scope,0,"cgroup.subtree_control")) (void)write_text(p,"-cpu");
    q->owner=false; q->cgroup[0]=q->cgroup_proc[0]=0; q->observed_threads=0;
}
static bool activate(struct frame_pacer_thread_cpu_quota *q)
{
    struct sd_api api; struct sd_bus *bus=0; char p[1400], root[80], name[160];
    if(!host_pid_visible() || !identity(name,sizeof(name)) || !api_open(&api) ||
       !open_user_bus(&api, &bus)) {
        record_failure(q, "open delegated user scope", errno);
        if (bus) api.bus_unref(bus);
        api_close(&api);
        return false;
    }
    (void)start_scope(&api,bus,name); /* An existing same-identity scope is acceptable. */
    if (!scope_path(q,"/sys/fs/cgroup",name)) {
        record_failure(q, "verify delegated scope", errno);
        if (bus) api.bus_unref(bus);
        api_close(&api);
        return false;
    }
    if(!cgroup_root(root,sizeof(root))) {
        bool external=start_external(q,&api,bus,name);
        if (bus) api.bus_unref(bus);
        api_close(&api);
        if (!external) record_failure(q,"start external controller",errno);
        return external;
    }
    /* Reconstruct the verified scope through the writable cgroup filesystem. */
    if (!scope_path(q,root,name)) {
        if (bus) api.bus_unref(bus);
        api_close(&api);
        record_failure(q,"verify writable delegated scope",errno);
        return false;
    }
    if (bus) api.bus_unref(bus);
    api_close(&api);
    /* scope_path overwrote the name; its terminal component is still the exact generated scope. */
    if(!join_path(q->cgroup,sizeof(q->cgroup),q->scope,"frame-pacer-thread-cpu",0)) {
        record_failure(q, "construct threaded root", ENAMETOOLONG);
        return false;
    }
    if(mkdir(q->cgroup,0700)) {
        record_failure(q, errno == EEXIST ? "claim threaded root" : "create threaded root", errno);
        return false;
    }
    q->owner=true;
    record_state(q, "created threaded root");
    if(!join_path(p,sizeof(p),q->cgroup,0,"cgroup.type") || !write_text(p,"threaded") ||
       !join_path(p,sizeof(p),q->scope,0,"cgroup.subtree_control") || !write_text(p,"+cpu") ||
       !join_path(p,sizeof(p),q->cgroup,0,"cgroup.subtree_control") || !write_text(p,"+cpu")) {
        record_failure(q, "initialize threaded CPU topology", errno);
        cleanup(q); return false;
    }
    return true;
}
enum reconcile_result { RECONCILE_FATAL, RECONCILE_INCOMPLETE, RECONCILE_CONFIRMED };

static enum reconcile_result reconcile(struct frame_pacer_thread_cpu_quota *q, uint32_t quota)
{
    uint32_t tids[FRAME_PACER_THREAD_CPU_QUOTA_TIDS_MAX],n,i,managed,verified; bool over, incomplete = false; char child[64],p[1400],want[32]; int r;
    n=collect(tids,&over); if(over || !n) return RECONCILE_FATAL;
    r=snprintf(want,sizeof(want),"%u %u",quota*1000U,CPU_PERIOD); if(r<0||(size_t)r>=sizeof(want))return RECONCILE_FATAL;
    managed = 0;
    for(i=0;i<n;++i) {
        bool created = false;
        if(snprintf(child,sizeof(child),"t-%u",tids[i])<0 || !join_path(p,sizeof(p),q->cgroup,child,0))return RECONCILE_FATAL;
        if (!mkdir(p,0700)) created = true;
        else if (errno != EEXIST) {
            if (!tid_exists(tids[i])) continue;
            incomplete = true;
            continue;
        }
        (void)created;
        if (!join_path(p,sizeof(p),q->cgroup,child,"cgroup.type") ||
            (!file_is(p,"threaded") && !write_text(p,"threaded"))) {
            if (!tid_exists(tids[i])) continue;
            incomplete = true;
            continue;
        }
        if (!join_path(p,sizeof(p),q->cgroup,child,"cgroup.threads")) return RECONCILE_FATAL;
        if (!write_tid(p,tids[i])) {
            /* `/proc/self/task` can race an exiting renderer thread. */
            if (!tid_exists(tids[i])) continue;
            incomplete = true;
            continue;
        }
        if (!join_path(p,sizeof(p),q->cgroup,child,"cpu.max") || !write_text(p,want)) {
            if (!tid_exists(tids[i])) continue;
            incomplete = true;
            continue;
        }
        tids[managed++] = tids[i];
    }
    if (!managed) return RECONCILE_INCOMPLETE;
    /* A vanished TID leaves an empty, known child; remove only that child. */
    for(i=0;i<q->observed_threads;++i) if(!tid_in(tids,n,q->tids[i]) &&
        snprintf(child,sizeof(child),"t-%u",q->tids[i])>0 &&
        join_path(p,sizeof(p),q->cgroup,child,0)) (void)rmdir(p);
    verified = 0;
    for(i=0;i<managed;++i) {
        char expected[1400];
        if (snprintf(child,sizeof(child),"t-%u",tids[i])<0 ||
            !join_path(expected,sizeof(expected),q->cgroup,child,0) ||
            !join_path(expected,sizeof(expected),q->cgroup_proc,child,0) ||
            !tid_path_is(tids[i],expected) ||
            !join_path(p,sizeof(p),q->cgroup,child,"cpu.max") || !file_is(p,want)) {
            if (!tid_exists(tids[i])) continue;
            incomplete = true;
            continue;
        }
        tids[verified++] = tids[i];
    }
    if (!verified) return RECONCILE_INCOMPLETE;
    q->observed_threads=verified; memcpy(q->tids,tids,verified*sizeof(tids[0]));
    return incomplete || verified != n ? RECONCILE_INCOMPLETE : RECONCILE_CONFIRMED;
}
static void *worker(void *arg)
{
    struct frame_pacer_thread_cpu_quota *q=arg; bool active=false; uint32_t last=0;
    for (;;) { bool enabled; uint32_t want;
        (void)pthread_mutex_lock(&q->mutex); while(!q->stop && !q->requested_enabled && !active) (void)pthread_cond_wait(&q->changed,&q->mutex); if(q->stop){(void)pthread_mutex_unlock(&q->mutex);break;} enabled=q->requested_enabled; want=q->requested; (void)pthread_mutex_unlock(&q->mutex);
        if(!enabled) { if(active || q->external) { record_state(q, "explicit policy cleanup"); cleanup(q); } active=false; continue; }
        if(!active) active=activate(q);
        if (active && q->external) {
            bool ok;
            if (last != want && !write_external_state(q->external_state,true,want)) {
                (void)pthread_mutex_lock(&q->mutex);
                q->confirmed=false;
                (void)pthread_mutex_unlock(&q->mutex);
            } else if (last != want) {
                last=want;
            }
            ok=external_confirmed(q->external_state,want);
            (void)pthread_mutex_lock(&q->mutex);
            q->confirmed=ok && q->requested_enabled && q->requested==want;
            (void)pthread_mutex_unlock(&q->mutex);
            if (!ok) active=false;
        } else if (active) {
            enum reconcile_result result = reconcile(q, want);

            if (result == RECONCILE_CONFIRMED) {
                last=want;
                (void)pthread_mutex_lock(&q->mutex);
                q->confirmed=q->requested_enabled&&q->requested==want;
                (void)pthread_mutex_unlock(&q->mutex);
            } else {
                (void)pthread_mutex_lock(&q->mutex);
                q->confirmed=false;
                (void)pthread_mutex_unlock(&q->mutex);
                if (result == RECONCILE_FATAL) {
                    record_state(q, "fatal reconciliation cleanup");
                    cleanup(q); active=false;
                }
            }
        } else {
            (void)pthread_mutex_lock(&q->mutex); q->confirmed=false;
            (void)pthread_mutex_unlock(&q->mutex);
        }
        (void)last; { struct timespec t={.tv_nsec=POLL_NS}; (void)nanosleep(&t,0); }
    }
    if (active) cleanup(q);
    return 0;
}
void frame_pacer_thread_cpu_quota_init(struct frame_pacer_thread_cpu_quota *q)
{ if(!q)return; memset(q,0,sizeof(*q)); if(pthread_mutex_init(&q->mutex,0)||pthread_cond_init(&q->changed,0)){memset(q,0,sizeof(*q));return;} q->initialized=true; if(!pthread_create(&q->worker,0,worker,q))q->worker_started=true; }
void frame_pacer_thread_cpu_quota_destroy(struct frame_pacer_thread_cpu_quota *q)
{ if(!q||!q->initialized)return; (void)pthread_mutex_lock(&q->mutex);q->stop=true;q->confirmed=false;(void)pthread_cond_signal(&q->changed);(void)pthread_mutex_unlock(&q->mutex);if(q->worker_started)(void)pthread_join(q->worker,0);(void)pthread_cond_destroy(&q->changed);(void)pthread_mutex_destroy(&q->mutex);memset(q,0,sizeof(*q)); }
void frame_pacer_thread_cpu_quota_publish(struct frame_pacer_thread_cpu_quota *q,bool enabled,uint32_t percent)
{ if(!q||!q->initialized)return; if(!enabled||percent<1||percent>100){enabled=false;percent=0;} (void)pthread_mutex_lock(&q->mutex); if(q->requested_enabled!=enabled||q->requested!=percent)q->confirmed=false; q->requested_enabled=enabled;q->requested=percent;(void)pthread_cond_signal(&q->changed);(void)pthread_mutex_unlock(&q->mutex); }
void frame_pacer_thread_cpu_quota_set_logger(struct frame_pacer_thread_cpu_quota *q,
                                             void (*log)(const char *))
{ if(!q||!q->initialized)return; (void)pthread_mutex_lock(&q->mutex); q->log=log; (void)pthread_mutex_unlock(&q->mutex); }
bool frame_pacer_thread_cpu_quota_confirmed(struct frame_pacer_thread_cpu_quota *q,uint32_t *percent)
{ bool ok=false;if(percent)*percent=0;if(!q||!q->initialized)return false;(void)pthread_mutex_lock(&q->mutex);ok=q->requested_enabled&&q->confirmed;if(ok&&percent)*percent=q->requested;(void)pthread_mutex_unlock(&q->mutex);return ok; }
