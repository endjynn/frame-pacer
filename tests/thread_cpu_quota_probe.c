#define _GNU_SOURCE
/*
 * Opt-in viability probe for the per-thread CPU-ceiling design.  It never
 * accepts a target PID: only the forked probe child can be placed in a scope.
 */
#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define PROBE_TIMEOUT_US UINT64_C(500000)
#define WINDOW_SECONDS 2
#define WINDOW_USEC ((uint64_t)WINDOW_SECONDS * UINT64_C(1000000))

struct worker {
    char threads_path[1500];
    atomic_bool ready;
    atomic_bool start;
    atomic_bool stop;
    bool moved;
};

struct sd_bus;
struct sd_bus_message;
struct sd_bus_error { const char *name; const char *message; int need_free; };
struct sd_api {
    void *library;
    int (*open_user)(struct sd_bus **);
    struct sd_bus *(*bus_unref)(struct sd_bus *);
    int (*message_new_method_call)(struct sd_bus *, struct sd_bus_message **,
                                   const char *, const char *, const char *, const char *);
    int (*message_append)(struct sd_bus_message *, const char *, ...);
    int (*message_open_container)(struct sd_bus_message *, char, const char *);
    int (*message_close_container)(struct sd_bus_message *);
    int (*message_read)(struct sd_bus_message *, const char *, ...);
    struct sd_bus_message *(*message_unref)(struct sd_bus_message *);
    int (*bus_call)(struct sd_bus *, struct sd_bus_message *, uint64_t,
                    struct sd_bus_error *, struct sd_bus_message **);
    void (*error_free)(struct sd_bus_error *);
};

static bool symbol(void *library, const char *name, void *target, size_t size)
{
    void *value = dlsym(library, name);
    if (!value || size != sizeof(value)) return false;
    memcpy(target, &value, size);
    return true;
}

static void api_close(struct sd_api *api)
{
    if (api->library) (void)dlclose(api->library);
    memset(api, 0, sizeof(*api));
}

