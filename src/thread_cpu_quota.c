#define _GNU_SOURCE
#include "thread_cpu_quota.h"
#include "thread_cpu_external.h"
#include "thread_cpu_systemd.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
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

static bool write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "we");
    bool written;
    bool closed;

    if (!file) return false;
    written = fputs(text, file) >= 0;
    closed = fclose(file) == 0;
    return written && closed;
}

#ifdef FRAME_PACER_TEST
bool frame_pacer_thread_cpu_quota_test_write_text(const char *path, const char *text)
{
    return write_text(path, text);
}
#endif
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
{
    int written = snprintf(out, n, "%s%s%s%s%s", base,
                           child ? "/" : "", child ? child : "",
                           file ? "/" : "", file ? file : "");

    return written >= 0 && (size_t)written < n;
}

static bool write_tid(const char *path, uint32_t tid)
{
    char text[32];
    int written = snprintf(text, sizeof(text), "%u", tid);

    return written > 0 && (size_t)written < sizeof(text) &&
           write_text(path, text);
}

static bool complete_line(FILE *file, const char *line)
{
    int byte;

    if (strchr(line, '\n') || feof(file))
        return true;
    do {
        byte = fgetc(file);
    } while (byte != '\n' && byte != EOF);
    return false;
}

static bool parse_host_pid_line(const char *line)
{
    const char *value;

    if (!line || strncmp(line, "NSpid:", 6))
        return false;
    value = line + 6;
    while (*value == ' ' || *value == '\t')
        ++value;
    if (!isdigit((unsigned char)*value))
        return false;
    while (isdigit((unsigned char)*value))
        ++value;
    if (*value == '\r')
        ++value;
    return *value == '\n' && value[1] == '\0';
}

static bool valid_boot_id(const char *boot)
{
    size_t index;

    if (!boot || strlen(boot) != 36)
        return false;
    for (index = 0; index < 36; ++index) {
        bool separator = index == 8 || index == 13 || index == 18 ||
                         index == 23;

        if (separator ? boot[index] != '-' :
                        !isxdigit((unsigned char)boot[index]))
            return false;
    }
    return true;
}

#ifdef FRAME_PACER_TEST
bool frame_pacer_thread_cpu_quota_test_parse_host_pid_line(const char *line)
{
    return parse_host_pid_line(line);
}

bool frame_pacer_thread_cpu_quota_test_valid_boot_id(const char *boot)
{
    return valid_boot_id(boot);
}
#endif

static bool host_pid_visible(void)
{
    char line[256];
    FILE *file = fopen("/proc/self/status", "re");
    bool visible = false;

    if (!file) return false;
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "NSpid:", 6)) continue;
        visible = complete_line(file, line) && parse_host_pid_line(line);
        break;
    }
    (void)fclose(file);
    return visible;
}

static bool identity(char *scope, size_t n)
{
    char boot[64] = {0};
    FILE *file = fopen("/proc/sys/kernel/random/boot_id", "re");
    int written;

    if (!file || !fgets(boot, sizeof(boot), file) ||
        !complete_line(file, boot)) {
        if (file) (void)fclose(file);
        return false;
    }
    (void)fclose(file);
    boot[strcspn(boot, "\r\n")] = '\0';
    if (!valid_boot_id(boot))
        return false;
    written = snprintf(scope, n,
                       "frame-pacer-thread-cpu-u%ju-b%.12s-p%ju.scope",
                       (uintmax_t)getuid(), boot, (uintmax_t)getpid());
    return written > 0 && (size_t)written < n;
}

