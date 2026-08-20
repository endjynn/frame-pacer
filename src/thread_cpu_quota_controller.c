/* Runs outside a Steam Runtime mount namespace.  It is started only as a
 * transient --user service and accepts one already-created, delegated scope. */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#define MAX_TIDS 1024U
#define PERIOD 100000U

static bool join(char *out, size_t size, const char *a, const char *b)
{ int n = snprintf(out, size, "%s/%s", a, b); return n >= 0 && (size_t)n < size; }
static bool write_file(const char *path, const char *text)
{ int fd = open(path, O_WRONLY | O_TRUNC | O_CLOEXEC | O_NOFOLLOW); size_t n = strlen(text); bool ok;
  if (fd < 0) return false;
  ok = write(fd, text, n) == (ssize_t)n && fsync(fd) == 0;
  (void)close(fd);
  return ok; }
static bool read_file(const char *path, char *out, size_t size)
{ int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW); ssize_t n; struct stat st;
  if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != getuid() || st.st_nlink != 1) { if(fd>=0)(void)close(fd); return false; }
  n = read(fd, out, size - 1); (void)close(fd); if (n < 0) return false; out[n] = 0; return true; }
static bool tid_exists(pid_t pid, uint32_t tid)
{ char p[80]; return snprintf(p,sizeof(p),"/proc/%ld/task/%u",(long)pid,tid)>0 && !access(p,F_OK); }
static unsigned collect(pid_t pid, uint32_t out[MAX_TIDS], bool *overflow)
{ char path[64]; DIR *d; struct dirent *e; unsigned n=0; *overflow=false;
  if(snprintf(path,sizeof(path),"/proc/%ld/task",(long)pid)<0 || !(d=opendir(path))) return 0;
  while((e=readdir(d))) { char *end; unsigned long v; if(!isdigit((unsigned char)e->d_name[0]))continue; v=strtoul(e->d_name,&end,10); if(*end||!v||v>UINT32_MAX)continue; if(n==MAX_TIDS)*overflow=true; else out[n++]=(uint32_t)v; }
  (void)closedir(d); return n; }
static bool cgroup_of(pid_t pid, uint32_t tid, char *out, size_t size)
{ char path[80],line[2048]; FILE *f; if(snprintf(path,sizeof(path),"/proc/%ld/task/%u/cgroup",(long)pid,tid)<0 || !(f=fopen(path,"re")))return false;
  if(!fgets(line,sizeof(line),f)){fclose(f);return false;} fclose(f); if(strncmp(line,"0::",3))return false; line[strcspn(line,"\r\n")]=0;
  return snprintf(out,size,"%s",line+3)>=0 && strlen(line+3)<size; }
static bool write_tid(const char *path, uint32_t tid)
{ char text[32]; int n=snprintf(text,sizeof(text),"%u",tid); return n>0 && (size_t)n<sizeof(text) && write_file(path,text); }
static bool file_is(const char *path, const char *want)
{ char text[64]; if(!read_file(path,text,sizeof(text)))return false; text[strcspn(text,"\r\n")]=0; return !strcmp(text,want); }
static void prune_empty_children(const char *root)
{ DIR *d=opendir(root); struct dirent *e; char path[PATH_MAX];
  if(!d)return;
  while((e=readdir(d))) { const char *p=e->d_name; FILE *threads;
    if(strncmp(p,"t-",2) || !isdigit((unsigned char)p[2]))continue;
    for(p+=2;*p;++p)if(!isdigit((unsigned char)*p))break;
    if(*p || !join(path,sizeof(path),root,e->d_name))continue;
    { char threads_path[PATH_MAX];
      if(!join(threads_path,sizeof(threads_path),path,"cgroup.threads") || !(threads=fopen(threads_path,"re")))continue;
      if(fgetc(threads)==EOF)(void)rmdir(path);
      (void)fclose(threads);
    }
  }
  (void)closedir(d); }
static bool remove_tree(pid_t pid, const char *scope, const char *root, const char *root_rel)
{ uint32_t tids[MAX_TIDS]; bool over; unsigned n=collect(pid,tids,&over),i; char child[64],p[PATH_MAX],rel[PATH_MAX];
  (void)over;
  for(i=0;i<n;++i) if(snprintf(child,sizeof(child),"t-%u",tids[i])>0 && join(rel,sizeof(rel),root_rel,child) && cgroup_of(pid,tids[i],p,sizeof(p)) && !strcmp(p,rel) && join(p,sizeof(p),scope,"cgroup.threads")) (void)write_tid(p,tids[i]);
  for(i=0;i<n;++i) if(snprintf(child,sizeof(child),"t-%u",tids[i])>0 && join(p,sizeof(p),root,child)) (void)rmdir(p);
  prune_empty_children(root);
  (void)rmdir(root); if(join(p,sizeof(p),scope,"cgroup.subtree_control"))(void)write_file(p,"-cpu"); return true; }