static bool api_open(struct sd_api *api)
{
    void *library = dlopen("libsystemd.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!library) return false;
    memset(api, 0, sizeof(*api));
    api->library = library;
#define LOAD(member, name) if (!symbol(library, name, &api->member, sizeof(api->member))) goto fail
    LOAD(open_user, "sd_bus_open_user");
    LOAD(bus_unref, "sd_bus_unref");
    LOAD(message_new_method_call, "sd_bus_message_new_method_call");
    LOAD(message_append, "sd_bus_message_append");
    LOAD(message_open_container, "sd_bus_message_open_container");
    LOAD(message_close_container, "sd_bus_message_close_container");
    LOAD(message_read, "sd_bus_message_read");
    LOAD(message_unref, "sd_bus_message_unref");
    LOAD(bus_call, "sd_bus_call");
    LOAD(error_free, "sd_bus_error_free");
#undef LOAD
    return true;
fail:
    api_close(api);
    return false;
}

static bool host_pid_visible(void)
{
    char line[256];
    FILE *file = fopen("/proc/self/status", "re");
    bool result = false;
    if (!file) return false;
    while (fgets(line, sizeof(line), file)) {
        char *value;
        if (strncmp(line, "NSpid:", 6)) continue;
        value = line + 6;
        while (*value == ' ' || *value == '\t') ++value;
        result = !strchr(value, ' ') && !strchr(value, '\t');
        break;
    }
    (void)fclose(file);
    return result;
}

static bool scope_name(char *output, size_t size)
{
    char boot_id[64] = {0};
    FILE *file = fopen("/proc/sys/kernel/random/boot_id", "re");
    int written;
    if (!file || !fgets(boot_id, sizeof(boot_id), file)) { if (file) fclose(file); return false; }
    (void)fclose(file);
    boot_id[strcspn(boot_id, "\r\n")] = '\0';
    written = snprintf(output, size, "frame-pacer-thread-probe-u%ju-b%.12s-p%ju.scope",
                       (uintmax_t)getuid(), boot_id, (uintmax_t)getpid());
    return written > 0 && (size_t)written < size;
}

static bool property_prefix(const struct sd_api *api, struct sd_bus_message *message,
                            const char *name, const char *signature)
{
    return api->message_open_container(message, 'r', "sv") >= 0 &&
           api->message_append(message, "s", name) >= 0 &&
           api->message_open_container(message, 'v', signature) >= 0;
}

static bool property_bool(const struct sd_api *api, struct sd_bus_message *message,
                          const char *name, int value)
{
    return property_prefix(api, message, name, "b") && api->message_append(message, "b", value) >= 0 &&
           api->message_close_container(message) >= 0 && api->message_close_container(message) >= 0;
}

static bool property_string(const struct sd_api *api, struct sd_bus_message *message,
                            const char *name, const char *value)
{
    return property_prefix(api, message, name, "s") && api->message_append(message, "s", value) >= 0 &&
           api->message_close_container(message) >= 0 && api->message_close_container(message) >= 0;
}

static bool property_pid(const struct sd_api *api, struct sd_bus_message *message)
{
    uint32_t pid = (uint32_t)getpid();
    return property_prefix(api, message, "PIDs", "au") &&
           api->message_open_container(message, 'a', "u") >= 0 &&
           api->message_append(message, "u", pid) >= 0 &&
           api->message_close_container(message) >= 0 && api->message_close_container(message) >= 0 &&
           api->message_close_container(message) >= 0;
}

static bool start_scope(struct sd_api *api, struct sd_bus *bus, const char *name)
{
    struct sd_bus_message *request = 0, *reply = 0;
    struct sd_bus_error error = {0};
    bool result = false;
    if (api->message_new_method_call(bus, &request, "org.freedesktop.systemd1",
                                     "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager",
                                     "StartTransientUnit") < 0 ||
        api->message_append(request, "ss", name, "fail") < 0 ||
        api->message_open_container(request, 'a', "(sv)") < 0 ||
        !property_pid(api, request) || !property_bool(api, request, "Delegate", 1) ||
        !property_bool(api, request, "CPUAccounting", 1) ||
        !property_string(api, request, "CollectMode", "inactive-or-failed") ||
        api->message_close_container(request) < 0 ||
        api->message_open_container(request, 'a', "(sa(sv))") < 0 ||
        api->message_close_container(request) < 0 ||
        api->bus_call(bus, request, PROBE_TIMEOUT_US, &error, &reply) < 0) goto done;
    result = true;
done:
    api->error_free(&error);
    if (reply) api->message_unref(reply);
    if (request) api->message_unref(request);
    return result;
}

static bool cgroup_path(char *output, size_t size, const char *scope)
{
    char line[2048];
    unsigned int attempt;

    for (attempt = 0; attempt < 50; ++attempt) {
        FILE *file = fopen("/proc/self/cgroup", "re");

        if (file && fgets(line, sizeof(line), file) &&
            (strchr(line, '\n') || feof(file)) && !strncmp(line, "0::/", 4)) {
            char *terminal;
            int written;

            line[strcspn(line, "\r\n")] = '\0';
            terminal = strrchr(line + 3, '/');
            if (terminal && !strcmp(terminal + 1, scope)) {
                (void)fclose(file);
                written = snprintf(output, size, "/sys/fs/cgroup%s",
                                   line + 3);
                return written >= 0 && (size_t)written < size;
            }
        }
        if (file) (void)fclose(file);
        {
            struct timespec pause = {.tv_nsec = 10000000};
            (void)nanosleep(&pause, 0);
        }
    }
    return false;
}

static bool cgroup_file(char *output, size_t size, const char *root, const char *child,
                        const char *file)
{
    int written = snprintf(output, size, "%s%s%s%s%s", root, child ? "/" : "",
                           child ? child : "", file ? "/" : "", file ? file : "");
    return written >= 0 && (size_t)written < size;
}

static bool write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "we");

    if (!file) return false;
    if (fputs(text, file) < 0) {
        (void)fclose(file);
        return false;
    }
    return fclose(file) == 0;
}