static bool mount_is_rw(const char *path)
{
    char line[2048];
    FILE *file = fopen(path, "re");
    bool writable = false;

    if (!file) return false;
    while (fgets(line, sizeof(line), file)) {
        char *mount = strstr(line, " /sys/fs/cgroup ");
        char *dash = strstr(line, " - ");

        if (!complete_line(file, line))
            continue;
        if (mount && dash && mount < dash && !strncmp(mount + 16, "rw,", 3)) {
            writable = true;
            break;
        }
    }
    (void)fclose(file);
    return writable;
}
static bool cgroup_root(char *out, size_t n)
{
    pid_t pid = getppid();
    unsigned int i;
    int written;

    if (mount_is_rw("/proc/self/mountinfo")) {
        written = snprintf(out, n, "/sys/fs/cgroup");
        return written > 0 && (size_t)written < n;
    }
    /* pressure-vessel makes cgroupfs read-only for the game, but its direct
     * launcher parent remains in the caller's writable user mount namespace. */
    for (i = 0; i < 24 && pid > 1; ++i) {
        char status[64], mountinfo[64], line[256];
        FILE *file;
        pid_t parent = 0;

        written = snprintf(status, sizeof(status), "/proc/%ld/status",
                           (long)pid);
        if (written < 0 || (size_t)written >= sizeof(status) ||
            !(file = fopen(status, "re")))
            break;
        while (fgets(line, sizeof(line), file)) {
            unsigned long uid, value;

            if (!complete_line(file, line))
                continue;
            if (sscanf(line, "Uid:\t%lu", &uid) == 1 && uid != (unsigned long)getuid()) {
                (void)fclose(file);
                return false;
            }
            if (sscanf(line, "PPid:\t%lu", &value) == 1) parent = (pid_t)value;
        }
        (void)fclose(file);
        written = snprintf(mountinfo, sizeof(mountinfo),
                           "/proc/%ld/mountinfo", (long)pid);
        if (written < 0 || (size_t)written >= sizeof(mountinfo))
            return false;
        if (mount_is_rw(mountinfo)) {
            written = snprintf(out, n, "/proc/%ld/root/sys/fs/cgroup",
                               (long)pid);
            if (written > 0 && (size_t)written < n &&
                access(out, R_OK | X_OK) == 0)
                return true;
        }
        pid = parent;
    }
    return false;
}
static bool scope_path(struct frame_pacer_thread_cpu_quota *q, const char *root,
                       const char *scope)
{
    char line[2048], marker[256];
    unsigned int i;
    int marker_length;

    marker_length = snprintf(marker, sizeof(marker),
                             "/%s/frame-pacer-thread-cpu/t-", scope);
    if (marker_length < 0 || (size_t)marker_length >= sizeof(marker))
        return false;
    for (i = 0; i < 50; ++i) {
        FILE *file = fopen("/proc/self/cgroup", "re");

        if (file && fgets(line, sizeof(line), file) &&
            (strchr(line, '\n') || feof(file)) &&
            !strncmp(line, "0::/", 4)) {
            char *path = line + 3;
            char *terminal;
            int scope_length, proc_length;

            path[strcspn(path, "\r\n")] = '\0';
            terminal = strrchr(path, '/');
            terminal = terminal ? terminal + 1 : path;
            scope_length = snprintf(q->scope, sizeof(q->scope), "%s%s",
                                    root, path);
            proc_length = snprintf(q->cgroup_proc, sizeof(q->cgroup_proc),
                                   "%s/frame-pacer-thread-cpu", path);
            if (!strcmp(terminal, scope) && scope_length >= 0 &&
                (size_t)scope_length < sizeof(q->scope) && proc_length >= 0 &&
                (size_t)proc_length < sizeof(q->cgroup_proc)) {
                (void)fclose(file);
                return true;
            }
            /* A second backend can initialize after the controller moved this
             * process into its own threaded child.  Accept only the exact scope
             * identity and a numeric owned child; this joins that controller and
             * never claims a sibling hierarchy. */
            {
                char *owned = strstr(path, marker);

                if (owned) {
                    char *tid = owned + strlen(marker);
                    size_t prefix_length;
                    char saved;

                    if (!isdigit((unsigned char)*tid) ||
                        tid[strspn(tid, "0123456789")] != '\0')
                        goto retry;
                    prefix_length = (size_t)(owned - path) + 1 + strlen(scope);
                    saved = path[prefix_length];
                    path[prefix_length] = '\0';
                    scope_length = snprintf(q->scope, sizeof(q->scope),
                                            "%s%s", root, path);
                    proc_length = snprintf(
                        q->cgroup_proc, sizeof(q->cgroup_proc),
                        "%s/frame-pacer-thread-cpu", path);
                    path[prefix_length] = saved;
                    if (scope_length >= 0 &&
                        (size_t)scope_length < sizeof(q->scope) &&
                        proc_length >= 0 &&
                        (size_t)proc_length < sizeof(q->cgroup_proc)) {
                        (void)fclose(file);
                        return true;
                    }
                }
            }
        }
retry:
        if (file) (void)fclose(file);
        {
            struct timespec delay = {.tv_nsec = 10000000};

            (void)nanosleep(&delay, 0);
        }
    }
    return false;
}
static uint32_t collect(uint32_t out[FRAME_PACER_THREAD_CPU_QUOTA_TIDS_MAX], bool *overflow)
{
    DIR *directory = opendir("/proc/self/task");
    struct dirent *entry;
    uint32_t count = 0;

    *overflow = false;
    if (!directory) return 0;
    while ((entry = readdir(directory))) {
        char *end;
        unsigned long value;

        if (!isdigit((unsigned char)entry->d_name[0])) continue;
        value = strtoul(entry->d_name, &end, 10);
        if (*end || !value || value > UINT32_MAX) continue;
        if (count == FRAME_PACER_THREAD_CPU_QUOTA_TIDS_MAX)
            *overflow = true;
        else
            out[count++] = (uint32_t)value;
    }
    (void)closedir(directory);
    return count;
}
static bool tid_in(const uint32_t *tids, uint32_t count, uint32_t tid)
{
    uint32_t i;

    for (i = 0; i < count; ++i)
        if (tids[i] == tid) return true;
    return false;
}
static bool tid_path_is(uint32_t tid, const char *expected)
{
    char path[128], line[2048];
    FILE *file;
    int written = snprintf(path, sizeof(path),
                           "/proc/self/task/%u/cgroup", tid);

    if (written < 0 || (size_t)written >= sizeof(path) ||
        !(file = fopen(path, "re")))
        return false;
    if (!fgets(line, sizeof(line), file) ||
        (!strchr(line, '\n') && !feof(file))) {
        (void)fclose(file);
        return false;
    }
    (void)fclose(file);
    line[strcspn(line, "\r\n")] = '\0';
    return !strncmp(line, "0::", 3) && !strcmp(line + 3, expected);
}
static bool tid_exists(uint32_t tid)
{
    char path[64];
    int written = snprintf(path, sizeof(path), "/proc/self/task/%u", tid);

    return written > 0 && (size_t)written < sizeof(path) &&
           !access(path, F_OK);
}
static bool file_is(const char *path, const char *wanted)
{
    char text[64];
    FILE *file = fopen(path, "re");
    bool matches = false;

    if (file && fgets(text, sizeof(text), file)) {
        text[strcspn(text, "\r\n")] = '\0';
        matches = !strcmp(text, wanted);
    }
    if (file) (void)fclose(file);
    return matches;
}

