#define _POSIX_C_SOURCE 200809L
#include "hud_drm_fdinfo.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FRAME_PACER_DRM_FDINFO_INTERVAL_NS UINT64_C(500000000)
#define FRAME_PACER_DRM_FDINFO_MAX_CLIENTS 64U

bool frame_pacer_drm_fdinfo_parse_render_ns(const char *line, uint64_t *value)
{
    const char *cursor;
    char *end;
    unsigned long long parsed;
    if (!line || !value || strncmp(line, "drm-engine-render:", 18))
        return false;
    cursor = line + 18;
    while (*cursor == ' ' || *cursor == '\t')
        ++cursor;
    errno = 0;
    parsed = strtoull(cursor, &end, 10);
    if (errno || end == cursor)
        return false;
    while (*end == ' ' || *end == '\t')
        ++end;
    if (strncmp(end, "ns", 2) ||
        (end[2] && end[2] != '\r' && end[2] != '\n'))
        return false;
    *value = (uint64_t)parsed;
    return true;
}

bool frame_pacer_drm_fdinfo_utilisation(uint64_t previous_render_ns, uint64_t render_ns,
                                        uint64_t elapsed_ns, unsigned int *percent)
{
    uint64_t delta;
    if (!percent || !elapsed_ns || render_ns < previous_render_ns)
        return false;
    delta = render_ns - previous_render_ns;
    if (delta >= elapsed_ns)
        *percent = 100;
    else
        *percent =
            (unsigned int)((delta * 100 + elapsed_ns / 2) / elapsed_ns);
    return true;
}

static bool fd_matches_render_node(const char *directory, const char *fd_name,
                                   const char *render_node)
{
    char path[96], target[PATH_MAX];
    const char *base;
    ssize_t length;
    if (snprintf(path, sizeof(path), "%s/%s", directory, fd_name) >=
        (int)sizeof(path))
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
    bool have_client = false, have_render = false;
    if (snprintf(path, sizeof(path), "%sinfo/%s", directory, fd_name) >=
        (int)sizeof(path))
        return false;
    file = fopen(path, "re");
    if (!file) return false;
    while (fgets(line, sizeof(line), file)) {
        if (!strncmp(line, "drm-client-id:", 14)) {
            char *value = line + 14;
            while (*value == ' ' || *value == '\t')
                ++value;
            value[strcspn(value, "\r\n")] = '\0';
            if (*value &&
                snprintf(client, client_size, "%s", value) < (int)client_size)
                have_client = true;
        } else if (frame_pacer_drm_fdinfo_parse_render_ns(line, render_ns)) {
            have_render = true;
        }
    }
    (void)fclose(file);
    return have_client && have_render;
}

bool frame_pacer_drm_fdinfo_sample(struct frame_pacer_drm_fdinfo *state, unsigned int process_id,
                                   const char *render_node, uint64_t now_ns, unsigned int *percent)
{
    DIR *directory;
    struct dirent *entry;
    char directory_path[64], clients[FRAME_PACER_DRM_FDINFO_MAX_CLIENTS][32];
    unsigned int client_count = 0;
    uint64_t render_ns = 0;
    if (!state || !process_id || !render_node || !*render_node || !percent)
        return false;
    if (state->available && now_ns >= state->previous_sample_ns &&
        now_ns - state->previous_sample_ns < FRAME_PACER_DRM_FDINFO_INTERVAL_NS) {
        *percent = state->cached_use_percent;
        return true;
    }
    if (snprintf(directory_path, sizeof(directory_path), "/proc/%u/fd",
                 process_id) >= (int)sizeof(directory_path))
        return false;
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
    if (state->started && now_ns > state->previous_sample_ns &&
        frame_pacer_drm_fdinfo_utilisation(state->previous_render_ns, render_ns,
                                           now_ns - state->previous_sample_ns, percent)) {
        state->available = true;
        state->cached_use_percent = *percent;
    }
    state->previous_render_ns = render_ns;
    state->previous_sample_ns = now_ns;
    state->started = true;
    return state->available;
}
