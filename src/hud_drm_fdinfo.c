#define _POSIX_C_SOURCE 200809L
#include "hud_drm_fdinfo.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum drm_engine {
    DRM_ENGINE_RENDER,
    DRM_ENGINE_GFX,
    DRM_ENGINE_COMPUTE,
    DRM_ENGINE_INVALID
};

struct observed_engine {
    uint64_t value_ns;
    bool present;
};

struct observed_client {
    uint64_t client_id;
    struct observed_engine engines[FRAME_PACER_DRM_ENGINE_COUNT];
};

enum fdinfo_result {
    FDINFO_SKIPPED,
    FDINFO_VALID,
    FDINFO_INVALID
};

static enum drm_engine core_engine(const char *line, size_t *prefix_length)
{
    static const struct {
        const char *prefix;
        enum drm_engine engine;
    } prefixes[] = {
        { "drm-engine-render:", DRM_ENGINE_RENDER },
        { "drm-engine-gfx:", DRM_ENGINE_GFX },
        { "drm-engine-compute:", DRM_ENGINE_COMPUTE },
    };
    size_t index;

    if (!line) return DRM_ENGINE_INVALID;
    for (index = 0; index < sizeof(prefixes) / sizeof(prefixes[0]); ++index) {
        size_t length = strlen(prefixes[index].prefix);

        if (!strncmp(line, prefixes[index].prefix, length)) {
            if (prefix_length) *prefix_length = length;
            return prefixes[index].engine;
        }
    }
    return DRM_ENGINE_INVALID;
}

static bool parse_unsigned(const char *text, uint64_t *value)
{
    const char *cursor = text;
    uint64_t parsed = 0;

    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    if (!isdigit((unsigned char)*cursor)) return false;
    do {
        unsigned int digit = (unsigned int)(*cursor - '0');

        if (parsed > (UINT64_MAX - digit) / 10U) return false;
        parsed = parsed * 10U + digit;
        ++cursor;
    } while (isdigit((unsigned char)*cursor));
    while (*cursor && isspace((unsigned char)*cursor)) ++cursor;
    if (*cursor) return false;
    *value = parsed;
    return true;
}

static bool parse_engine_ns(const char *line, enum drm_engine *engine,
                            uint64_t *value)
{
    const char *cursor;
    size_t prefix_length = 0;
    uint64_t parsed = 0;
    enum drm_engine parsed_engine = core_engine(line, &prefix_length);

    if (parsed_engine == DRM_ENGINE_INVALID || !value) return false;
    cursor = line + prefix_length;
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    if (!isdigit((unsigned char)*cursor)) return false;
    do {
        unsigned int digit = (unsigned int)(*cursor - '0');

        if (parsed > (UINT64_MAX - digit) / 10U) return false;
        parsed = parsed * 10U + digit;
        ++cursor;
    } while (isdigit((unsigned char)*cursor));
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    if (cursor[0] != 'n' || cursor[1] != 's') return false;
    cursor += 2;
    while (*cursor && isspace((unsigned char)*cursor)) ++cursor;
    if (*cursor) return false;
    if (engine) *engine = parsed_engine;
    *value = parsed;
    return true;
}

bool frame_pacer_drm_fdinfo_parse_render_ns(const char *line, uint64_t *value)
{
    return parse_engine_ns(line, 0, value);
}

bool frame_pacer_drm_fdinfo_utilisation(uint64_t previous_render_ns,
                                        uint64_t render_ns,
                                        uint64_t elapsed_ns,
                                        unsigned int *percent)
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
        if (delta < threshold) break;
        *percent = candidate;
    }
    return true;
}

static bool fd_matches_render_node(const char *directory, const char *fd_name,
                                   const char *render_node)
{
    char path[PATH_MAX], target[PATH_MAX];
    const char *base;
    ssize_t length;
    int written = snprintf(path, sizeof(path), "%s/%s", directory, fd_name);

    if (written < 0 || (size_t)written >= sizeof(path)) return false;
    length = readlink(path, target, sizeof(target) - 1);
    if (length < 0) return false;
    target[length] = '\0';
    base = strrchr(target, '/');
    return base && !strcmp(base + 1, render_node);
}

static enum fdinfo_result read_fdinfo(const char *directory,
                                      const char *fd_name,
                                      struct observed_client *client)
{
    FILE *file;
    char path[PATH_MAX], line[256];
    bool have_client = false, have_engine = false, valid = true;
    int written = snprintf(path, sizeof(path), "%sinfo/%s", directory,
                           fd_name);

    if (!client || written < 0 || (size_t)written >= sizeof(path))
        return FDINFO_INVALID;
    memset(client, 0, sizeof(*client));
    file = fopen(path, "re");
    if (!file) return FDINFO_SKIPPED;
    while (fgets(line, sizeof(line), file)) {
        enum drm_engine engine;
        uint64_t value;

        if (!strchr(line, '\n') && !feof(file)) {
            valid = false;
            break;
        }
        if (!strncmp(line, "drm-client-id:", 14)) {
            if (have_client || !parse_unsigned(line + 14, &client->client_id)) {
                valid = false;
                break;
            }
            have_client = true;
        } else if ((engine = core_engine(line, 0)) != DRM_ENGINE_INVALID) {
            if (!parse_engine_ns(line, &engine, &value)) {
                valid = false;
                break;
            }
            if (!client->engines[engine].present ||
                value > client->engines[engine].value_ns)
                client->engines[engine].value_ns = value;
            client->engines[engine].present = true;
            have_engine = true;
        }
    }
    if (ferror(file)) valid = false;
    (void)fclose(file);
    return valid && have_client && have_engine ? FDINFO_VALID : FDINFO_INVALID;
}

