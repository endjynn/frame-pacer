#define _GNU_SOURCE
#include "pacer_limit.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void set_path(struct frame_pacer_limit *limit)
{
    const char *config = getenv("XDG_CONFIG_HOME");
    const char *home;
    int written;

    if (config && *config) {
        written = snprintf(limit->path, sizeof(limit->path),
                           "%s/frame-pacer/frame-pacer.conf", config);
        if (written < 0 || (size_t)written >= sizeof(limit->path))
            limit->path[0] = '\0';
        return;
    }
    home = getenv("HOME");
    if (home && *home) {
        written = snprintf(limit->path, sizeof(limit->path),
                           "%s/.config/frame-pacer/frame-pacer.conf", home);
        if (written < 0 || (size_t)written >= sizeof(limit->path))
            limit->path[0] = '\0';
    }
}

static bool same_executable(const char *left, const char *right)
{
    size_t length = strlen(left);
    bool windows = length >= 4 && !strcasecmp(left + length - 4, ".exe");

    return windows ? !strcasecmp(left, right) : !strcmp(left, right);
}

static void add_executable_candidate(struct frame_pacer_limit *limit, const char *value,
                                     size_t length)
{
    const char *base = value;
    char basename[FRAME_PACER_EXECUTABLE_MAX];
    size_t basename_length;
    unsigned int index;

    if (!limit || !value || !length ||
        limit->executable_candidate_count >= FRAME_PACER_EXECUTABLE_CANDIDATES_MAX)
        return;
    for (index = 0; index < length; ++index)
        if (value[index] == '/' || value[index] == '\\') base = value + index + 1;
    if (base == value + length) return;
    basename_length = length - (size_t)(base - value);
    if (basename_length >= sizeof(basename)) return;
    memcpy(basename, base, basename_length);
    basename[basename_length] = '\0';
    for (index = 0; index < limit->executable_candidate_count; ++index)
        if (same_executable(limit->executable_candidates[index], basename)) return;
    (void)snprintf(limit->executable_candidates[limit->executable_candidate_count],
                   FRAME_PACER_EXECUTABLE_MAX, "%s", basename);
    ++limit->executable_candidate_count;
}

static void add_command_candidates(struct frame_pacer_limit *limit, const char *command,
                                   size_t bytes, bool include_command)
{
    size_t begin = 0;
    bool first = true;

    while (begin < bytes) {
        const char *argument = command + begin;
        const char *end = memchr(argument, '\0', bytes - begin);
        size_t length = end ? (size_t)(end - argument) : bytes - begin;

        if (length && ((include_command && first) ||
            (length >= 4 && !strncasecmp(argument + length - 4, ".exe", 4))))
            add_executable_candidate(limit, argument, length);
        if (!end) break;
        begin += length + 1;
        first = false;
    }
}

static ssize_t read_command(pid_t process_id, char command[FRAME_PACER_EXECUTABLE_MAX])
{
    char path[64];
    ssize_t bytes;
    char extra;
    int fd;
    int written;

    written = snprintf(path, sizeof(path), "/proc/%ld/cmdline",
                       (long)process_id);
    if (written < 0 || (size_t)written >= sizeof(path)) return -1;
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    do {
        bytes = read(fd, command, FRAME_PACER_EXECUTABLE_MAX);
    } while (bytes < 0 && errno == EINTR);
    if (bytes == FRAME_PACER_EXECUTABLE_MAX) {
        ssize_t remaining;

        do {
            remaining = read(fd, &extra, 1);
        } while (remaining < 0 && errno == EINTR);
        if (remaining != 0) bytes = -1;
    }
    (void)close(fd);
    return bytes;
}

