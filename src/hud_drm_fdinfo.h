#ifndef FRAME_PACER_HUD_DRM_FDINFO_H
#define FRAME_PACER_HUD_DRM_FDINFO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct frame_pacer_drm_fdinfo {
    uint64_t previous_render_ns;
    uint64_t previous_sample_ns;
    unsigned int cached_use_percent;
    bool started;
    bool available;
};

/* Parse the stable Linux DRM fdinfo render-engine counter. */
bool frame_pacer_drm_fdinfo_parse_render_ns(const char *line, uint64_t *value);
/* Convert successive per-process engine-time samples to a bounded percent. */
bool frame_pacer_drm_fdinfo_utilisation(uint64_t previous_render_ns, uint64_t render_ns,
                                        uint64_t elapsed_ns, unsigned int *percent);
/* Samples only file descriptors belonging to render_node and deduplicates DRM
 * client IDs, because several FDs may describe one game client. */
bool frame_pacer_drm_fdinfo_sample(struct frame_pacer_drm_fdinfo *, unsigned int process_id,
                                   const char *render_node, uint64_t now_ns, unsigned int *percent);

#ifdef FRAME_PACER_TEST
bool frame_pacer_drm_fdinfo_test_update_sample(
    struct frame_pacer_drm_fdinfo *, uint64_t render_ns, uint64_t now_ns,
    unsigned int *percent);
#endif

#endif