static void cleanup(struct frame_pacer_thread_cpu_quota *q)
{
    uint32_t now[FRAME_PACER_THREAD_CPU_QUOTA_TIDS_MAX], count, i;
    bool overflow;
    char path[1400], expected[1400], child[64];

    if (q->external) {
        (void)frame_pacer_thread_cpu_external_write(q->external_state, false,
                                                    0);
        frame_pacer_thread_cpu_external_wait_off(q->external_state);
        frame_pacer_thread_cpu_external_reap(q);
        q->external = false;
        q->cgroup_proc[0] = '\0';
        return;
    }
    if (q->owner && q->cgroup[0]) {
        record_state(q, "remove owned threaded topology");
        count = collect(now, &overflow);
        (void)overflow;
        for (i = 0; i < count; ++i) {
            char scope_threads[1400];
            int written;

            if (!tid_in(q->tids, q->observed_threads, now[i])) continue;
            written = snprintf(child, sizeof(child), "t-%u", now[i]);
            if (written > 0 && (size_t)written < sizeof(child) &&
                join_path(path, sizeof(path), q->cgroup, child, 0) &&
                join_path(expected, sizeof(expected), q->cgroup_proc, child,
                          0) &&
                tid_path_is(now[i], expected) &&
                join_path(scope_threads, sizeof(scope_threads), q->scope, 0,
                          "cgroup.threads"))
                (void)write_tid(scope_threads, now[i]);
        }
        for (i = 0; i < q->observed_threads; ++i) {
            int written = snprintf(child, sizeof(child), "t-%u", q->tids[i]);

            if (written > 0 && (size_t)written < sizeof(child) &&
                join_path(path, sizeof(path), q->cgroup, child, 0))
                (void)rmdir(path);
        }
        (void)rmdir(q->cgroup);
        if (join_path(path, sizeof(path), q->scope, 0,
                      "cgroup.subtree_control"))
            (void)write_text(path, "-cpu");
    }
    q->owner = false;
    q->cgroup[0] = q->cgroup_proc[0] = '\0';
    q->observed_threads = 0;
}
static bool activate(struct frame_pacer_thread_cpu_quota *q, uint32_t quota)
{
    struct frame_pacer_systemd systemd = {0};
    char path[1400], root[80], name[160];

    if (!host_pid_visible() || !identity(name, sizeof(name))) {
        record_failure(q, "open delegated user scope", errno);
        return false;
    }
    if (!frame_pacer_systemd_open(&systemd)) {
        const char *failure_stage = 0;
        bool external = frame_pacer_thread_cpu_external_start_native(
            q, name, quota, &failure_stage);

        if (!external)
            record_failure(q, failure_stage ? failure_stage :
                                          "start native external controller",
                           errno);

        return external;
    }
    /* An existing same-identity scope is acceptable. */
    (void)frame_pacer_systemd_start_scope(&systemd, name, getpid());
    if (!scope_path(q, "/sys/fs/cgroup", name)) {
        record_failure(q, "verify delegated scope", errno);
        frame_pacer_systemd_close(&systemd);
        return false;
    }
    if (!cgroup_root(root, sizeof(root))) {
        bool external = frame_pacer_thread_cpu_external_start_service(
            q, &systemd, name, quota);

        frame_pacer_systemd_close(&systemd);
        if (!external)
            record_failure(q, "start external controller", errno);
        return external;
    }
    /* Reconstruct the verified scope through the writable cgroup filesystem. */
    if (!scope_path(q, root, name)) {
        frame_pacer_systemd_close(&systemd);
        record_failure(q, "verify writable delegated scope", errno);
        return false;
    }
    frame_pacer_systemd_close(&systemd);
    if (!join_path(q->cgroup, sizeof(q->cgroup), q->scope,
                   "frame-pacer-thread-cpu", 0)) {
        record_failure(q, "construct threaded root", ENAMETOOLONG);
        return false;
    }
    if (mkdir(q->cgroup, 0700)) {
        record_failure(q, errno == EEXIST ? "claim threaded root" :
                                              "create threaded root",
                       errno);
        return false;
    }
    q->owner = true;
    record_state(q, "created threaded root");
    if (!join_path(path, sizeof(path), q->cgroup, 0, "cgroup.type") ||
        !write_text(path, "threaded") ||
        !join_path(path, sizeof(path), q->scope, 0,
                   "cgroup.subtree_control") ||
        !write_text(path, "+cpu") ||
        !join_path(path, sizeof(path), q->cgroup, 0,
                   "cgroup.subtree_control") ||
        !write_text(path, "+cpu")) {
        record_failure(q, "initialize threaded CPU topology", errno);
        cleanup(q);
        return false;
    }
    return true;
}
enum reconcile_result {
    RECONCILE_FATAL,
    RECONCILE_INCOMPLETE,
    RECONCILE_CONFIRMED
};

