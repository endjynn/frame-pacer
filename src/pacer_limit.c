#define _GNU_SOURCE
#include "pacer_limit.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct parsed_rule {
    char section[FRAME_PACER_EXECUTABLE_MAX];
    char executable[FRAME_PACER_EXECUTABLE_MAX];
    size_t header_line;
    uint32_t fps;
    uint32_t quota;
    int priority;
    bool has_executable;
    bool has_fps;
    bool has_quota;
    bool quota_enabled;
};

struct parse_state {
    struct frame_pacer_effective_config result;
    struct parsed_rule rule;
    char selected_section[FRAME_PACER_EXECUTABLE_MAX];
    uint32_t selected_fps;
    uint32_t selected_quota;
    unsigned int rule_count;
    int selected_priority;
    bool selected_quota_enabled;
    bool selected_has_quota;
    bool selected_fps_off;
    bool global_present;
    bool global_off;
    bool hud_present;
    bool in_rule;
};

static void set_failure(struct frame_pacer_effective_config *result,
                        enum frame_pacer_config_status status,
                        enum frame_pacer_config_reason reason, size_t line)
{
    result->status = status;
    result->reason = reason;
    result->error_line = line;
}

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

static void add_executable_candidate(struct frame_pacer_limit *limit,
                                     const char *value, size_t length)
{
    const char *base = value;
    char basename[FRAME_PACER_EXECUTABLE_MAX];
    size_t basename_length;
    unsigned int index;

    if (!limit || !value || !length ||
        limit->executable_candidate_count >=
            FRAME_PACER_EXECUTABLE_CANDIDATES_MAX)
        return;
    for (index = 0; index < length; ++index)
        if (value[index] == '/' || value[index] == '\\')
            base = value + index + 1;
    if (base == value + length)
        return;
    basename_length = length - (size_t)(base - value);
    if (basename_length >= sizeof(basename))
        return;
    memcpy(basename, base, basename_length);
    basename[basename_length] = '\0';
    for (index = 0; index < limit->executable_candidate_count; ++index)
        if (same_executable(limit->executable_candidates[index], basename))
            return;
    (void)snprintf(
        limit->executable_candidates[limit->executable_candidate_count],
        FRAME_PACER_EXECUTABLE_MAX, "%s", basename);
    ++limit->executable_candidate_count;
}

static void add_command_candidates(struct frame_pacer_limit *limit,
                                   const char *command, size_t bytes,
                                   bool include_command)
{
    size_t begin = 0;
    bool first = true;

    while (begin < bytes) {
        const char *argument = command + begin;
        const char *end = memchr(argument, '\0', bytes - begin);
        size_t length = end ? (size_t)(end - argument) : bytes - begin;

        if (length &&
            ((include_command && first) ||
             (length >= 4 && !strncasecmp(argument + length - 4, ".exe", 4))))
            add_executable_candidate(limit, argument, length);
        if (!end)
            break;
        begin += length + 1;
        first = false;
    }
}

static ssize_t read_command(pid_t process_id,
                            char command[FRAME_PACER_EXECUTABLE_MAX])
{
    char path[64];
    ssize_t bytes;
    char extra;
    int fd;
    int written;

    written =
        snprintf(path, sizeof(path), "/proc/%ld/cmdline", (long)process_id);
    if (written < 0 || (size_t)written >= sizeof(path))
        return -1;
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return -1;
    do {
        bytes = read(fd, command, FRAME_PACER_EXECUTABLE_MAX);
    } while (bytes < 0 && errno == EINTR);
    if (bytes == FRAME_PACER_EXECUTABLE_MAX) {
        ssize_t remaining;

        do {
            remaining = read(fd, &extra, 1);
        } while (remaining < 0 && errno == EINTR);
        if (remaining != 0)
            bytes = -1;
    }
    (void)close(fd);
    return bytes;
}

/* /proc maps fields use ASCII separators, regardless of the game's locale. */
static bool maps_space(unsigned char character)
{
    return character == ' ' || (character >= '\t' && character <= '\r');
}

static const char *next_maps_field(const char *cursor, const char *end)
{
    while (cursor < end && maps_space((unsigned char)*cursor))
        ++cursor;
    while (cursor < end && !maps_space((unsigned char)*cursor))
        ++cursor;
    return cursor;
}

