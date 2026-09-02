#define _POSIX_C_SOURCE 200809L
#include "hud_drm_fdinfo.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FRAME_PACER_DRM_FDINFO_INTERVAL_NS UINT64_C(500000000)
#define FRAME_PACER_DRM_FDINFO_MAX_CLIENTS 64U

static size_t core_engine_prefix_length(const char *line)
{
    static const char *const prefixes[] = {
        "drm-engine-render:",
        "drm-engine-gfx:",
        "drm-engine-compute:",
    };
    size_t index;

    if (!line) return 0;
    for (index = 0; index < sizeof(prefixes) / sizeof(prefixes[0]); ++index) {
        size_t length = strlen(prefixes[index]);

        if (!strncmp(line, prefixes[index], length)) return length;
    }
    return 0;
}

bool frame_pacer_drm_fdinfo_parse_render_ns(const char *line, uint64_t *value)
{
    const char *cursor;
    char *end;
    unsigned long long parsed;
    size_t prefix_length = core_engine_prefix_length(line);

    if (!prefix_length || !value) return false;
    cursor = line + prefix_length;
    while (*cursor == ' ' || *cursor == '\t')
        ++cursor;
    if (!isdigit((unsigned char)*cursor))
        return false;
    errno = 0;
    parsed = strtoull(cursor, &end, 10);
    if (errno || end == cursor)
        return false;
    while (*end == ' ' || *end == '\t')
        ++end;
    if (strncmp(end, "ns", 2)) return false;
    end += 2;
    while (*end && isspace((unsigned char)*end)) ++end;
    if (*end) return false;
    *value = (uint64_t)parsed;
    return true;
}

static bool accumulate_core_engine_ns(const char *line, uint64_t *total)
{
    uint64_t value;

    if (!total || !frame_pacer_drm_fdinfo_parse_render_ns(line, &value) ||
        UINT64_MAX - *total < value)
        return false;
    *total += value;
    return true;
}

#ifdef FRAME_PACER_TEST
bool frame_pacer_drm_fdinfo_test_accumulate_core_ns(const char *line,
                                                     uint64_t *total)
{
    return accumulate_core_engine_ns(line, total);
}
#endif

bool frame_pacer_drm_fdinfo_utilisation(uint64_t previous_render_ns, uint64_t render_ns,
                                        uint64_t elapsed_ns, unsigned int *percent)
{
    uint64_t delta;
    unsigned int candidate;

    if (!percent || !elapsed_ns || render_ns < previous_render_ns)
        return false;
    delta = render_ns - previous_render_ns;
    *percent = 0;
    for (candidate = 1; candidate <= 100; ++candidate) {
        unsigned int numerator = candidate * 2U - 1U;
        uint64_t threshold = (elapsed_ns / 200U) * numerator;
        uint64_t remainder = elapsed_ns % 200U;

        threshold += (remainder * numerator + 199U) / 200U;
        if (delta < threshold)
            break;
        *percent = candidate;
    }
    return true;
}

static bool update_sample(struct frame_pacer_drm_fdinfo *state,
                          uint64_t render_ns, uint64_t now_ns,
                          unsigned int *percent)
{
    if (state->started && now_ns > state->previous_sample_ns &&
        frame_pacer_drm_fdinfo_utilisation(state->previous_render_ns, render_ns,
                                           now_ns - state->previous_sample_ns,
                                           percent)) {
        state->available = true;
        state->cached_use_percent = *percent;
    } else if (state->started) {
        state->available = false;
        state->cached_use_percent = 0;
    }
    state->previous_render_ns = render_ns;
    state->previous_sample_ns = now_ns;
    state->started = true;
    return state->available;
}

#ifdef FRAME_PACER_TEST
bool frame_pacer_drm_fdinfo_test_update_sample(
    struct frame_pacer_drm_fdinfo *state, uint64_t render_ns, uint64_t now_ns,
    unsigned int *percent)
{
    return update_sample(state, render_ns, now_ns, percent);
}
#endif