static bool merge_client(struct observed_client *clients,
                         unsigned int *client_count,
                         const struct observed_client *candidate)
{
    unsigned int client_index, engine;

    for (client_index = 0; client_index < *client_count; ++client_index)
        if (clients[client_index].client_id == candidate->client_id)
            break;
    if (client_index == *client_count) {
        if (*client_count == FRAME_PACER_DRM_FDINFO_MAX_CLIENTS)
            return false;
        clients[client_index] = *candidate;
        ++*client_count;
        return true;
    }
    for (engine = 0; engine < FRAME_PACER_DRM_ENGINE_COUNT; ++engine) {
        if (candidate->engines[engine].present &&
            (!clients[client_index].engines[engine].present ||
             candidate->engines[engine].value_ns >
                 clients[client_index].engines[engine].value_ns))
            clients[client_index].engines[engine] = candidate->engines[engine];
    }
    return true;
}

static const struct frame_pacer_drm_client_state *previous_client(
    const struct frame_pacer_drm_fdinfo *state, uint64_t client_id)
{
    unsigned int index;

    for (index = 0; index < FRAME_PACER_DRM_FDINFO_MAX_CLIENTS; ++index)
        if (state->clients[index].used &&
            state->clients[index].client_id == client_id)
            return &state->clients[index];
    return 0;
}

static bool commit_sample(struct frame_pacer_drm_fdinfo *state,
                          const struct observed_client *observed,
                          unsigned int client_count, uint64_t now_ns,
                          unsigned int *percent)
{
    struct frame_pacer_drm_client_state
        next_clients[FRAME_PACER_DRM_FDINFO_MAX_CLIENTS] = {{0}};
    uint64_t deltas[FRAME_PACER_DRM_ENGINE_COUNT] = {0};
    bool comparable[FRAME_PACER_DRM_ENGINE_COUNT] = {false};
    bool elapsed_valid = state->started && now_ns > state->previous_sample_ns;
    unsigned int client_index, engine, peak = 0;
    bool available = false;

    if (state->started && !elapsed_valid) return false;
    for (client_index = 0; client_index < client_count; ++client_index) {
        const struct frame_pacer_drm_client_state *previous =
            previous_client(state, observed[client_index].client_id);
        struct frame_pacer_drm_client_state *next =
            &next_clients[client_index];

        next->used = true;
        next->client_id = observed[client_index].client_id;
        for (engine = 0; engine < FRAME_PACER_DRM_ENGINE_COUNT; ++engine) {
            uint64_t current;

            if (!observed[client_index].engines[engine].present) continue;
            current = observed[client_index].engines[engine].value_ns;
            next->engines[engine].started = true;
            next->engines[engine].high_water_ns = current;
            if (!elapsed_valid || !previous ||
                !previous->engines[engine].started)
                continue;
            comparable[engine] = true;
            if (current < previous->engines[engine].high_water_ns) {
                next->engines[engine].high_water_ns =
                    previous->engines[engine].high_water_ns;
                continue;
            }
            current -= previous->engines[engine].high_water_ns;
            if (UINT64_MAX - deltas[engine] < current) return false;
            deltas[engine] += current;
        }
    }
    if (elapsed_valid) {
        uint64_t elapsed_ns = now_ns - state->previous_sample_ns;

        for (engine = 0; engine < FRAME_PACER_DRM_ENGINE_COUNT; ++engine) {
            unsigned int engine_percent;

            if (comparable[engine] &&
                frame_pacer_drm_fdinfo_utilisation(
                    0, deltas[engine], elapsed_ns, &engine_percent)) {
                if (!available || engine_percent > peak) peak = engine_percent;
                available = true;
            }
        }
    }
    memset(state->clients, 0, sizeof(state->clients));
    memcpy(state->clients, next_clients, sizeof(next_clients));
    state->previous_sample_ns = now_ns;
    state->cached_use_percent = available ? peak : 0;
    state->available = available;
    state->started = true;
    if (available) *percent = peak;
    return available;
}

static bool sample_from_root(struct frame_pacer_drm_fdinfo *state,
                             unsigned int process_id, const char *render_node,
                             uint64_t now_ns, unsigned int *percent,
                             const char *proc_root)
{
    struct observed_client clients[FRAME_PACER_DRM_FDINFO_MAX_CLIENTS] = {0};
    DIR *directory;
    struct dirent *entry;
    char directory_path[PATH_MAX];
    unsigned int client_count = 0;
    bool valid = true;
    int written;

    if (!state || !process_id || !render_node || !*render_node || !percent ||
        !proc_root || !*proc_root)
        return false;
    written = snprintf(directory_path, sizeof(directory_path), "%s/%u/fd",
                       proc_root, process_id);
    if (written < 0 || (size_t)written >= sizeof(directory_path)) return false;
    directory = opendir(directory_path);
    if (!directory) return false;
    for (;;) {
        struct observed_client candidate;
        enum fdinfo_result result;

        errno = 0;
        entry = readdir(directory);
        if (!entry) {
            if (errno) valid = false;
            break;
        }
        if (entry->d_name[0] == '.' ||
            !fd_matches_render_node(directory_path, entry->d_name,
                                    render_node))
            continue;
        result = read_fdinfo(directory_path, entry->d_name, &candidate);
        if (result == FDINFO_SKIPPED) continue;
        if (result == FDINFO_INVALID ||
            !merge_client(clients, &client_count, &candidate)) {
            valid = false;
            break;
        }
    }
    if (closedir(directory)) valid = false;
    if (!valid) {
        state->available = false;
        state->cached_use_percent = 0;
        return false;
    }
    if (!commit_sample(state, clients, client_count, now_ns, percent)) {
        state->available = false;
        state->cached_use_percent = 0;
        return false;
    }
    return true;
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