static bool mapped_executable_path(const char *line, size_t length,
                                   const char **path, size_t *path_length)
{
    const char *cursor = line, *end = line + length;
    const char *offset_begin, *offset_end;

    cursor = next_maps_field(cursor, end); /* address */
    cursor = next_maps_field(cursor, end); /* permissions */
    while (cursor < end && maps_space((unsigned char)*cursor))
        ++cursor;
    offset_begin = cursor;
    offset_end = next_maps_field(cursor, end);
    if (offset_begin == offset_end)
        return false;
    for (cursor = offset_begin; cursor < offset_end; ++cursor)
        if (*cursor != '0')
            return false;
    cursor = next_maps_field(offset_end, end); /* device */
    cursor = next_maps_field(cursor, end);     /* inode */
    while (cursor < end && maps_space((unsigned char)*cursor))
        ++cursor;
    while (end > cursor && (end[-1] == '\n' || end[-1] == '\r'))
        --end;
    if (cursor == end || *cursor != '/' || (size_t)(end - cursor) < 4U ||
        strncasecmp(end - 4, ".exe", 4))
        return false;
    *path = cursor;
    *path_length = (size_t)(end - cursor);
    return true;
}

static void add_mapped_executable_candidate(struct frame_pacer_limit *limit)
{
    const char *maps_path = "/proc/self/maps";
    char *line = 0;
    size_t capacity = 0;
    FILE *maps;

#ifdef FRAME_PACER_TEST
    {
        const char *test_path = getenv("FRAME_PACER_TEST_PROC_MAPS");
        if (test_path && *test_path)
            maps_path = test_path;
    }
#endif
    maps = fopen(maps_path, "r");
    if (!maps)
        return;
    while (getline(&line, &capacity, maps) >= 0) {
        const char *path;
        size_t path_length;

        if (!mapped_executable_path(line, strlen(line), &path, &path_length))
            continue;
        add_executable_candidate(limit, path, path_length);
        break;
    }
    free(line);
    (void)fclose(maps);
}

static pid_t parent_process_id(pid_t process_id)
{
    char path[64], text[512], *end, *cursor;
    ssize_t bytes;
    long parent;
    int fd;
    int written;

    written = snprintf(path, sizeof(path), "/proc/%ld/stat", (long)process_id);
    if (written < 0 || (size_t)written >= sizeof(path))
        return 0;
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return 0;
    bytes = read(fd, text, sizeof(text) - 1);
    (void)close(fd);
    if (bytes <= 0)
        return 0;
    text[bytes] = '\0';
    end = strrchr(text, ')');
    if (!end)
        return 0;
    cursor = end + 1;
    while (*cursor && isspace((unsigned char)*cursor))
        ++cursor;
    if (!*cursor)
        return 0;
    ++cursor;
    while (*cursor && isspace((unsigned char)*cursor))
        ++cursor;
    parent = strtol(cursor, &end, 10);
    if (cursor == end || parent < 2 || parent > INT_MAX)
        return 0;
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

    /* Wine may clear or retain the launcher's command line after creating a
     * child game process.  Its mapped PE image remains authoritative. */
    add_mapped_executable_candidate(limit);
    bytes = read_command(process_id, command);
    if (bytes > 0)
        add_command_candidates(limit, command, (size_t)bytes, true);
    for (depth = 0; depth < 8; ++depth) {
        process_id = parent_process_id(process_id);
        if (!process_id || !process_owned_by_user(process_id))
            break;
        bytes = read_command(process_id, command);
        if (bytes > 0)
            add_command_candidates(limit, command, (size_t)bytes, false);
    }
    if (limit->executable_candidate_count)
        (void)snprintf(limit->executable, sizeof(limit->executable), "%s",
                       limit->executable_candidates[0]);
}

static void effective_defaults(const struct frame_pacer_limit *limit,
                               struct frame_pacer_effective_config *result)
{
    memset(result, 0, sizeof(*result));
    result->status = FRAME_PACER_CONFIG_UNREADABLE;
    result->reason = FRAME_PACER_REASON_CONFIG_PATH_UNAVAILABLE;
    result->fps_source = FRAME_PACER_SOURCE_DEFAULT;
    result->hud_enabled = true;
    result->hud_source = FRAME_PACER_SOURCE_DEFAULT;
    result->thread_cpu_source = FRAME_PACER_SOURCE_DEFAULT;
    if (limit->reporting_enabled)
        (void)snprintf(result->renderer, sizeof(result->renderer), "%s",
                       limit->executable);
}

void frame_pacer_limit_init(struct frame_pacer_limit *limit)
{
    if (!limit)
        return;
    memset(limit, 0, sizeof(*limit));
    atomic_init(&limit->last_check_ns, 0);
    atomic_init(&limit->fps, FRAME_PACER_FPS_LIMIT_OFF);
    atomic_init(&limit->thread_cpu_quota, 0);
    atomic_init(&limit->hud_enabled, true);
    atomic_init(&limit->revision, 0);
    if (pthread_mutex_init(&limit->mutex, 0))
        return;
    set_path(limit);
    set_executable_candidates(limit);
    effective_defaults(limit, &limit->effective);
    limit->initialized = true;
}