static bool fd_matches_render_node(const char *directory, const char *fd_name,
                                   const char *render_node)
{
    char path[96], target[PATH_MAX];
    const char *base;
    ssize_t length;
    int written = snprintf(path, sizeof(path), "%s/%s", directory, fd_name);

    if (written < 0 || (size_t)written >= sizeof(path))
        return false;
    length = readlink(path, target, sizeof(target) - 1);
    if (length < 0) return false;
    target[length] = '\0';
    base = strrchr(target, '/');
    return base && !strcmp(base + 1, render_node);
}

static bool read_fdinfo(const char *directory, const char *fd_name, char *client,
                        size_t client_size, uint64_t *render_ns)
{
    FILE *file;
    char path[112], line[256];
    bool have_client = false, have_render = false, valid = true;
    int written = snprintf(path, sizeof(path), "%sinfo/%s", directory,
                           fd_name);

    if (!render_ns || written < 0 || (size_t)written >= sizeof(path))
        return false;
    *render_ns = 0;
    file = fopen(path, "re");
    if (!file) return false;
    while (fgets(line, sizeof(line), file)) {
        if (!strncmp(line, "drm-client-id:", 14)) {
            char *value = line + 14;
            int client_written;

            while (*value == ' ' || *value == '\t')
                ++value;
            value[strcspn(value, "\r\n")] = '\0';
            client_written = snprintf(client, client_size, "%s", value);
            if (*value && client_written >= 0 &&
                (size_t)client_written < client_size)
                have_client = true;
        } else if (core_engine_prefix_length(line)) {
            if (!accumulate_core_engine_ns(line, render_ns)) {
                valid = false;
                break;
            }
            have_render = true;
        }
    }
    (void)fclose(file);
    return valid && have_client && have_render;
}

static bool sample_from_root(struct frame_pacer_drm_fdinfo *state,
                             unsigned int process_id, const char *render_node,
                             uint64_t now_ns, unsigned int *percent,
                             const char *proc_root)
{
    DIR *directory;
    struct dirent *entry;
    char directory_path[PATH_MAX];
    char clients[FRAME_PACER_DRM_FDINFO_MAX_CLIENTS][32];
    unsigned int client_count = 0;
    uint64_t render_ns = 0;
    if (!state || !process_id || !render_node || !*render_node || !percent ||
        !proc_root || !*proc_root)
        return false;
    if (state->available && now_ns >= state->previous_sample_ns &&
        now_ns - state->previous_sample_ns < FRAME_PACER_DRM_FDINFO_INTERVAL_NS) {
        *percent = state->cached_use_percent;
        return true;
    }
    {
        int written = snprintf(directory_path, sizeof(directory_path),
                               "%s/%u/fd", proc_root, process_id);

        if (written < 0 || (size_t)written >= sizeof(directory_path))
            return false;
    }
    directory = opendir(directory_path);
    if (!directory) return false;
    while ((entry = readdir(directory))) {
        char client[32];
        uint64_t client_render_ns;
        unsigned int index;
        if (entry->d_name[0] == '.' ||
            !fd_matches_render_node(directory_path, entry->d_name, render_node) ||
            !read_fdinfo(directory_path, entry->d_name, client, sizeof(client),
                         &client_render_ns))
            continue;
        for (index = 0; index < client_count; ++index)
            if (!strcmp(clients[index], client))
                break;
        if (index != client_count ||
            client_count == FRAME_PACER_DRM_FDINFO_MAX_CLIENTS ||
            UINT64_MAX - render_ns < client_render_ns)
            continue;
        (void)snprintf(clients[client_count++], sizeof(clients[0]), "%s", client);
        render_ns += client_render_ns;
    }
    (void)closedir(directory);
    if (!client_count)
        return false;
    return update_sample(state, render_ns, now_ns, percent);
}

bool frame_pacer_drm_fdinfo_sample(struct frame_pacer_drm_fdinfo *state,
                                   unsigned int process_id,
                                   const char *render_node, uint64_t now_ns,
                                   unsigned int *percent)
{
    return sample_from_root(state, process_id, render_node, now_ns, percent,
                            "/proc");
}

#ifdef FRAME_PACER_TEST
bool frame_pacer_drm_fdinfo_test_sample_from_root(
    struct frame_pacer_drm_fdinfo *state, unsigned int process_id,
    const char *render_node, uint64_t now_ns, unsigned int *percent,
    const char *proc_root)
{
    return sample_from_root(state, process_id, render_node, now_ns, percent,
                            proc_root);
}
#endif
