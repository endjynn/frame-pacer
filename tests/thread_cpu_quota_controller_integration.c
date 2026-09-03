#define _GNU_SOURCE
/* Opt-in black-box test of the production controller.  Never add to make check.
 */
#include "thread_cpu_quota.h"

#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

struct worker {
    atomic_uint tid;
    atomic_bool stop;
};

#define EXTRA_WORKERS 128U
static char last_quota_log[192];

static void quota_log(const char *message)
{
    if (message)
        (void)snprintf(last_quota_log, sizeof(last_quota_log), "%s", message);
}

static void *run(void *data)
{
    struct worker *worker = data;
    volatile uint64_t value = 1;
    atomic_store(&worker->tid, (unsigned int)syscall(SYS_gettid));
    while (!atomic_load(&worker->stop))
        value = value * 1103515245U + 12345U;
    return (void *)(uintptr_t)value;
}

static void *idle(void *data)
{
    struct worker *worker = data;

    atomic_store(&worker->tid, (unsigned int)syscall(SYS_gettid));
    while (!atomic_load(&worker->stop)) {
        struct timespec pause = {.tv_nsec = 1000000};
        (void)nanosleep(&pause, 0);
    }
    return 0;
}

static bool wait_confirmed(struct frame_pacer_thread_cpu_quota *quota,
                           uint32_t percent)
{
    unsigned int attempt;
    for (attempt = 0; attempt < 80; ++attempt) {
        uint32_t actual = 0;
        if (frame_pacer_thread_cpu_quota_confirmed(quota, &actual) &&
            actual == percent)
            return true;
        {
            struct timespec pause = {.tv_nsec = 50000000};
            (void)nanosleep(&pause, 0);
        }
    }
    return false;
}

static bool wait_path_removed(const char *path)
{
    unsigned int attempt;

    for (attempt = 0; attempt < 80; ++attempt) {
        struct timespec pause = {.tv_nsec = 25000000L};

        if (access(path, F_OK) != 0)
            return true;
        (void)nanosleep(&pause, 0);
    }
    return false;
}

static bool remains_unconfirmed(struct frame_pacer_thread_cpu_quota *quota)
{
    unsigned int attempt;

    for (attempt = 0; attempt < 10; ++attempt) {
        struct timespec pause = {.tv_nsec = 50000000L};

        (void)nanosleep(&pause, 0);
        if (frame_pacer_thread_cpu_quota_confirmed(quota, 0))
            return false;
    }
    return true;
}

static bool tid_in_own_child(uint32_t tid, const char *root, uint32_t percent)
{
    char proc[128], path[1400], line[256], expected[1400], quota[32];
    FILE *file;
    int result;
    result = snprintf(proc, sizeof(proc), "/proc/self/task/%u/cgroup", tid);
    if (result < 0 || (size_t)result >= sizeof(proc))
        return false;
    file = fopen(proc, "re");
    if (!file || !fgets(line, sizeof(line), file)) {
        if (file)
            fclose(file);
        return false;
    }
    (void)fclose(file);
    char *ending = strpbrk(line, "\r\n");
    if (ending)
        *ending = '\0';
    result = snprintf(expected, sizeof(expected), "0::%s/t-%u",
                      root + strlen("/sys/fs/cgroup"), tid);
    if (result < 0 || (size_t)result >= sizeof(expected) ||
        strcmp(line, expected))
        return false;
    result = snprintf(path, sizeof(path), "%s/t-%u/cpu.max", root, tid);
    if (result < 0 || (size_t)result >= sizeof(path))
        return false;
    file = fopen(path, "re");
    if (!file || !fgets(line, sizeof(line), file)) {
        if (file)
            fclose(file);
        return false;
    }
    (void)fclose(file);
    result = snprintf(quota, sizeof(quota), "%u 100000", percent * 1000U);
    ending = strpbrk(line, "\r\n");
    if (ending)
        *ending = '\0';
    return result > 0 && (size_t)result < sizeof(quota) && !strcmp(line, quota);
}

static bool discover_owned_root(uint32_t tid, char *root, size_t size)
{
    char proc[128], line[1400], suffix[64];
    FILE *file;
    char *end;
    int written =
        snprintf(proc, sizeof(proc), "/proc/self/task/%u/cgroup", tid);

    if (written < 0 || (size_t)written >= sizeof(proc))
        return false;
    file = fopen(proc, "re");
    if (!file || !fgets(line, sizeof(line), file)) {
        if (file)
            (void)fclose(file);
        return false;
    }
    (void)fclose(file);
    char *ending = strpbrk(line, "\r\n");
    if (ending)
        *ending = '\0';
    written =
        snprintf(suffix, sizeof(suffix), "/frame-pacer-thread-cpu/t-%u", tid);
    if (written < 0 || (size_t)written >= sizeof(suffix) ||
        strncmp(line, "0::", 3) || strlen(line + 3) <= strlen(suffix))
        return false;
    end = line + strlen(line) - strlen(suffix);
    if (strcmp(end, suffix))
        return false;
    *end = '\0';
    written = snprintf(root, size, "/sys/fs/cgroup%s/frame-pacer-thread-cpu",
                       line + 3);
    return written >= 0 && (size_t)written < size;
}