void frame_pacer_limit_destroy(struct frame_pacer_limit *limit)
{
    if (limit && limit->initialized) {
        limit->initialized = false;
        free(limit->config_buffer);
        limit->config_buffer = 0;
        limit->config_buffer_capacity = 0;
        (void)pthread_mutex_destroy(&limit->mutex);
    }
}

const char *frame_pacer_limit_executable(const struct frame_pacer_limit *limit)
{
    return limit && limit->initialized ? limit->executable : "";
}

static void trim(const char **begin, const char **end)
{
    while (*begin < *end && isspace((unsigned char)**begin))
        ++*begin;
    while (*end > *begin && isspace((unsigned char)(*end)[-1]))
        --*end;
}

static bool parse_fps(const char *begin, const char *end, uint32_t *fps)
{
    uint64_t value = 0;

    if (begin == end || !isdigit((unsigned char)*begin))
        return false;
    while (begin < end && isdigit((unsigned char)*begin)) {
        value = value * 10 + (unsigned)(*begin++ - '0');
        if (value > FRAME_PACER_MAX_FPS)
            return false;
    }
    if (begin != end || value < FRAME_PACER_MIN_FPS)
        return false;
    *fps = (uint32_t)value;
    return true;
}

static bool parse_fps_limit(const char *begin, const char *end, uint32_t *fps,
                            bool *explicit_off)
{
    if ((size_t)(end - begin) == 3 && !memcmp(begin, "off", 3)) {
        *fps = FRAME_PACER_FPS_LIMIT_OFF;
        *explicit_off = true;
        return true;
    }
    *explicit_off = false;
    return parse_fps(begin, end, fps);
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
    if (begin == end || end[-1] != '%' || !isdigit((unsigned char)*begin))
        return false;
    while (begin < end - 1 && isdigit((unsigned char)*begin)) {
        value = value * 10 + (unsigned)(*begin++ - '0');
        if (value > 100)
            return false;
    }
    if (begin != end - 1 || value < 1)
        return false;
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

    for (index = 0; index < limit->executable_candidate_count; ++index)
        if (same_executable(executable, limit->executable_candidates[index]))
            return (int)index;
    return -1;
}

static bool copy_value(char *destination, size_t capacity, const char *begin,
                       const char *end)
{
    size_t length = (size_t)(end - begin);

    if (!length || length >= capacity)
        return false;
    memcpy(destination, begin, length);
    destination[length] = '\0';
    return true;
}

static bool finish_rule(struct parse_state *state,
                        const struct frame_pacer_limit *limit)
{
    struct parsed_rule *rule = &state->rule;

    if (!state->in_rule)
        return true;
    if (!rule->has_executable || !rule->has_fps) {
        set_failure(&state->result, FRAME_PACER_CONFIG_MALFORMED,
                    FRAME_PACER_REASON_INCOMPLETE_RULE, rule->header_line);
        return false;
    }
    ++state->rule_count;
    if (rule->priority < 0)
        return true;
    if (state->selected_priority == rule->priority) {
        set_failure(&state->result, FRAME_PACER_CONFIG_MALFORMED,
                    FRAME_PACER_REASON_DUPLICATE_MATCHING_RULE,
                    rule->header_line);
        return false;
    }
    if (state->selected_priority < 0 ||
        rule->priority < state->selected_priority) {
        state->selected_priority = rule->priority;
        state->selected_fps = rule->fps;
        state->selected_fps_off = rule->fps == FRAME_PACER_FPS_LIMIT_OFF;
        state->selected_quota_enabled = rule->quota_enabled;
        state->selected_quota = rule->quota;
        state->selected_has_quota = rule->has_quota;
        if (limit->reporting_enabled)
            (void)snprintf(state->selected_section,
                           sizeof(state->selected_section), "%s",
                           rule->section);
    }
    return true;
}

static bool valid_bytes(const char *text, size_t length,
                        struct frame_pacer_effective_config *result)
{
    size_t index, line = 1;

    for (index = 0; index < length; ++index) {
        unsigned char byte = (unsigned char)text[index];

        if (byte == '\n') {
            ++line;
        } else if (byte != '\r' && byte != '\t' &&
                   (byte < 0x20 || byte > 0x7e)) {
            set_failure(result, FRAME_PACER_CONFIG_MALFORMED,
                        FRAME_PACER_REASON_INVALID_BYTE, line);
            return false;
        }
    }
    return true;
}

