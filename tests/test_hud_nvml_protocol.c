#include "hud_nvml_protocol.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    struct frame_pacer_nvml_message input = {
        .sequence = 7,
        .sample = {
            .available = FRAME_PACER_NVML_GPU_USE |
                         FRAME_PACER_NVML_GPU_TEMP,
            .gpu_use_percent = 37,
            .gpu_temp_celsius = 64,
        },
    };
    struct frame_pacer_nvml_message output;
    unsigned char encoded[FRAME_PACER_NVML_PROTOCOL_SIZE + 1];

    memset(encoded, 0xff, sizeof(encoded));
    frame_pacer_nvml_protocol_encode(encoded, &input);
    assert(frame_pacer_nvml_protocol_decode(
        encoded, FRAME_PACER_NVML_PROTOCOL_SIZE, 0, false, &output));
    assert(output.sequence == 7);
    assert(output.sample.available == input.sample.available);
    assert(output.sample.gpu_use_percent == 37);
    assert(output.sample.gpu_temp_celsius == 64);
    assert(!frame_pacer_nvml_protocol_decode(
        encoded, FRAME_PACER_NVML_PROTOCOL_SIZE - 1, 0, false, &output));
    assert(!frame_pacer_nvml_protocol_decode(encoded, sizeof(encoded), 0,
                                             false, &output));
    assert(!frame_pacer_nvml_protocol_decode(
        encoded, FRAME_PACER_NVML_PROTOCOL_SIZE, 7, true, &output));
    encoded[0] ^= 1U;
    assert(!frame_pacer_nvml_protocol_decode(
        encoded, FRAME_PACER_NVML_PROTOCOL_SIZE, 0, false, &output));
    encoded[0] ^= 1U;
    encoded[4] = 2;
    assert(!frame_pacer_nvml_protocol_decode(
        encoded, FRAME_PACER_NVML_PROTOCOL_SIZE, 0, false, &output));
    encoded[4] = FRAME_PACER_NVML_PROTOCOL_VERSION;
    encoded[16] = 101;
    assert(!frame_pacer_nvml_protocol_decode(
        encoded, FRAME_PACER_NVML_PROTOCOL_SIZE, 0, false, &output));
    frame_pacer_nvml_protocol_encode(encoded, &input);
    encoded[20] = 201;
    assert(!frame_pacer_nvml_protocol_decode(
        encoded, FRAME_PACER_NVML_PROTOCOL_SIZE, 0, false, &output));
    frame_pacer_nvml_protocol_encode(encoded, 0);
    assert(!frame_pacer_nvml_protocol_decode(
        encoded, FRAME_PACER_NVML_PROTOCOL_SIZE, 0, false, &output));
    assert(!frame_pacer_nvml_protocol_decode(0, 0, 0, false, &output));
    assert(!frame_pacer_nvml_protocol_decode(
        encoded, FRAME_PACER_NVML_PROTOCOL_SIZE, 0, false, 0));
}
