#ifndef FRAME_PACER_THREAD_CPU_QUOTA_H
#define FRAME_PACER_THREAD_CPU_QUOTA_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#define FRAME_PACER_THREAD_CPU_QUOTA_TIDS_MAX 1024U

/* The worker owns every cgroup operation.  Publishing is safe in hot paths. */
struct frame_pacer_thread_cpu_quota {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    pthread_t worker;
    char scope[1200];
    char cgroup[1200];
    /* The equivalent cgroup-v2 path as reported by /proc/self/cgroup. */
    char cgroup_proc[1200];
    char external_state[1200];
    uint32_t tids[FRAME_PACER_THREAD_CPU_QUOTA_TIDS_MAX];
    uint32_t observed_threads;
    uint32_t requested;
    bool requested_enabled;
    bool confirmed;
    bool worker_started;
    bool stop;
    bool owner;
    bool external;
    bool initialized;
    pid_t external_pid;
    int last_error;
    char failure_stage[48];
    void (*log)(const char *);
};

void frame_pacer_thread_cpu_quota_init(struct frame_pacer_thread_cpu_quota *);
void frame_pacer_thread_cpu_quota_destroy(
    struct frame_pacer_thread_cpu_quota *);
void frame_pacer_thread_cpu_quota_publish(struct frame_pacer_thread_cpu_quota *,
                                          bool enabled, uint32_t percent);
void frame_pacer_thread_cpu_quota_set_logger(
    struct frame_pacer_thread_cpu_quota *, void (*log)(const char *));
bool frame_pacer_thread_cpu_quota_confirmed(
    struct frame_pacer_thread_cpu_quota *, uint32_t *percent);

#ifdef FRAME_PACER_TEST
bool frame_pacer_thread_cpu_quota_test_write_text(const char *, const char *);
bool frame_pacer_thread_cpu_quota_test_parse_confirmation(const char *,
                                                          uint32_t);
bool frame_pacer_thread_cpu_quota_test_write_external_state(const char *, bool,
                                                            uint32_t);
bool frame_pacer_thread_cpu_quota_test_helper_path(const char *, char *,
                                                   size_t);
bool frame_pacer_thread_cpu_quota_test_runtime_helper_path(char *, size_t);
bool frame_pacer_thread_cpu_quota_test_parse_host_pid_line(const char *);
bool frame_pacer_thread_cpu_quota_test_valid_boot_id(const char *);
#endif

#endif
