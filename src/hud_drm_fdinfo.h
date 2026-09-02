#ifndef FRAME_PACER_HUD_DRM_FDINFO_H
#define FRAME_PACER_HUD_DRM_FDINFO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FRAME_PACER_DRM_FDINFO_MAX_CLIENTS 64U
#define FRAME_PACER_DRM_ENGINE_COUNT 3U

struct frame_pacer_drm_engine_state {
    uint64_t high_water_ns;
    bool started;
};

struct frame_pacer_drm_client_state {
    uint64_t client_id;
    struct frame_pacer_drm_engine_state
        engines[FRAME_PACER_DRM_ENGINE_COUNT];
    bool used;
};

struct frame_pacer_drm_fdinfo {
    struct frame_pacer_drm_client_state
        clients[FRAME_PACER_DRM_FDINFO_MAX_CLIENTS];
    uint64_t previous_sample_ns;
    unsigned int cached_use_percent;
    bool started;
    bool available;
};

/* Parse a stable Linux DRM fdinfo render, graphics, or compute counter. */
bool frame_pacer_drm_fdinfo_parse_render_ns(const char *line, uint64_t *value);
/* Convert successive per-process engine-time samples to a bounded percent. */
bool frame_pacer_drm_fdinfo_utilisation(uint64_t previous_render_ns,
                                        uint64_t render_ns,
                                        uint64_t elapsed_ns,
                                        unsigned int *percent);
/* Sample only descriptors belonging to render_node and deduplicate DRM client
 * IDs. Failed samples leave the previously committed baselines unchanged. */
bool frame_pacer_drm_fdinfo_sample(struct frame_pacer_drm_fdinfo *,
                                   unsigned int process_id,
                                   const char *render_node, uint64_t now_ns,
                                   unsigned int *percent);

#ifdef FRAME_PACER_TEST
bool frame_pacer_drm_fdinfo_test_sample_from_root(
    struct frame_pacer_drm_fdinfo *, unsigned int process_id,
    const char *render_node, uint64_t now_ns, unsigned int *percent,
    const char *proc_root);
#endif

#endif