int main(void)
{
    struct frame_pacer_thread_cpu_quota quota;
    struct worker left = {0}, right = {0};
    struct worker extra[EXTRA_WORKERS] = {0};
    pthread_t left_thread, right_thread;
    pthread_t extra_threads[EXTRA_WORKERS];
    unsigned int index;
    char root[sizeof(quota.cgroup)];

    if (!getenv("FRAME_PACER_THREAD_CPU_QUOTA_INTEGRATION"))
        return 77;
#ifdef FRAME_PACER_TEST
    {
        char helper[1200];

        if (!frame_pacer_thread_cpu_quota_test_runtime_helper_path(
                helper, sizeof(helper))) {
            perror("runtime helper path");
            return 77;
        }
    }
#endif
    frame_pacer_thread_cpu_quota_init(&quota);
    frame_pacer_thread_cpu_quota_set_logger(&quota, quota_log);
    assert(!pthread_create(&left_thread, 0, run, &left));
    assert(!pthread_create(&right_thread, 0, run, &right));
    for (index = 0; index < EXTRA_WORKERS; ++index)
        assert(!pthread_create(&extra_threads[index], 0, idle, &extra[index]));
    while (!atomic_load(&left.tid) || !atomic_load(&right.tid)) {
        struct timespec pause = {.tv_nsec = 1000000};
        (void)nanosleep(&pause, 0);
    }
    frame_pacer_thread_cpu_quota_publish(&quota, true, 50);
    if (!wait_confirmed(&quota, 50))
        goto unavailable;
    if (quota.cgroup[0])
        assert(snprintf(root, sizeof(root), "%s", quota.cgroup) > 0);
    else
        assert(discover_owned_root(atomic_load(&left.tid), root, sizeof(root)));
    assert(tid_in_own_child(atomic_load(&left.tid), root, 50));
    assert(tid_in_own_child(atomic_load(&right.tid), root, 50));
    {
        const char *failure_trigger =
            getenv("FRAME_PACER_TEST_CONTROLLER_FAIL_WRITES_WHEN");
        int trigger_fd = -1;

        if (failure_trigger && *failure_trigger) {
            trigger_fd = open(failure_trigger,
                              O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
            assert(trigger_fd >= 0);
            assert(!close(trigger_fd));
        }
        frame_pacer_thread_cpu_quota_publish(&quota, true, 75);
        if (trigger_fd >= 0) {
            assert(remains_unconfirmed(&quota));
            assert(!unlink(failure_trigger));
        }
    }
    assert(wait_confirmed(&quota, 75));
    if (quota.cgroup[0])
        assert(!strcmp(root, quota.cgroup));
    assert(tid_in_own_child(atomic_load(&left.tid), root, 75));
    assert(tid_in_own_child(atomic_load(&right.tid), root, 75));
    frame_pacer_thread_cpu_quota_publish(&quota, false, 0);
    assert(wait_path_removed(root));
    frame_pacer_thread_cpu_quota_publish(&quota, true, 60);
    assert(wait_confirmed(&quota, 60));
    if (quota.cgroup[0])
        assert(!strcmp(root, quota.cgroup));
    else {
        char resumed_root[sizeof(root)];

        assert(discover_owned_root(atomic_load(&left.tid), resumed_root,
                                   sizeof(resumed_root)));
        assert(!strcmp(root, resumed_root));
    }
    assert(tid_in_own_child(atomic_load(&left.tid), root, 60));
    assert(tid_in_own_child(atomic_load(&right.tid), root, 60));
    if (quota.external_pid > 0)
        assert(!kill(quota.external_pid, SIGTERM));
    frame_pacer_thread_cpu_quota_publish(&quota, false, 0);
    atomic_store(&left.stop, true);
    atomic_store(&right.stop, true);
    for (index = 0; index < EXTRA_WORKERS; ++index)
        atomic_store(&extra[index].stop, true);
    assert(!pthread_join(left_thread, 0));
    assert(!pthread_join(right_thread, 0));
    for (index = 0; index < EXTRA_WORKERS; ++index)
        assert(!pthread_join(extra_threads[index], 0));
    frame_pacer_thread_cpu_quota_destroy(&quota);
    assert(access(root, F_OK) != 0);
    return 0;
unavailable:
    frame_pacer_thread_cpu_quota_publish(&quota, false, 0);
    atomic_store(&left.stop, true);
    atomic_store(&right.stop, true);
    for (index = 0; index < EXTRA_WORKERS; ++index)
        atomic_store(&extra[index].stop, true);
    (void)pthread_join(left_thread, 0);
    (void)pthread_join(right_thread, 0);
    for (index = 0; index < EXTRA_WORKERS; ++index)
        (void)pthread_join(extra_threads[index], 0);
    frame_pacer_thread_cpu_quota_destroy(&quota);
    if (last_quota_log[0])
        fputs(last_quota_log, stderr);
    return 77;
}