static pid_t parent_process_id(pid_t process_id)
{
    char path[64], text[512], *end, *cursor;
    ssize_t bytes;
    long parent;
    int fd;
    int written;

    written = snprintf(path, sizeof(path), "/proc/%ld/stat", (long)process_id);
    if (written < 0 || (size_t)written >= sizeof(path)) return 0;
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return 0;
    bytes = read(fd, text, sizeof(text) - 1);
    (void)close(fd);
    if (bytes <= 0) return 0;
    text[bytes] = '\0';
    end = strrchr(text, ')');
    if (!end) return 0;
    cursor = end + 1;
    while (*cursor && isspace((unsigned char)*cursor)) ++cursor;
    if (!*cursor) return 0;
    ++cursor;
    while (*cursor && isspace((unsigned char)*cursor)) ++cursor;
    parent = strtol(cursor, &end, 10);
    if (cursor == end || parent < 2 || parent > INT_MAX) return 0;
    return (pid_t)parent;
}

static bool process_owned_by_user(pid_t process_id)
{
    char path[64];
    struct stat status;
    int written = snprintf(path, sizeof(path), "/proc/%ld", (long)process_id);

    return written >= 0 && (size_t)written < sizeof(path) &&
           !stat(path, &status) && status.st_uid == getuid();
}

static void set_executable_candidates(struct frame_pacer_limit *limit)
{
    char command[FRAME_PACER_EXECUTABLE_MAX];
    pid_t process_id = getpid();
    unsigned int depth;
    ssize_t bytes;

    bytes = read_command(process_id, command);
    if (bytes > 0) add_command_candidates(limit, command, (size_t)bytes, true);
    for (depth = 0; depth < 8; ++depth) {
        process_id = parent_process_id(process_id);
        if (!process_id || !process_owned_by_user(process_id)) break;
        bytes = read_command(process_id, command);
        if (bytes > 0) add_command_candidates(limit, command, (size_t)bytes, false);
    }
    if (limit->executable_candidate_count)
        (void)snprintf(limit->executable, sizeof(limit->executable), "%s",
                       limit->executable_candidates[0]);
}

void frame_pacer_limit_init(struct frame_pacer_limit *limit)
{
    if (!limit) return;
    memset(limit, 0, sizeof(*limit));
    if (pthread_mutex_init(&limit->mutex, 0)) return;
    limit->fps = FRAME_PACER_DEFAULT_FPS;
    limit->hud_enabled = true;
    set_path(limit);
    set_executable_candidates(limit);
    limit->initialized = true;
}

void frame_pacer_limit_destroy(struct frame_pacer_limit *limit)
{
    if (limit && limit->initialized) {
        limit->initialized = false;
        (void)pthread_mutex_destroy(&limit->mutex);
    }
}

const char *frame_pacer_limit_executable(const struct frame_pacer_limit *limit)
{
    return limit && limit->initialized ? limit->executable : "";
}

static struct frame_pacer_limit_stamp stamp_for(const struct stat *status)
{
    return (struct frame_pacer_limit_stamp){
        .present = true,
        .device = status->st_dev,
        .inode = status->st_ino,
        .mtime_seconds = status->st_mtim.tv_sec,
        .mtime_nanoseconds = status->st_mtim.tv_nsec,
        .size = status->st_size,
    };
}

static bool same_stamp(const struct frame_pacer_limit_stamp *left,
                       const struct frame_pacer_limit_stamp *right)
{
    return left->present == right->present && (!left->present ||
        (left->device == right->device && left->inode == right->inode &&
         left->mtime_seconds == right->mtime_seconds &&
         left->mtime_nanoseconds == right->mtime_nanoseconds && left->size == right->size));
}

static void trim(const char **begin, const char **end)
{
    while (*begin < *end && isspace((unsigned char)**begin)) ++*begin;
    while (*end > *begin && isspace((unsigned char)(*end)[-1])) --*end;
}