static bool write_own_id(const char *path, uintmax_t id)
{
    char value[32];
    int written = snprintf(value, sizeof(value), "%ju", id);
    return written > 0 && (size_t)written < sizeof(value) && write_text(path, value);
}

static bool write_own_tid(const char *path)
{
    return write_own_id(path, (uintmax_t)syscall(SYS_gettid));
}

static bool sleep_seconds(time_t seconds)
{
    struct timespec pause = {.tv_sec = seconds};
    while (nanosleep(&pause, &pause)) {
        if (errno != EINTR) return false;
    }
    return true;
}

static void *burn_worker(void *data)
{
    struct worker *worker = data;
    volatile uint64_t value = (uint64_t)syscall(SYS_gettid);

    worker->moved = write_own_tid(worker->threads_path);
    atomic_store_explicit(&worker->ready, true, memory_order_release);
    while (!atomic_load_explicit(&worker->start, memory_order_acquire) &&
           !atomic_load_explicit(&worker->stop, memory_order_acquire)) {
        struct timespec pause = {.tv_nsec = 1000000};
        (void)nanosleep(&pause, 0);
    }
    while (!atomic_load_explicit(&worker->stop, memory_order_acquire)) {
        value = value * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    }
    return (void *)(uintptr_t)value;
}

static bool wait_workers(const struct worker *left, const struct worker *right)
{
    unsigned int attempt;

    for (attempt = 0; attempt < 100; ++attempt) {
        if (atomic_load_explicit(&left->ready, memory_order_acquire) &&
            atomic_load_explicit(&right->ready, memory_order_acquire)) {
            return left->moved && right->moved;
        }
        {
            struct timespec pause = {.tv_nsec = 10000000};
            (void)nanosleep(&pause, 0);
        }
    }
    return false;
}

static bool cpu_usage_usec(const char *path, uint64_t *usage)
{
    char key[64];
    unsigned long long value;
    FILE *file = fopen(path, "re");

    if (!file) return false;
    while (fscanf(file, "%63s %llu", key, &value) == 2) {
        if (!strcmp(key, "usage_usec")) {
            *usage = value;
            (void)fclose(file);
            return true;
        }
    }
    (void)fclose(file);
    return false;
}