static enum reconcile_result reconcile(struct frame_pacer_thread_cpu_quota *q, uint32_t quota)
{
    uint32_t tids[FRAME_PACER_THREAD_CPU_QUOTA_TIDS_MAX];
    uint32_t count, i, managed, verified;
    bool overflow, incomplete = false;
    char child[64], path[1400], wanted[32];
    int written;

    count = collect(tids, &overflow);
    if (overflow || !count) return RECONCILE_FATAL;
    written = snprintf(wanted, sizeof(wanted), "%u %u", quota * 1000U,
                       CPU_PERIOD);
    if (written < 0 || (size_t)written >= sizeof(wanted))
        return RECONCILE_FATAL;
    managed = 0;
    for (i = 0; i < count; ++i) {
        written = snprintf(child, sizeof(child), "t-%u", tids[i]);
        if (written < 0 || (size_t)written >= sizeof(child) ||
            !join_path(path, sizeof(path), q->cgroup, child, 0))
            return RECONCILE_FATAL;
        if (mkdir(path, 0700) && errno != EEXIST) {
            if (!tid_exists(tids[i])) continue;
            incomplete = true;
            continue;
        }
        if (!join_path(path, sizeof(path), q->cgroup, child, "cgroup.type") ||
            (!file_is(path, "threaded") && !write_text(path, "threaded"))) {
            if (!tid_exists(tids[i])) continue;
            incomplete = true;
            continue;
        }
        if (!join_path(path, sizeof(path), q->cgroup, child,
                       "cgroup.threads"))
            return RECONCILE_FATAL;
        if (!write_tid(path, tids[i])) {
            /* `/proc/self/task` can race an exiting renderer thread. */
            if (!tid_exists(tids[i])) continue;
            incomplete = true;
            continue;
        }
        if (!join_path(path, sizeof(path), q->cgroup, child, "cpu.max") ||
            !write_text(path, wanted)) {
            if (!tid_exists(tids[i])) continue;
            incomplete = true;
            continue;
        }
        tids[managed++] = tids[i];
    }
    if (!managed) return RECONCILE_INCOMPLETE;
    /* A vanished TID leaves an empty, known child; remove only that child. */
    for (i = 0; i < q->observed_threads; ++i) {
        written = snprintf(child, sizeof(child), "t-%u", q->tids[i]);
        if (!tid_in(tids, managed, q->tids[i]) && written > 0 &&
            (size_t)written < sizeof(child) &&
            join_path(path, sizeof(path), q->cgroup, child, 0))
            (void)rmdir(path);
    }
    verified = 0;
    for (i = 0; i < managed; ++i) {
        char expected[1400];

        written = snprintf(child, sizeof(child), "t-%u", tids[i]);
        if (written < 0 || (size_t)written >= sizeof(child) ||
            !join_path(expected, sizeof(expected), q->cgroup_proc, child, 0) ||
            !tid_path_is(tids[i], expected) ||
            !join_path(path, sizeof(path), q->cgroup, child, "cpu.max") ||
            !file_is(path, wanted)) {
            if (!tid_exists(tids[i])) continue;
            incomplete = true;
            continue;
        }
        tids[verified++] = tids[i];
    }
    if (!verified) return RECONCILE_INCOMPLETE;
    q->observed_threads = verified;
    memcpy(q->tids, tids, verified * sizeof(tids[0]));
    return incomplete || verified != count ? RECONCILE_INCOMPLETE :
                                             RECONCILE_CONFIRMED;
}
static void *worker(void *arg)
{
    struct frame_pacer_thread_cpu_quota *q = arg;
    bool active = false;
    uint32_t last = 0;

    for (;;) {
        bool enabled;
        uint32_t wanted;

        (void)pthread_mutex_lock(&q->mutex);
        while (!q->stop && !q->requested_enabled && !active)
            (void)pthread_cond_wait(&q->changed, &q->mutex);
        if (q->stop) {
            (void)pthread_mutex_unlock(&q->mutex);
            break;
        }
        enabled = q->requested_enabled;
        wanted = q->requested;
        (void)pthread_mutex_unlock(&q->mutex);
        if (!enabled) {
            if (active || q->external) {
                record_state(q, "explicit policy cleanup");
                cleanup(q);
            }
            active = false;
            continue;
        }
        if (!active) active = activate(q, wanted);
        if (active && q->external) {
            bool confirmed;

            if (last != wanted &&
                !frame_pacer_thread_cpu_external_write(q->external_state,
                                                       true, wanted)) {
                (void)pthread_mutex_lock(&q->mutex);
                q->confirmed = false;
                (void)pthread_mutex_unlock(&q->mutex);
            } else if (last != wanted) {
                last = wanted;
            }
            confirmed = frame_pacer_thread_cpu_external_confirmed(
                q->external_state, wanted);
            (void)pthread_mutex_lock(&q->mutex);
            q->confirmed = confirmed && q->requested_enabled &&
                           q->requested == wanted;
            (void)pthread_mutex_unlock(&q->mutex);
            if (!confirmed) active = false;
        } else if (active) {
            enum reconcile_result result = reconcile(q, wanted);

            if (result == RECONCILE_CONFIRMED) {
                last = wanted;
                (void)pthread_mutex_lock(&q->mutex);
                q->confirmed = q->requested_enabled &&
                               q->requested == wanted;
                (void)pthread_mutex_unlock(&q->mutex);
            } else {
                (void)pthread_mutex_lock(&q->mutex);
                q->confirmed = false;
                (void)pthread_mutex_unlock(&q->mutex);
                if (result == RECONCILE_FATAL) {
                    record_state(q, "fatal reconciliation cleanup");
                    cleanup(q);
                    active = false;
                }
            }
        } else {
            (void)pthread_mutex_lock(&q->mutex);
            q->confirmed = false;
            (void)pthread_mutex_unlock(&q->mutex);
        }
        {
            struct timespec delay = {.tv_nsec = POLL_NS};

            (void)nanosleep(&delay, 0);
        }
    }
    if (active || q->external || q->owner) cleanup(q);
    return 0;
}
void frame_pacer_thread_cpu_quota_init(struct frame_pacer_thread_cpu_quota *q)
{
    if (!q) return;
    memset(q, 0, sizeof(*q));
    if (pthread_mutex_init(&q->mutex, 0)) return;
    if (pthread_cond_init(&q->changed, 0)) {
        (void)pthread_mutex_destroy(&q->mutex);
        memset(q, 0, sizeof(*q));
        return;
    }
    q->initialized = true;
    if (!pthread_create(&q->worker, 0, worker, q)) q->worker_started = true;
}
void frame_pacer_thread_cpu_quota_destroy(struct frame_pacer_thread_cpu_quota *q)
{
    if (!q || !q->initialized) return;
    (void)pthread_mutex_lock(&q->mutex);
    q->stop = true;
    q->confirmed = false;
    (void)pthread_cond_signal(&q->changed);
    (void)pthread_mutex_unlock(&q->mutex);
    if (q->worker_started) (void)pthread_join(q->worker, 0);
    (void)pthread_cond_destroy(&q->changed);
    (void)pthread_mutex_destroy(&q->mutex);
    memset(q, 0, sizeof(*q));
}
void frame_pacer_thread_cpu_quota_publish(
    struct frame_pacer_thread_cpu_quota *q, bool enabled, uint32_t percent)
{
    if (!q || !q->initialized) return;
    if (!enabled || percent < 1 || percent > 100) {
        enabled = false;
        percent = 0;
    }
    (void)pthread_mutex_lock(&q->mutex);
    if (q->requested_enabled != enabled || q->requested != percent)
        q->confirmed = false;
    q->requested_enabled = enabled;
    q->requested = percent;
    (void)pthread_cond_signal(&q->changed);
    (void)pthread_mutex_unlock(&q->mutex);
}
void frame_pacer_thread_cpu_quota_set_logger(struct frame_pacer_thread_cpu_quota *q,
                                             void (*log)(const char *))
{
    if (!q || !q->initialized) return;
    (void)pthread_mutex_lock(&q->mutex);
    q->log = log;
    (void)pthread_mutex_unlock(&q->mutex);
}
bool frame_pacer_thread_cpu_quota_confirmed(
    struct frame_pacer_thread_cpu_quota *q, uint32_t *percent)
{
    bool confirmed = false;

    if (percent) *percent = 0;
    if (!q || !q->initialized) return false;
    (void)pthread_mutex_lock(&q->mutex);
    confirmed = q->requested_enabled && q->confirmed;
    if (confirmed && percent) *percent = q->requested;
    (void)pthread_mutex_unlock(&q->mutex);
    return confirmed;
}