static bool parse_fps(const char *begin, const char *end, uint32_t *fps)
{
    uint64_t value = 0;

    if (begin == end || !isdigit((unsigned char)*begin)) return false;
    while (begin < end && isdigit((unsigned char)*begin)) {
        value = value * 10 + (unsigned)(*begin++ - '0');
        if (value > FRAME_PACER_MAX_FPS) return false;
    }
    if (begin != end || value < FRAME_PACER_MIN_FPS) return false;
    *fps = (uint32_t)value;
    return true;
}

static bool parse_thread_cpu_quota(const char *begin, const char *end,
                                   bool *enabled, uint32_t *quota)
{
    uint64_t value = 0;

    if ((size_t)(end - begin) == 3 && !memcmp(begin, "off", 3)) {
        *enabled = false;
        *quota = 0;
        return true;
    }
    if (begin == end || end[-1] != '%' || !isdigit((unsigned char)*begin)) return false;
    while (begin < end - 1 && isdigit((unsigned char)*begin)) {
        value = value * 10 + (unsigned)(*begin++ - '0');
        if (value > 100) return false;
    }
    if (begin != end - 1 || value < 1) return false;
    *enabled = true;
    *quota = (uint32_t)value;
    return true;
}

static bool parse_hud_enabled(const char *begin, const char *end, bool *enabled)
{
    if ((size_t)(end - begin) == 2 && !memcmp(begin, "on", 2)) {
        *enabled = true;
        return true;
    }
    if ((size_t)(end - begin) == 3 && !memcmp(begin, "off", 3)) {
        *enabled = false;
        return true;
    }
    return false;
}

static int executable_match_priority(const struct frame_pacer_limit *limit,
                                     const char *executable)
{
    unsigned int index;

    for (index = 0; limit && index < limit->executable_candidate_count; ++index)
        if (same_executable(executable, limit->executable_candidates[index])) return (int)index;
    return -1;
}

static bool finish_rule(bool in_rule, bool has_executable, bool has_fps)
{
    return !in_rule || (has_executable && has_fps);
}

static bool apply_rule(bool in_rule, bool has_executable, bool has_fps,
                       int rule_priority, uint32_t rule_fps, bool rule_quota_enabled,
                       uint32_t rule_quota, int *matched_priority, uint32_t *fps,
                       bool *quota_enabled, uint32_t *quota)
{
    if (!finish_rule(in_rule, has_executable, has_fps)) return false;
    if (rule_priority < 0) return true;
    if (*matched_priority == rule_priority) return false;
    if (*matched_priority < 0 || rule_priority < *matched_priority) {
        *fps = rule_fps;
        *quota_enabled = rule_quota_enabled;
        *quota = rule_quota;
        *matched_priority = rule_priority;
    }
    return true;
}