/* Creates no bandwidth limit. All writes are below the verified scope root. */
static bool topology_stage(const char *scope, const char **failed_stage, int *failed_errno)
{
    char root[1400], path[1500];
    bool scope_cpu_enabled = false, root_created = false, left_created = false, right_created = false;
    bool result = false;

    *failed_stage = "construct threads root";
    *failed_errno = 0;
    if (!cgroup_file(root, sizeof(root), scope, "threads", 0)) goto done;
    *failed_stage = "create threads root";
    if (mkdir(root, 0700)) { *failed_errno = errno; goto done; }
    root_created = true;
    *failed_stage = "mark threads root threaded";
    if (!cgroup_file(path, sizeof(path), root, 0, "cgroup.type") ||
        !write_text(path, "threaded")) { *failed_errno = errno; goto done; }
    *failed_stage = "enable CPU controller below delegated scope";
    if (!cgroup_file(path, sizeof(path), scope, 0, "cgroup.subtree_control") ||
        !write_text(path, "+cpu")) { *failed_errno = errno; goto done; }
    scope_cpu_enabled = true;
    *failed_stage = "enable CPU controller below threads root";
    if (!cgroup_file(path, sizeof(path), root, 0, "cgroup.subtree_control") ||
        !write_text(path, "+cpu")) { *failed_errno = errno; goto done; }
    *failed_stage = "create left child";
    if (!cgroup_file(path, sizeof(path), root, "left", 0) || mkdir(path, 0700)) {
        *failed_errno = errno;
        goto done;
    }
    left_created = true;
    *failed_stage = "create right child";
    if (!cgroup_file(path, sizeof(path), root, "right", 0) || mkdir(path, 0700)) {
        *failed_errno = errno;
        goto done;
    }
    right_created = true;
    *failed_stage = "mark left threaded";
    if (!cgroup_file(path, sizeof(path), root, "left", "cgroup.type") ||
        !write_text(path, "threaded")) { *failed_errno = errno; goto done; }
    *failed_stage = "mark right threaded";
    if (!cgroup_file(path, sizeof(path), root, "right", "cgroup.type") ||
        !write_text(path, "threaded")) { *failed_errno = errno; goto done; }
    *failed_stage = "move probe TID into left child";
    if (!cgroup_file(path, sizeof(path), root, "left", "cgroup.threads") ||
        !write_own_tid(path)) { *failed_errno = errno; goto done; }
    result = true;
done:
    /* Return the probe process before removing only directories it created. */
    if (!cgroup_file(path, sizeof(path), scope, 0, "cgroup.threads") || !write_own_tid(path)) {
        result = false;
        *failed_stage = "return probe to delegated scope";
        *failed_errno = errno;
    }
    if (right_created && cgroup_file(path, sizeof(path), root, "right", 0) && rmdir(path)) {
        result = false;
        *failed_stage = "remove right child";
        *failed_errno = errno;
    }
    if (left_created && cgroup_file(path, sizeof(path), root, "left", 0) && rmdir(path)) {
        result = false;
        *failed_stage = "remove left child";
        *failed_errno = errno;
    }
    if (root_created && rmdir(root)) {
        result = false;
        *failed_stage = "remove threads root";
        *failed_errno = errno;
    }
    if (scope_cpu_enabled) {
        if (!cgroup_file(path, sizeof(path), scope, 0, "cgroup.subtree_control") ||
            !write_text(path, "-cpu")) {
            result = false;
            *failed_stage = "disable CPU controller below delegated scope";
            *failed_errno = errno;
        }
    }
    return result;
}