static bool parse_limit(const char *text, size_t length,
                        const struct frame_pacer_limit *limit,
                        struct frame_pacer_effective_config *result)
{
    struct parse_state state;
    size_t offset = 0, line_number = 1;
    uint32_t global_fps = FRAME_PACER_FPS_LIMIT_OFF;

    memset(&state, 0, sizeof(state));
    state.result = *result;
    state.result.status = FRAME_PACER_CONFIG_VALID;
    state.result.reason = FRAME_PACER_REASON_NONE;
    state.selected_priority = -1;
    if (!valid_bytes(text, length, &state.result))
        goto failure;

    while (offset < length) {
        const char *raw = text + offset;
        const char *newline = memchr(raw, '\n', length - offset);
        const char *begin = raw;
        const char *end = newline ? newline : text + length;
        const char *equals;

        trim(&begin, &end);
        if (begin == end || *begin == '#')
            goto next;
        if (*begin == '[') {
            const char *name_begin, *name_end;

            if (!finish_rule(&state, limit))
                goto failure;
            if ((size_t)(end - begin) < 3 || end[-1] != ']') {
                set_failure(&state.result, FRAME_PACER_CONFIG_MALFORMED,
                            FRAME_PACER_REASON_INVALID_SECTION, line_number);
                goto failure;
            }
            name_begin = begin + 1;
            name_end = end - 1;
            trim(&name_begin, &name_end);
            memset(&state.rule, 0, sizeof(state.rule));
            if (!copy_value(state.rule.section, sizeof(state.rule.section),
                            name_begin, name_end)) {
                set_failure(&state.result, FRAME_PACER_CONFIG_MALFORMED,
                            FRAME_PACER_REASON_INVALID_SECTION, line_number);
                goto failure;
            }
            state.rule.header_line = line_number;
            state.rule.priority = -1;
            state.in_rule = true;
            goto next;
        }
        equals = memchr(begin, '=', (size_t)(end - begin));
        if (!equals) {
            set_failure(&state.result, FRAME_PACER_CONFIG_MALFORMED,
                        FRAME_PACER_REASON_MISSING_EQUALS, line_number);
            goto failure;
        }
        {
            const char *key_begin = begin, *key_end = equals;
            const char *value_begin = equals + 1, *value_end = end;

            trim(&key_begin, &key_end);
            trim(&value_begin, &value_end);
            if (!state.in_rule &&
                (size_t)(key_end - key_begin) == strlen("global_fps_limit") &&
                !memcmp(key_begin, "global_fps_limit",
                        strlen("global_fps_limit"))) {
                bool explicit_off;

                if (state.global_present)
                    goto duplicate_key;
                if (!parse_fps_limit(value_begin, value_end, &global_fps,
                                     &explicit_off))
                    goto invalid_value;
                state.global_present = true;
                state.global_off = explicit_off;
            } else if (!state.in_rule &&
                       (size_t)(key_end - key_begin) == strlen("hud") &&
                       !memcmp(key_begin, "hud", strlen("hud"))) {
                if (state.hud_present)
                    goto duplicate_key;
                if (!parse_hud_enabled(value_begin, value_end,
                                       &state.result.hud_enabled))
                    goto invalid_value;
                state.hud_present = true;
                state.result.hud_source = FRAME_PACER_SOURCE_GLOBAL;
            } else if (state.in_rule &&
                       (size_t)(key_end - key_begin) == strlen("fps_limit") &&
                       !memcmp(key_begin, "fps_limit", strlen("fps_limit"))) {
                bool explicit_off;

                if (state.rule.has_fps)
                    goto duplicate_key;
                if (!parse_fps_limit(value_begin, value_end, &state.rule.fps,
                                     &explicit_off))
                    goto invalid_value;
                (void)explicit_off;
                state.rule.has_fps = true;
            } else if (state.in_rule &&
                       (size_t)(key_end - key_begin) == strlen("executable") &&
                       !memcmp(key_begin, "executable", strlen("executable"))) {
                size_t value_length;

                if (state.rule.has_executable)
                    goto duplicate_key;
                if ((size_t)(value_end - value_begin) < 3 ||
                    *value_begin != '"' || value_end[-1] != '"')
                    goto invalid_executable;
                ++value_begin;
                --value_end;
                value_length = (size_t)(value_end - value_begin);
                if (!value_length ||
                    value_length >= sizeof(state.rule.executable) ||
                    memchr(value_begin, '"', value_length) ||
                    memchr(value_begin, '/', value_length) ||
                    memchr(value_begin, '\\', value_length))
                    goto invalid_executable;
                memcpy(state.rule.executable, value_begin, value_length);
                state.rule.executable[value_length] = '\0';
                state.rule.has_executable = true;
                state.rule.priority =
                    executable_match_priority(limit, state.rule.executable);
            } else if (state.in_rule &&
                       (size_t)(key_end - key_begin) ==
                           strlen("thread_cpu_limit") &&
                       !memcmp(key_begin, "thread_cpu_limit",
                               strlen("thread_cpu_limit"))) {
                if (state.rule.has_quota)
                    goto duplicate_key;
                if (!parse_thread_cpu_quota(value_begin, value_end,
                                            &state.rule.quota_enabled,
                                            &state.rule.quota))
                    goto invalid_value;
                state.rule.has_quota = true;
            } else {
                set_failure(&state.result, FRAME_PACER_CONFIG_MALFORMED,
                            FRAME_PACER_REASON_UNKNOWN_KEY, line_number);
                goto failure;
            }
        }
    next:
        if (!newline)
            break;
        offset = (size_t)(newline - text) + 1;
        ++line_number;
        continue;
    duplicate_key:
        set_failure(&state.result, FRAME_PACER_CONFIG_MALFORMED,
                    FRAME_PACER_REASON_DUPLICATE_KEY, line_number);
        goto failure;
    invalid_value:
        set_failure(&state.result, FRAME_PACER_CONFIG_MALFORMED,
                    FRAME_PACER_REASON_INVALID_VALUE, line_number);
        goto failure;
    invalid_executable:
        set_failure(&state.result, FRAME_PACER_CONFIG_MALFORMED,
                    FRAME_PACER_REASON_INVALID_EXECUTABLE, line_number);
        goto failure;
    }
    if (!finish_rule(&state, limit))
        goto failure;

