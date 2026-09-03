#ifndef FRAME_PACER_HUD_NVML_PROTOCOL_H
#define FRAME_PACER_HUD_NVML_PROTOCOL_H

#include "hud_nvml_provider.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FRAME_PACER_NVML_PROTOCOL_SIZE 24U
#define FRAME_PACER_NVML_PROTOCOL_MAGIC UINT32_C(0x4c4d4e46)
#define FRAME_PACER_NVML_PROTOCOL_VERSION 1U

struct frame_pacer_nvml_message {
    uint32_t sequence;
    struct frame_pacer_nvml_sample sample;
};

FRAME_PACER_NVML_INTERNAL void frame_pacer_nvml_protocol_encode(
    unsigned char output[FRAME_PACER_NVML_PROTOCOL_SIZE],
    const struct frame_pacer_nvml_message *);
FRAME_PACER_NVML_INTERNAL bool
frame_pacer_nvml_protocol_decode(const unsigned char *, size_t,
                                 uint32_t previous_sequence, bool have_previous,
                                 struct frame_pacer_nvml_message *);

#endif