static bool apply(pid_t pid, const char *scope, const char *root, const char *root_rel, unsigned quota)
{ uint32_t tids[MAX_TIDS]; bool over; unsigned n=collect(pid,tids,&over),i,verified=0; char p[PATH_MAX],rel[PATH_MAX],child[64],want[32];
  if(over || !n || quota<1 || quota>100) return false;
  if(mkdir(root,0700) && errno!=EEXIST)return false;
  if(!join(p,sizeof(p),root,"cgroup.type") || (!file_is(p,"threaded")&&!write_file(p,"threaded")))return false;
  if(!join(p,sizeof(p),scope,"cgroup.subtree_control") || !write_file(p,"+cpu") || !join(p,sizeof(p),root,"cgroup.subtree_control") || !write_file(p,"+cpu"))return false;
  if(snprintf(want,sizeof(want),"%u %u",quota*1000U,PERIOD)<0)return false;
  for(i=0;i<n;++i) {
    if(snprintf(child,sizeof(child),"t-%u",tids[i])<0 || !join(p,sizeof(p),root,child))return false;
    if(mkdir(p,0700) && errno!=EEXIST){if(tid_exists(pid,tids[i]))return false;continue;}
    if(snprintf(p,sizeof(p),"%s/%s/cgroup.type",root,child)<0 || (!file_is(p,"threaded")&&!write_file(p,"threaded"))){if(tid_exists(pid,tids[i]))return false;continue;}
    if(snprintf(p,sizeof(p),"%s/%s/cgroup.threads",root,child)<0 || !write_tid(p,tids[i])){if(tid_exists(pid,tids[i]))return false;continue;}
    if(snprintf(p,sizeof(p),"%s/%s/cpu.max",root,child)<0 || !write_file(p,want)){if(tid_exists(pid,tids[i]))return false;continue;}
  }
  prune_empty_children(root);
  for(i=0;i<n;++i) {
    if(snprintf(child,sizeof(child),"t-%u",tids[i])<0 || !join(rel,sizeof(rel),root_rel,child) || !cgroup_of(pid,tids[i],p,sizeof(p)) || strcmp(p,rel)) return false;
    if(snprintf(p,sizeof(p),"%s/%s/cpu.max",root,child)<0 || !file_is(p,want))return false;
    ++verified;
  }
  return verified==n;
}
int main(int argc, char **argv)
{ pid_t pid; const char *scope_rel,*state; char scope[PATH_MAX],root[PATH_MAX],root_rel[PATH_MAX],status[PATH_MAX],buf[64],last[64]=""; bool active=false;
  if(argc!=6 || strcmp(argv[1],"--pid") || strcmp(argv[3],"--scope")) return 64;
  pid=(pid_t)strtol(argv[2],0,10); scope_rel=argv[4]; state=argv[5];
  if(pid<=1 || strncmp(scope_rel,"/user.slice/user-",17) || strstr(scope_rel,"..") || snprintf(scope,sizeof(scope),"/sys/fs/cgroup%s",scope_rel)<0 || !join(root,sizeof(root),scope,"frame-pacer-thread-cpu") || !join(root_rel,sizeof(root_rel),scope_rel,"frame-pacer-thread-cpu") || snprintf(status,sizeof(status),"%s.status",state)<0) return 64;
  while(kill(pid,0)==0) { unsigned quota=0; bool on=false; struct timespec pause={.tv_nsec=250000000L};
    if(!read_file(state,buf,sizeof(buf))) break;
    if(sscanf(buf,"on %u",&quota)==1 && quota>=1 && quota<=100)on=true;
    if(on) { bool ok=apply(pid,scope,root,root_rel,quota); (void)snprintf(last,sizeof(last),ok?"confirmed %u\n":"off\n",quota); active=true; }
    else { if(active)remove_tree(pid,scope,root,root_rel); (void)snprintf(last,sizeof(last),"off\n"); active=false; }
    { int fd=open(status,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600); if(fd>=0){ ssize_t ignored=write(fd,last,strlen(last)); (void)ignored; (void)close(fd);} }
    (void)nanosleep(&pause,0);
  }
  if(active)remove_tree(pid,scope,root,root_rel);
  (void)unlink(status);
  (void)unlink(state);
  return 0;
}