static bool parse_limit(const char *text, const struct frame_pacer_limit *limit, uint32_t *fps,
                        bool *quota_enabled, uint32_t *quota, bool *hud_enabled)
{
    const char *line = text;
    bool has_global = false, in_rule = false, has_executable = false, has_fps = false;
    bool has_hud = false, has_quota = false, rule_quota_enabled = false;
    int rule_priority = -1, matched_priority = -1;
    uint32_t global = 0, rule_fps = 0, rule_quota = 0;

    while (*line) {
        const char *end = strchr(line, '\n');
        const char *begin = line;
        const char *line_end = end ? end : line + strlen(line);
        const char *equals;

        trim(&begin, &line_end);
        if (begin == line_end || *begin == '#') goto next;
        if (*begin == '[') {
            if (line_end - begin < 3 || line_end[-1] != ']' ||
                !apply_rule(in_rule, has_executable, has_fps, rule_priority, rule_fps,
                            rule_quota_enabled, rule_quota, &matched_priority, fps,
                            quota_enabled, quota)) return false;
            ++begin;
            --line_end;
            trim(&begin, &line_end);
            if (begin == line_end) return false;
            in_rule = true;
            has_executable = has_fps = has_quota = rule_quota_enabled = false;
            rule_quota = 0;
            rule_priority = -1;
            goto next;
        }
        equals = memchr(begin, '=', (size_t)(line_end - begin));
        if (!equals) return false;
        {
            const char *key_begin = begin, *key_end = equals;
            const char *value_begin = equals + 1, *value_end = line_end;

            trim(&key_begin, &key_end);
            trim(&value_begin, &value_end);
            if ((!in_rule && key_end - key_begin ==
                              (ptrdiff_t)strlen("global_fps_limit") &&
                 !memcmp(key_begin, "global_fps_limit", strlen("global_fps_limit"))) ||
                (in_rule && key_end - key_begin == (ptrdiff_t)strlen("fps_limit") &&
                 !memcmp(key_begin, "fps_limit", strlen("fps_limit")))) {
                uint32_t value;

                if (!parse_fps(value_begin, value_end, &value)) return false;
                if (in_rule) {
                    if (has_fps) return false;
                    has_fps = true;
                    rule_fps = value;
                } else {
                    if (has_global) return false;
                    global = value;
                    has_global = true;
                }
            } else if (!in_rule && key_end - key_begin == (ptrdiff_t)strlen("hud") &&
                       !memcmp(key_begin, "hud", strlen("hud"))) {
                if (has_hud || !parse_hud_enabled(value_begin, value_end, hud_enabled))
                    return false;
                has_hud = true;
            } else if (in_rule && key_end - key_begin == (ptrdiff_t)strlen("executable") &&
                       !memcmp(key_begin, "executable", strlen("executable"))) {
                char value[FRAME_PACER_EXECUTABLE_MAX];
                size_t length;

                if (has_executable || value_end - value_begin < 3 ||
                    *value_begin != '"' || value_end[-1] != '"') return false;
                ++value_begin;
                --value_end;
                length = (size_t)(value_end - value_begin);
                if (!length || length >= sizeof(value) ||
                    memchr(value_begin, '"', length) ||
                    memchr(value_begin, '/', length) ||
                    memchr(value_begin, '\\', length))
                    return false;
                memcpy(value, value_begin, length);
                value[length] = '\0';
                has_executable = true;
                rule_priority = executable_match_priority(limit, value);
            } else if (in_rule && key_end - key_begin == (ptrdiff_t)strlen("thread_cpu_limit") &&
                       !memcmp(key_begin, "thread_cpu_limit", strlen("thread_cpu_limit"))) {
                if (has_quota || !parse_thread_cpu_quota(value_begin, value_end,
                                                          &rule_quota_enabled, &rule_quota))
                    return false;
                has_quota = true;
            } else return false;
        }
next:
        line = end ? end + 1 : line_end;
    }
    if (!has_global || !apply_rule(in_rule, has_executable, has_fps, rule_priority, rule_fps,
                                   rule_quota_enabled, rule_quota, &matched_priority, fps,
                                   quota_enabled, quota)) return false;
    if (matched_priority < 0) {
        *fps = global;
        *quota_enabled = false;
        *quota = 0;
    }
    return true;
}

