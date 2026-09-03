#ifndef FRAME_PACER_HUD_NVML_CLIENT_H
#define FRAME_PACER_HUD_NVML_CLIENT_H

#include "hud_nvml_protocol.h"

#include <stdbool.h>
#include <stdint.h>

FRAME_PACER_NVML_INTERNAL bool
frame_pacer_nvml_client_acquire(unsigned int process_id,
                                const char *pci_bus_id);
FRAME_PACER_NVML_INTERNAL void frame_pacer_nvml_client_release(void);
FRAME_PACER_NVML_INTERNAL bool
frame_pacer_nvml_client_snapshot(struct frame_pacer_nvml_message *);

#ifdef FRAME_PACER_TEST
FRAME_PACER_NVML_INTERNAL unsigned int
frame_pacer_nvml_client_test_attempts(void);
FRAME_PACER_NVML_INTERNAL int frame_pacer_nvml_client_test_child(void);
FRAME_PACER_NVML_INTERNAL void
frame_pacer_nvml_client_test_publish(const struct frame_pacer_nvml_message *);
#endif

#endif
