#ifndef FRAME_PACER_LIMIT_H
#define FRAME_PACER_LIMIT_H

#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#define FRAME_PACER_FPS_LIMIT_OFF 0U
#define FRAME_PACER_MIN_FPS 1U
#define FRAME_PACER_MAX_FPS 999U
#define FRAME_PACER_CONFIG_POLL_NS UINT64_C(1000000000)
#define FRAME_PACER_CONFIG_MAX_BYTES 1048576U
#define FRAME_PACER_EXECUTABLE_MAX 512U
#define FRAME_PACER_EXECUTABLE_CANDIDATES_MAX 16U

#if defined(__GNUC__)
#define FRAME_PACER_LIMIT_INTERNAL __attribute__((visibility("hidden")))
#else
#define FRAME_PACER_LIMIT_INTERNAL
#endif

enum frame_pacer_config_status {
    FRAME_PACER_CONFIG_VALID,
    FRAME_PACER_CONFIG_MISSING,
    FRAME_PACER_CONFIG_INSECURE,
    FRAME_PACER_CONFIG_UNREADABLE,
    FRAME_PACER_CONFIG_MALFORMED
};

enum frame_pacer_config_reason {
    FRAME_PACER_REASON_NONE,
    FRAME_PACER_REASON_EXPLICITLY_OFF,
    FRAME_PACER_REASON_NO_PER_GAME_RULES,
    FRAME_PACER_REASON_NO_EXECUTABLE_MATCH,
    FRAME_PACER_REASON_CONFIG_PATH_UNAVAILABLE,
    FRAME_PACER_REASON_MISSING_FILE,
    FRAME_PACER_REASON_NOT_REGULAR_FILE,
    FRAME_PACER_REASON_SYMBOLIC_LINK,
    FRAME_PACER_REASON_WRONG_OWNER,
    FRAME_PACER_REASON_MULTIPLE_HARD_LINKS,
    FRAME_PACER_REASON_INSECURE_PERMISSIONS,
    FRAME_PACER_REASON_EMPTY_FILE,
    FRAME_PACER_REASON_FILE_TOO_LARGE,
    FRAME_PACER_REASON_METADATA_FAILED,
    FRAME_PACER_REASON_OPEN_FAILED,
    FRAME_PACER_REASON_READ_FAILED,
    FRAME_PACER_REASON_CLOSE_FAILED,
    FRAME_PACER_REASON_CHANGED_DURING_READ,
    FRAME_PACER_REASON_OUT_OF_MEMORY,
    FRAME_PACER_REASON_INVALID_SECTION,
    FRAME_PACER_REASON_MISSING_EQUALS,
    FRAME_PACER_REASON_UNKNOWN_KEY,
    FRAME_PACER_REASON_DUPLICATE_KEY,
    FRAME_PACER_REASON_INVALID_VALUE,
    FRAME_PACER_REASON_INVALID_EXECUTABLE,
    FRAME_PACER_REASON_INVALID_BYTE,
    FRAME_PACER_REASON_INCOMPLETE_RULE,
    FRAME_PACER_REASON_DUPLICATE_MATCHING_RULE
};

enum frame_pacer_value_source {
    FRAME_PACER_SOURCE_DEFAULT,
    FRAME_PACER_SOURCE_GLOBAL,
    FRAME_PACER_SOURCE_PER_GAME
};

struct frame_pacer_effective_config {
    uint64_t revision;
    enum frame_pacer_config_status status;
    enum frame_pacer_config_reason reason;
    size_t error_line;
    uint32_t fps_limit;
    enum frame_pacer_value_source fps_source;
    bool hud_enabled;
    enum frame_pacer_value_source hud_source;
    bool thread_cpu_enabled;
    uint32_t thread_cpu_percent;
    enum frame_pacer_value_source thread_cpu_source;
    char renderer[FRAME_PACER_EXECUTABLE_MAX];
    char matched_section[FRAME_PACER_EXECUTABLE_MAX];
    char matched_executable[FRAME_PACER_EXECUTABLE_MAX];
};

#ifdef FRAME_PACER_TEST
enum frame_pacer_limit_test_failure {
    FRAME_PACER_TEST_FAILURE_NONE,
    FRAME_PACER_TEST_FAILURE_METADATA,
    FRAME_PACER_TEST_FAILURE_WRONG_OWNER,
    FRAME_PACER_TEST_FAILURE_OPEN,
    FRAME_PACER_TEST_FAILURE_FSTAT,
    FRAME_PACER_TEST_FAILURE_FINAL_FSTAT,
    FRAME_PACER_TEST_FAILURE_READ,
    FRAME_PACER_TEST_FAILURE_CHANGED,
    FRAME_PACER_TEST_FAILURE_CLOSE,
    FRAME_PACER_TEST_FAILURE_ALLOC
};
#endif

struct frame_pacer_limit {
    pthread_mutex_t mutex;
    char path[1200];
    _Atomic uint64_t last_check_ns;
    _Atomic uint32_t fps;
    _Atomic uint32_t thread_cpu_quota;
    _Atomic bool hud_enabled;
    _Atomic uint64_t revision;
    struct frame_pacer_effective_config effective;
    char executable_candidates[FRAME_PACER_EXECUTABLE_CANDIDATES_MAX]
                              [FRAME_PACER_EXECUTABLE_MAX];
    unsigned int executable_candidate_count;
    char executable[FRAME_PACER_EXECUTABLE_MAX];
    char *config_buffer;
    size_t config_buffer_capacity;
#ifdef FRAME_PACER_TEST
    enum frame_pacer_limit_test_failure test_failure;
#endif
    bool reporting_enabled;
    bool initialized;
};

void frame_pacer_limit_init(struct frame_pacer_limit *);
void frame_pacer_limit_destroy(struct frame_pacer_limit *);
uint32_t frame_pacer_limit_poll(struct frame_pacer_limit *, uint64_t now_ns);
FRAME_PACER_LIMIT_INTERNAL uint64_t frame_pacer_limit_revision(
    const struct frame_pacer_limit *);
FRAME_PACER_LIMIT_INTERNAL bool frame_pacer_limit_snapshot(
    struct frame_pacer_limit *, struct frame_pacer_effective_config *);
FRAME_PACER_LIMIT_INTERNAL void frame_pacer_limit_set_reporting_enabled(
    struct frame_pacer_limit *, bool enabled);
uint32_t frame_pacer_limit_thread_cpu_quota(struct frame_pacer_limit *, bool *enabled);
bool frame_pacer_limit_hud_enabled(struct frame_pacer_limit *);
const char *frame_pacer_limit_executable(const struct frame_pacer_limit *);
#ifdef FRAME_PACER_TEST
void frame_pacer_limit_test_fail_at(struct frame_pacer_limit *,
                                    enum frame_pacer_limit_test_failure);
#endif

#endif