static uint32_t read_limit(const struct frame_pacer_limit *limit,
                           const struct stat *expected, bool *quota_enabled,
                           uint32_t *quota, bool *hud_enabled)
{
    char text[4097];
    struct stat status;
    ssize_t bytes;
    int fd;
    uint32_t fps = FRAME_PACER_DEFAULT_FPS;

    if (!limit->path[0] || !expected || expected->st_uid != getuid() ||
        !S_ISREG(expected->st_mode) || expected->st_nlink != 1 ||
        (expected->st_mode & (S_IRWXG | S_IRWXO)) ||
        expected->st_size < 1 || expected->st_size >= (off_t)sizeof(text))
        return FRAME_PACER_DEFAULT_FPS;
    fd = open(limit->path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return FRAME_PACER_DEFAULT_FPS;
    if (fstat(fd, &status) || status.st_uid != expected->st_uid ||
        status.st_dev != expected->st_dev || status.st_ino != expected->st_ino ||
        !S_ISREG(status.st_mode) || status.st_nlink != 1 ||
        (status.st_mode & (S_IRWXG | S_IRWXO)) ||
        status.st_size != expected->st_size) {
        (void)close(fd);
        return FRAME_PACER_DEFAULT_FPS;
    }
    do {
        bytes = read(fd, text, sizeof(text) - 1);
    } while (bytes < 0 && errno == EINTR);
    if (bytes < 0 || bytes != expected->st_size || fstat(fd, &status) ||
        status.st_uid != expected->st_uid || status.st_dev != expected->st_dev ||
        status.st_ino != expected->st_ino || status.st_nlink != 1 ||
        status.st_mode != expected->st_mode ||
        status.st_mtim.tv_sec != expected->st_mtim.tv_sec ||
        status.st_mtim.tv_nsec != expected->st_mtim.tv_nsec ||
        status.st_size != expected->st_size) {
        (void)close(fd);
        return FRAME_PACER_DEFAULT_FPS;
    }
    if (close(fd)) return FRAME_PACER_DEFAULT_FPS;
    text[bytes] = '\0';
    return parse_limit(text, limit, &fps, quota_enabled, quota, hud_enabled) ?
        fps : FRAME_PACER_DEFAULT_FPS;
}

uint32_t frame_pacer_limit_poll(struct frame_pacer_limit *limit, uint64_t now_ns, bool *changed)
{
    struct stat status;
    struct frame_pacer_limit_stamp current = {0};
    uint32_t next;
    bool quota_enabled = false;
    bool hud_enabled = true;
    uint32_t quota = 0;

    if (changed) *changed = false;
    if (!limit || !limit->initialized) return FRAME_PACER_DEFAULT_FPS;
    (void)pthread_mutex_lock(&limit->mutex);
    if (limit->last_check_ns && now_ns >= limit->last_check_ns &&
        now_ns - limit->last_check_ns < FRAME_PACER_CONFIG_POLL_NS) {
        next = limit->fps;
        (void)pthread_mutex_unlock(&limit->mutex);
        return next;
    }
    limit->last_check_ns = now_ns;
    if (limit->path[0] && !lstat(limit->path, &status) &&
        status.st_uid == getuid() && S_ISREG(status.st_mode) &&
        status.st_nlink == 1 &&
        !(status.st_mode & (S_IRWXG | S_IRWXO)))
        current = stamp_for(&status);
    if (!same_stamp(&limit->stamp, &current)) {
        next = current.present ? read_limit(limit, &status, &quota_enabled, &quota, &hud_enabled) :
                                 FRAME_PACER_DEFAULT_FPS;
        if (next != limit->fps && changed) *changed = true;
        limit->fps = next;
        limit->thread_cpu_quota_enabled = quota_enabled;
        limit->thread_cpu_quota = quota;
        limit->hud_enabled = hud_enabled;
        limit->stamp = current;
    }
    next = limit->fps;
    (void)pthread_mutex_unlock(&limit->mutex);
    return next;
}

uint32_t frame_pacer_limit_thread_cpu_quota(struct frame_pacer_limit *limit,
                                            bool *enabled)
{
    uint32_t quota = 0;

    if (enabled) *enabled = false;
    if (!limit || !limit->initialized) return 0;
    (void)pthread_mutex_lock(&limit->mutex);
    quota = limit->thread_cpu_quota;
    if (enabled) *enabled = limit->thread_cpu_quota_enabled;
    (void)pthread_mutex_unlock(&limit->mutex);
    return quota;
}

bool frame_pacer_limit_hud_enabled(struct frame_pacer_limit *limit)
{
    bool enabled = true;

    if (!limit || !limit->initialized) return true;
    (void)pthread_mutex_lock(&limit->mutex);
    enabled = limit->hud_enabled;
    (void)pthread_mutex_unlock(&limit->mutex);
    return enabled;
}
