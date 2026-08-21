#ifndef FRAME_PACER_LIMIT_H
#define FRAME_PACER_LIMIT_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#define FRAME_PACER_DEFAULT_FPS 70U
#define FRAME_PACER_MIN_FPS 1U
#define FRAME_PACER_MAX_FPS 999U
#define FRAME_PACER_CONFIG_POLL_NS UINT64_C(1000000000)
#define FRAME_PACER_EXECUTABLE_MAX 512U
#define FRAME_PACER_EXECUTABLE_CANDIDATES_MAX 16U

struct frame_pacer_limit_stamp {
    bool present;
    dev_t device;
    ino_t inode;
    int64_t mtime_seconds;
    long mtime_nanoseconds;
    off_t size;
};

struct frame_pacer_limit {
    pthread_mutex_t mutex;
    char path[1200];
    uint64_t last_check_ns;
    uint32_t fps;
    uint32_t thread_cpu_quota;
    bool thread_cpu_quota_enabled;
    bool hud_enabled;
    /* The renderer is first; bounded same-user ancestors follow. */
    char executable_candidates[FRAME_PACER_EXECUTABLE_CANDIDATES_MAX]
                              [FRAME_PACER_EXECUTABLE_MAX];
    unsigned int executable_candidate_count;
    char executable[FRAME_PACER_EXECUTABLE_MAX];
    bool initialized;
    struct frame_pacer_limit_stamp stamp;
};

void frame_pacer_limit_init(struct frame_pacer_limit *);
void frame_pacer_limit_destroy(struct frame_pacer_limit *);
/* Returns the current cap.  `changed` reports a valid, effective cap change. */
uint32_t frame_pacer_limit_poll(struct frame_pacer_limit *, uint64_t now_ns, bool *changed);
/* A selected executable rule may request an independent per-thread ceiling. */
uint32_t frame_pacer_limit_thread_cpu_quota(struct frame_pacer_limit *, bool *enabled);
/* The top-level HUD switch; enabled by default and refreshed by poll(). */
bool frame_pacer_limit_hud_enabled(struct frame_pacer_limit *);
/* The renderer executable basename captured at initialization. */
const char *frame_pacer_limit_executable(const struct frame_pacer_limit *);

#endif