/* Applies temporary ceilings only below the verified scope and removes them before return. */
static bool quota_stage(const char *scope, const char **failed_stage, int *failed_errno,
                        uint64_t *fifty_left, uint64_t *fifty_right,
                        uint64_t *seventy_five_left, uint64_t *seventy_five_right)
{
    char root[1400], path[1500], left_stat[1500], right_stat[1500];
    struct worker left = {0}, right = {0};
    pthread_t left_thread, right_thread;
    bool scope_cpu_enabled = false, root_created = false, left_created = false, right_created = false;
    bool left_started = false, right_started = false, result = false;
    uint64_t left_before, right_before, left_after, right_after;

    *failed_stage = "construct quota threads root";
    *failed_errno = 0;
    if (!cgroup_file(root, sizeof(root), scope, "quota-threads", 0)) goto done;
    *failed_stage = "create quota threads root";
    if (mkdir(root, 0700)) { *failed_errno = errno; goto done; }
    root_created = true;
    *failed_stage = "mark quota threads root threaded";
    if (!cgroup_file(path, sizeof(path), root, 0, "cgroup.type") ||
        !write_text(path, "threaded")) { *failed_errno = errno; goto done; }
    *failed_stage = "enable CPU controller below quota scope";
    if (!cgroup_file(path, sizeof(path), scope, 0, "cgroup.subtree_control") ||
        !write_text(path, "+cpu")) { *failed_errno = errno; goto done; }
    scope_cpu_enabled = true;
    *failed_stage = "enable CPU controller below quota root";
    if (!cgroup_file(path, sizeof(path), root, 0, "cgroup.subtree_control") ||
        !write_text(path, "+cpu")) { *failed_errno = errno; goto done; }
    *failed_stage = "create quota left child";
    if (!cgroup_file(path, sizeof(path), root, "left", 0) || mkdir(path, 0700)) {
        *failed_errno = errno; goto done;
    }
    left_created = true;
    *failed_stage = "create quota right child";
    if (!cgroup_file(path, sizeof(path), root, "right", 0) || mkdir(path, 0700)) {
        *failed_errno = errno; goto done;
    }
    right_created = true;
    *failed_stage = "mark quota left child threaded";
    if (!cgroup_file(path, sizeof(path), root, "left", "cgroup.type") ||
        !write_text(path, "threaded")) { *failed_errno = errno; goto done; }
    *failed_stage = "mark quota right child threaded";
    if (!cgroup_file(path, sizeof(path), root, "right", "cgroup.type") ||
        !write_text(path, "threaded")) { *failed_errno = errno; goto done; }
    if (!cgroup_file(left.threads_path, sizeof(left.threads_path), root, "left", "cgroup.threads") ||
        !cgroup_file(right.threads_path, sizeof(right.threads_path), root, "right", "cgroup.threads") ||
        !cgroup_file(left_stat, sizeof(left_stat), root, "left", "cpu.stat") ||
        !cgroup_file(right_stat, sizeof(right_stat), root, "right", "cpu.stat")) goto done;
    *failed_stage = "start quota workers";
    if (pthread_create(&left_thread, 0, burn_worker, &left)) { *failed_errno = errno; goto done; }
    left_started = true;
    if (pthread_create(&right_thread, 0, burn_worker, &right)) { *failed_errno = errno; goto done; }
    right_started = true;
    *failed_stage = "move quota worker TIDs";
    if (!wait_workers(&left, &right)) { *failed_errno = errno; goto done; }
    *failed_stage = "apply 50 percent ceilings";
    if (!cgroup_file(path, sizeof(path), root, "left", "cpu.max") || !write_text(path, "50000 100000") ||
        !cgroup_file(path, sizeof(path), root, "right", "cpu.max") || !write_text(path, "50000 100000")) {
        *failed_errno = errno; goto done;
    }
    if (!cpu_usage_usec(left_stat, &left_before) || !cpu_usage_usec(right_stat, &right_before)) goto done;
    atomic_store_explicit(&left.start, true, memory_order_release);
    atomic_store_explicit(&right.start, true, memory_order_release);
    if (!sleep_seconds(WINDOW_SECONDS) || !cpu_usage_usec(left_stat, &left_after) ||
        !cpu_usage_usec(right_stat, &right_after)) goto done;
    *fifty_left = left_after - left_before;
    *fifty_right = right_after - right_before;
    *failed_stage = "apply 75 percent ceilings";
    if (!cgroup_file(path, sizeof(path), root, "left", "cpu.max") || !write_text(path, "75000 100000") ||
        !cgroup_file(path, sizeof(path), root, "right", "cpu.max") || !write_text(path, "75000 100000")) {
        *failed_errno = errno; goto done;
    }
    if (!cpu_usage_usec(left_stat, &left_before) || !cpu_usage_usec(right_stat, &right_before) ||
        !sleep_seconds(WINDOW_SECONDS) || !cpu_usage_usec(left_stat, &left_after) ||
        !cpu_usage_usec(right_stat, &right_after)) goto done;
    *seventy_five_left = left_after - left_before;
    *seventy_five_right = right_after - right_before;
    *failed_stage = "validate sampled ceilings";
    if (*fifty_left > WINDOW_USEC * 5 / 8 || *fifty_right > WINDOW_USEC * 5 / 8 ||
        *fifty_left + *fifty_right <= WINDOW_USEC * 3 / 4 ||
        *seventy_five_left > WINDOW_USEC * 7 / 8 || *seventy_five_right > WINDOW_USEC * 7 / 8 ||
        *seventy_five_left <= *fifty_left * 11 / 10 ||
        *seventy_five_right <= *fifty_right * 11 / 10) goto done;
    result = true;
done:
    /* Clear ceilings before asking workers to stop, even on validation failure. */
    if (left_created && cgroup_file(path, sizeof(path), root, "left", "cpu.max") &&
        !write_text(path, "max 100000")) { result = false; *failed_stage = "clear left ceiling"; *failed_errno = errno; }
    if (right_created && cgroup_file(path, sizeof(path), root, "right", "cpu.max") &&
        !write_text(path, "max 100000")) { result = false; *failed_stage = "clear right ceiling"; *failed_errno = errno; }
    atomic_store_explicit(&left.stop, true, memory_order_release);
    atomic_store_explicit(&right.stop, true, memory_order_release);
    atomic_store_explicit(&left.start, true, memory_order_release);
    atomic_store_explicit(&right.start, true, memory_order_release);
    if (right_started && pthread_join(right_thread, 0)) { result = false; *failed_stage = "join right worker"; *failed_errno = errno; }
    if (left_started && pthread_join(left_thread, 0)) { result = false; *failed_stage = "join left worker"; *failed_errno = errno; }
    if (right_created && cgroup_file(path, sizeof(path), root, "right", 0) && rmdir(path)) { result = false; *failed_stage = "remove quota right child"; *failed_errno = errno; }
    if (left_created && cgroup_file(path, sizeof(path), root, "left", 0) && rmdir(path)) { result = false; *failed_stage = "remove quota left child"; *failed_errno = errno; }
    if (root_created && rmdir(root)) { result = false; *failed_stage = "remove quota threads root"; *failed_errno = errno; }
    if (scope_cpu_enabled && (!cgroup_file(path, sizeof(path), scope, 0, "cgroup.subtree_control") ||
        !write_text(path, "-cpu"))) { result = false; *failed_stage = "disable CPU controller below quota scope"; *failed_errno = errno; }
    return result;
}

