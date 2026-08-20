#define _GNU_SOURCE
/* Opt-in black-box test of the production controller.  Never add to make check. */
#include "thread_cpu_quota.h"

#include <assert.h>
#include <dirent.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

struct worker { atomic_uint tid; atomic_bool stop; };

#define EXTRA_WORKERS 128U

static void *run(void *data)
{
    struct worker *worker = data;
    volatile uint64_t value = 1;
    atomic_store(&worker->tid, (unsigned int)syscall(SYS_gettid));
    while (!atomic_load(&worker->stop)) value = value * 1103515245U + 12345U;
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

static bool wait_confirmed(struct frame_pacer_thread_cpu_quota *quota, uint32_t percent)
{
    unsigned int attempt;
    for (attempt = 0; attempt < 80; ++attempt) {
        uint32_t actual = 0;
        if (frame_pacer_thread_cpu_quota_confirmed(quota, &actual) && actual == percent)
            return true;
        { struct timespec pause = {.tv_nsec = 50000000}; (void)nanosleep(&pause, 0); }
    }
    return false;
}

static bool tid_in_own_child(uint32_t tid, const char *root, uint32_t percent)
{
    char proc[128], path[1400], line[256], expected[1400], quota[32];
    FILE *file;
    int result;
    result = snprintf(proc, sizeof(proc), "/proc/self/task/%u/cgroup", tid);
    if (result < 0 || (size_t)result >= sizeof(proc) || !(file = fopen(proc, "re")) ||
        !fgets(line, sizeof(line), file)) { if (file) fclose(file); return false; }
    (void)fclose(file);
    line[strcspn(line, "\r\n")] = '\0';
    result = snprintf(expected, sizeof(expected), "0::%s/t-%u", root + strlen("/sys/fs/cgroup"), tid);
    if (result < 0 || (size_t)result >= sizeof(expected) || strcmp(line, expected)) return false;
    result = snprintf(path, sizeof(path), "%s/t-%u/cpu.max", root, tid);
    if (result < 0 || (size_t)result >= sizeof(path) || !(file = fopen(path, "re")) ||
        !fgets(line, sizeof(line), file)) { if (file) fclose(file); return false; }
    (void)fclose(file);
    result = snprintf(quota, sizeof(quota), "%u 100000", percent * 1000U);
    line[strcspn(line, "\r\n")] = '\0';
    return result > 0 && (size_t)result < sizeof(quota) && !strcmp(line, quota);
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

    if (!getenv("FRAME_PACER_THREAD_CPU_QUOTA_INTEGRATION")) return 77;
    frame_pacer_thread_cpu_quota_init(&quota);
    assert(!pthread_create(&left_thread, 0, run, &left));
    assert(!pthread_create(&right_thread, 0, run, &right));
    for (index = 0; index < EXTRA_WORKERS; ++index)
        assert(!pthread_create(&extra_threads[index], 0, idle, &extra[index]));
    while (!atomic_load(&left.tid) || !atomic_load(&right.tid)) {
        struct timespec pause = {.tv_nsec = 1000000}; (void)nanosleep(&pause, 0);
    }
    frame_pacer_thread_cpu_quota_publish(&quota, true, 50);
    if (!wait_confirmed(&quota, 50)) goto unavailable;
    assert(tid_in_own_child(atomic_load(&left.tid), quota.cgroup, 50));
    assert(tid_in_own_child(atomic_load(&right.tid), quota.cgroup, 50));
    assert(snprintf(root, sizeof(root), "%s", quota.cgroup) > 0);
    frame_pacer_thread_cpu_quota_publish(&quota, true, 75);
    assert(wait_confirmed(&quota, 75));
    assert(!strcmp(root, quota.cgroup)); /* live update, never a replacement subtree */
    assert(tid_in_own_child(atomic_load(&left.tid), root, 75));
    assert(tid_in_own_child(atomic_load(&right.tid), root, 75));
    frame_pacer_thread_cpu_quota_publish(&quota, false, 0);
    atomic_store(&left.stop, true); atomic_store(&right.stop, true);
    for (index = 0; index < EXTRA_WORKERS; ++index) atomic_store(&extra[index].stop, true);
    assert(!pthread_join(left_thread, 0)); assert(!pthread_join(right_thread, 0));
    for (index = 0; index < EXTRA_WORKERS; ++index) assert(!pthread_join(extra_threads[index], 0));
    frame_pacer_thread_cpu_quota_destroy(&quota);
    assert(access(root, F_OK) != 0);
    return 0;
unavailable:
    frame_pacer_thread_cpu_quota_publish(&quota, false, 0);
    atomic_store(&left.stop, true); atomic_store(&right.stop, true);
    for (index = 0; index < EXTRA_WORKERS; ++index) atomic_store(&extra[index].stop, true);
    (void)pthread_join(left_thread, 0); (void)pthread_join(right_thread, 0);
    for (index = 0; index < EXTRA_WORKERS; ++index) (void)pthread_join(extra_threads[index], 0);
    frame_pacer_thread_cpu_quota_destroy(&quota);
    return 77;
}