    if (state.selected_priority >= 0) {
        state.result.fps_limit = state.selected_fps;
        state.result.fps_source = FRAME_PACER_SOURCE_PER_GAME;
        state.result.thread_cpu_enabled = state.selected_quota_enabled;
        state.result.thread_cpu_percent = state.selected_quota;
        state.result.thread_cpu_source = state.selected_has_quota
                                             ? FRAME_PACER_SOURCE_PER_GAME
                                             : FRAME_PACER_SOURCE_DEFAULT;
        if (limit->reporting_enabled) {
            (void)snprintf(state.result.matched_section,
                           sizeof(state.result.matched_section), "%s",
                           state.selected_section);
            (void)snprintf(
                state.result.matched_executable,
                sizeof(state.result.matched_executable), "%s",
                limit->executable_candidates[state.selected_priority]);
        }
        state.result.reason = state.selected_fps_off
                                  ? FRAME_PACER_REASON_EXPLICITLY_OFF
                                  : FRAME_PACER_REASON_NONE;
    } else {
        state.result.fps_limit = global_fps;
        state.result.fps_source = state.global_present
                                      ? FRAME_PACER_SOURCE_GLOBAL
                                      : FRAME_PACER_SOURCE_DEFAULT;
        if (state.global_present && state.global_off)
            state.result.reason = FRAME_PACER_REASON_EXPLICITLY_OFF;
        else if (state.rule_count)
            state.result.reason = FRAME_PACER_REASON_NO_EXECUTABLE_MATCH;
        else
            state.result.reason = FRAME_PACER_REASON_NO_PER_GAME_RULES;
    }
    *result = state.result;
    return true;

failure:
    *result = state.result;
    result->fps_limit = FRAME_PACER_FPS_LIMIT_OFF;
    result->fps_source = FRAME_PACER_SOURCE_DEFAULT;
    result->hud_enabled = true;
    result->hud_source = FRAME_PACER_SOURCE_DEFAULT;
    result->thread_cpu_enabled = false;
    result->thread_cpu_percent = 0;
    result->thread_cpu_source = FRAME_PACER_SOURCE_DEFAULT;
    result->matched_section[0] = '\0';
    result->matched_executable[0] = '\0';
    return false;
}