int main(void)
{
    struct sd_api api;
    struct sd_bus *bus = 0;
    char name[160], path[1200];
    const char *failed_stage;
    int failed_errno;
    uint64_t fifty_left = 0, fifty_right = 0, seventy_five_left = 0, seventy_five_right = 0;

    if (!getenv("FRAME_PACER_THREAD_CPU_QUOTA_INTEGRATION")) {
        fputs("thread-cpu-quota-probe: set FRAME_PACER_THREAD_CPU_QUOTA_INTEGRATION=1 to run\n", stderr);
        return 77;
    }
    if (!host_pid_visible()) {
        fputs("thread-cpu-quota-probe: host PID is not visible; no mutation\n", stderr);
        return 77;
    }
    if (!api_open(&api) || api.open_user(&bus) < 0 || !scope_name(name, sizeof(name)) ||
        !start_scope(&api, bus, name) || !cgroup_path(path, sizeof(path), name)) {
        if (bus) api.bus_unref(bus);
        api_close(&api);
        fputs("thread-cpu-quota-probe: delegated scope unavailable; no threaded topology attempted\n",
              stderr);
        return 77;
    }
    if (!topology_stage(path, &failed_stage, &failed_errno)) {
        api.bus_unref(bus);
        api_close(&api);
        fprintf(stderr, "thread-cpu-quota-probe: threaded topology unavailable at %s%s%s; "
                "rolled back owned paths\n", failed_stage,
                failed_errno ? ": " : "", failed_errno ? strerror(failed_errno) : "");
        return 77;
    }
    if (!quota_stage(path, &failed_stage, &failed_errno, &fifty_left, &fifty_right,
                     &seventy_five_left, &seventy_five_right)) {
        api.bus_unref(bus);
        api_close(&api);
        fprintf(stderr, "thread-cpu-quota-probe: quota stage unavailable at %s%s%s; "
                "cleared limits and rolled back owned paths\n", failed_stage,
                failed_errno ? ": " : "", failed_errno ? strerror(failed_errno) : "");
        return 77;
    }
    printf("scope=%s cgroup=%s thread-topology=confirmed "
           "50%%=%" PRIu64 ",%" PRIu64 "us 75%%=%" PRIu64 ",%" PRIu64 "us\n",
           name, path, fifty_left, fifty_right, seventy_five_left, seventy_five_right);
    api.bus_unref(bus);
    api_close(&api);
    return 0;
}