static bool read_complete(int fd, char *text, size_t length)
{
    size_t offset = 0;

    while (offset < length) {
        // Reload serializes the reusable buffer, parsing, and publication.
        // Unlocking here races another refresh; ordinary frame polls are
        // atomic. NOLINTNEXTLINE(clang-analyzer-unix.BlockInCriticalSection)
        ssize_t bytes = read(fd, text + offset, length - offset);

        if (bytes < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (!bytes)
            return false;
        offset += (size_t)bytes;
    }
    return true;
}

static bool same_file(const struct stat *left, const struct stat *right)
{
    return left->st_uid == right->st_uid && left->st_dev == right->st_dev &&
           left->st_ino == right->st_ino && left->st_nlink == right->st_nlink &&
           left->st_mode == right->st_mode && left->st_size == right->st_size &&
           left->st_mtim.tv_sec == right->st_mtim.tv_sec &&
           left->st_mtim.tv_nsec == right->st_mtim.tv_nsec &&
           left->st_ctim.tv_sec == right->st_ctim.tv_sec &&
           left->st_ctim.tv_nsec == right->st_ctim.tv_nsec;
}

static void resolve_limit(struct frame_pacer_limit *limit,
                          struct frame_pacer_effective_config *result)
{
    struct stat expected, status;
    char *resized, *text;
    size_t length;
    int fd, saved_errno;

    effective_defaults(limit, result);
    if (!limit->path[0])
        return;
#ifdef FRAME_PACER_TEST
    if (limit->test_failure == FRAME_PACER_TEST_FAILURE_METADATA) {
        set_failure(result, FRAME_PACER_CONFIG_UNREADABLE,
                    FRAME_PACER_REASON_METADATA_FAILED, 0);
        return;
    }
#endif
    if (lstat(limit->path, &expected)) {
        set_failure(result,
                    errno == ENOENT || errno == ENOTDIR
                        ? FRAME_PACER_CONFIG_MISSING
                        : FRAME_PACER_CONFIG_UNREADABLE,
                    errno == ENOENT || errno == ENOTDIR
                        ? FRAME_PACER_REASON_MISSING_FILE
                        : FRAME_PACER_REASON_METADATA_FAILED,
                    0);
        return;
    }
    if (S_ISLNK(expected.st_mode)) {
        set_failure(result, FRAME_PACER_CONFIG_INSECURE,
                    FRAME_PACER_REASON_SYMBOLIC_LINK, 0);
        return;
    }
    if (!S_ISREG(expected.st_mode)) {
        set_failure(result, FRAME_PACER_CONFIG_INSECURE,
                    FRAME_PACER_REASON_NOT_REGULAR_FILE, 0);
        return;
    }
#ifdef FRAME_PACER_TEST
    if (limit->test_failure == FRAME_PACER_TEST_FAILURE_WRONG_OWNER) {
        set_failure(result, FRAME_PACER_CONFIG_INSECURE,
                    FRAME_PACER_REASON_WRONG_OWNER, 0);
        return;
    }
#endif
    if (expected.st_uid != getuid()) {
        set_failure(result, FRAME_PACER_CONFIG_INSECURE,
                    FRAME_PACER_REASON_WRONG_OWNER, 0);
        return;
    }
    if (expected.st_nlink != 1) {
        set_failure(result, FRAME_PACER_CONFIG_INSECURE,
                    FRAME_PACER_REASON_MULTIPLE_HARD_LINKS, 0);
        return;
    }
    if (expected.st_mode & (S_IRWXG | S_IRWXO)) {
        set_failure(result, FRAME_PACER_CONFIG_INSECURE,
                    FRAME_PACER_REASON_INSECURE_PERMISSIONS, 0);
        return;
    }
    if (expected.st_size < 1) {
        set_failure(result, FRAME_PACER_CONFIG_MALFORMED,
                    FRAME_PACER_REASON_EMPTY_FILE, 0);
        return;
    }
    if (expected.st_size > (off_t)FRAME_PACER_CONFIG_MAX_BYTES) {
        set_failure(result, FRAME_PACER_CONFIG_MALFORMED,
                    FRAME_PACER_REASON_FILE_TOO_LARGE, 0);
        return;
    }
#ifdef FRAME_PACER_TEST
    if (limit->test_failure == FRAME_PACER_TEST_FAILURE_OPEN) {
        set_failure(result, FRAME_PACER_CONFIG_UNREADABLE,
                    FRAME_PACER_REASON_OPEN_FAILED, 0);
        return;
    }
#endif
    fd = open(limit->path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        saved_errno = errno;
        set_failure(result, FRAME_PACER_CONFIG_UNREADABLE,
                    saved_errno == ENOENT || saved_errno == ENOTDIR ||
                            saved_errno == ELOOP
                        ? FRAME_PACER_REASON_CHANGED_DURING_READ
                        : FRAME_PACER_REASON_OPEN_FAILED,
                    0);
        return;
    }
#ifdef FRAME_PACER_TEST
    if (limit->test_failure == FRAME_PACER_TEST_FAILURE_FSTAT) {
        (void)close(fd);
        set_failure(result, FRAME_PACER_CONFIG_UNREADABLE,
                    FRAME_PACER_REASON_METADATA_FAILED, 0);
        return;
    }
#endif
    if (fstat(fd, &status)) {
        (void)close(fd);
        set_failure(result, FRAME_PACER_CONFIG_UNREADABLE,
                    FRAME_PACER_REASON_METADATA_FAILED, 0);
        return;
    }
    if (!same_file(&expected, &status)) {
        (void)close(fd);
        set_failure(result, FRAME_PACER_CONFIG_UNREADABLE,
                    FRAME_PACER_REASON_CHANGED_DURING_READ, 0);
        return;
    }
    length = (size_t)expected.st_size;
#ifdef FRAME_PACER_TEST
    if (limit->test_failure == FRAME_PACER_TEST_FAILURE_ALLOC) {
        (void)close(fd);
        set_failure(result, FRAME_PACER_CONFIG_UNREADABLE,
                    FRAME_PACER_REASON_OUT_OF_MEMORY, 0);
        return;
    }
#endif
    if (length > limit->config_buffer_capacity) {
        resized = realloc(limit->config_buffer, length);
        if (!resized) {
            (void)close(fd);
            set_failure(result, FRAME_PACER_CONFIG_UNREADABLE,
                        FRAME_PACER_REASON_OUT_OF_MEMORY, 0);
            return;
        }
        limit->config_buffer = resized;
        limit->config_buffer_capacity = length;
    }
    text = limit->config_buffer;
#ifdef FRAME_PACER_TEST
    if (limit->test_failure == FRAME_PACER_TEST_FAILURE_READ ||
        !read_complete(fd, text, length)) {
#else
    if (!read_complete(fd, text, length)) {
#endif
        (void)close(fd);
        set_failure(result, FRAME_PACER_CONFIG_UNREADABLE,
                    FRAME_PACER_REASON_READ_FAILED, 0);
        return;
    }
#ifdef FRAME_PACER_TEST
    if (limit->test_failure == FRAME_PACER_TEST_FAILURE_FINAL_FSTAT ||
        fstat(fd, &status)) {
#else
    if (fstat(fd, &status)) {
#endif
        (void)close(fd);
        set_failure(result, FRAME_PACER_CONFIG_UNREADABLE,
                    FRAME_PACER_REASON_METADATA_FAILED, 0);
        return;
    }
#ifdef FRAME_PACER_TEST
    if (limit->test_failure == FRAME_PACER_TEST_FAILURE_CHANGED ||
        !same_file(&expected, &status)) {
#else
    if (!same_file(&expected, &status)) {
#endif
        (void)close(fd);
        set_failure(result, FRAME_PACER_CONFIG_UNREADABLE,
                    FRAME_PACER_REASON_CHANGED_DURING_READ, 0);
        return;
    }
#ifdef FRAME_PACER_TEST
    if (limit->test_failure == FRAME_PACER_TEST_FAILURE_CLOSE) {
        (void)close(fd);
        set_failure(result, FRAME_PACER_CONFIG_UNREADABLE,
                    FRAME_PACER_REASON_CLOSE_FAILED, 0);
        return;
    }
#endif
    if (close(fd)) {
        set_failure(result, FRAME_PACER_CONFIG_UNREADABLE,
                    FRAME_PACER_REASON_CLOSE_FAILED, 0);
        return;
    }
    (void)parse_limit(text, length, limit, result);
}

static bool same_effective(const struct frame_pacer_effective_config *left,
                           const struct frame_pacer_effective_config *right)
{
    return left->status == right->status && left->reason == right->reason &&
           left->error_line == right->error_line &&
           left->fps_limit == right->fps_limit &&
           left->fps_source == right->fps_source &&
           left->hud_enabled == right->hud_enabled &&
           left->hud_source == right->hud_source &&
           left->thread_cpu_enabled == right->thread_cpu_enabled &&
           left->thread_cpu_percent == right->thread_cpu_percent &&
           left->thread_cpu_source == right->thread_cpu_source &&
           !strcmp(left->renderer, right->renderer) &&
           !strcmp(left->matched_section, right->matched_section) &&
           !strcmp(left->matched_executable, right->matched_executable);
}

#if defined(__GNUC__)
__attribute__((noinline))
#endif
static uint32_t refresh_limit(struct frame_pacer_limit *limit, uint64_t now_ns)
{
    struct frame_pacer_effective_config next;
    uint64_t last_check_ns, revision;
    uint32_t fps;

    (void)pthread_mutex_lock(&limit->mutex);
    last_check_ns =
        atomic_load_explicit(&limit->last_check_ns, memory_order_relaxed);
    if (last_check_ns && now_ns >= last_check_ns &&
        now_ns - last_check_ns < FRAME_PACER_CONFIG_POLL_NS) {
        fps = atomic_load_explicit(&limit->fps, memory_order_relaxed);
        (void)pthread_mutex_unlock(&limit->mutex);
        return fps;
    }
    resolve_limit(limit, &next);
    revision = 0;
    if (limit->reporting_enabled) {
        revision = atomic_load_explicit(&limit->revision, memory_order_relaxed);
        if (!revision || !same_effective(&limit->effective, &next))
            ++revision;
        next.revision = revision;
        limit->effective = next;
    }
    atomic_store_explicit(&limit->fps, next.fps_limit, memory_order_relaxed);
    atomic_store_explicit(&limit->thread_cpu_quota,
                          next.thread_cpu_enabled ? next.thread_cpu_percent : 0,
                          memory_order_relaxed);
    atomic_store_explicit(&limit->hud_enabled, next.hud_enabled,
                          memory_order_relaxed);
    if (limit->reporting_enabled)
        atomic_store_explicit(&limit->revision, revision, memory_order_release);
    atomic_store_explicit(&limit->last_check_ns, now_ns, memory_order_release);
    fps = next.fps_limit;
    (void)pthread_mutex_unlock(&limit->mutex);
    return fps;
}

uint32_t frame_pacer_limit_poll(struct frame_pacer_limit *limit,
                                uint64_t now_ns)
{
    uint64_t last_check_ns;

    if (!limit || !limit->initialized)
        return FRAME_PACER_FPS_LIMIT_OFF;
    last_check_ns =
        atomic_load_explicit(&limit->last_check_ns, memory_order_acquire);
    if (last_check_ns && now_ns >= last_check_ns &&
        now_ns - last_check_ns < FRAME_PACER_CONFIG_POLL_NS)
        return atomic_load_explicit(&limit->fps, memory_order_relaxed);
    return refresh_limit(limit, now_ns);
}

uint64_t frame_pacer_limit_revision(const struct frame_pacer_limit *limit)
{
    if (!limit || !limit->initialized)
        return 0;
    return atomic_load_explicit(&limit->revision, memory_order_acquire);
}

bool frame_pacer_limit_snapshot(struct frame_pacer_limit *limit,
                                struct frame_pacer_effective_config *snapshot)
{
    uint64_t revision;

    if (!limit || !limit->initialized || !snapshot)
        return false;
    if (!atomic_load_explicit(&limit->revision, memory_order_acquire))
        return false;
    (void)pthread_mutex_lock(&limit->mutex);
    *snapshot = limit->effective;
    revision = atomic_load_explicit(&limit->revision, memory_order_relaxed);
    (void)pthread_mutex_unlock(&limit->mutex);
    return snapshot->revision != 0 && snapshot->revision == revision;
}

void frame_pacer_limit_set_reporting_enabled(struct frame_pacer_limit *limit,
                                             bool enabled)
{
    if (!limit || !limit->initialized)
        return;
    (void)pthread_mutex_lock(&limit->mutex);
    limit->reporting_enabled = enabled;
    atomic_store_explicit(&limit->revision, 0, memory_order_release);
    atomic_store_explicit(&limit->last_check_ns, 0, memory_order_release);
    (void)pthread_mutex_unlock(&limit->mutex);
}

uint32_t frame_pacer_limit_thread_cpu_quota(struct frame_pacer_limit *limit,
                                            bool *enabled)
{
    uint32_t quota;

    if (enabled)
        *enabled = false;
    if (!limit || !limit->initialized)
        return 0;
    quota =
        atomic_load_explicit(&limit->thread_cpu_quota, memory_order_relaxed);
    if (enabled)
        *enabled = quota != 0;
    return quota;
}

bool frame_pacer_limit_hud_enabled(struct frame_pacer_limit *limit)
{
    if (!limit || !limit->initialized)
        return true;
    return atomic_load_explicit(&limit->hud_enabled, memory_order_relaxed);
}

#ifdef FRAME_PACER_TEST
void frame_pacer_limit_test_fail_at(struct frame_pacer_limit *limit,
                                    enum frame_pacer_limit_test_failure failure)
{
    if (!limit || !limit->initialized)
        return;
    (void)pthread_mutex_lock(&limit->mutex);
    limit->test_failure = failure;
    (void)pthread_mutex_unlock(&limit->mutex);
}
#endif
