#include "hud_nvml_protocol.h"

#include <string.h>

static void put_u16(unsigned char *output, uint16_t value)
{
    output[0] = (unsigned char)value;
    output[1] = (unsigned char)(value >> 8);
}

static void put_u32(unsigned char *output, uint32_t value)
{
    output[0] = (unsigned char)value;
    output[1] = (unsigned char)(value >> 8);
    output[2] = (unsigned char)(value >> 16);
    output[3] = (unsigned char)(value >> 24);
}

static uint16_t get_u16(const unsigned char *input)
{
    return (uint16_t)((uint16_t)input[0] | (uint16_t)input[1] << 8);
}

static uint32_t get_u32(const unsigned char *input)
{
    return (uint32_t)input[0] | (uint32_t)input[1] << 8 |
           (uint32_t)input[2] << 16 | (uint32_t)input[3] << 24;
}

void frame_pacer_nvml_protocol_encode(
    unsigned char output[FRAME_PACER_NVML_PROTOCOL_SIZE],
    const struct frame_pacer_nvml_message *message)
{
    memset(output, 0, FRAME_PACER_NVML_PROTOCOL_SIZE);
    if (!message)
        return;
    put_u32(output, FRAME_PACER_NVML_PROTOCOL_MAGIC);
    put_u16(output + 4, FRAME_PACER_NVML_PROTOCOL_VERSION);
    put_u16(output + 6, FRAME_PACER_NVML_PROTOCOL_SIZE);
    put_u32(output + 8, message->sequence);
    put_u32(output + 12, message->sample.available);
    put_u32(output + 16, message->sample.gpu_use_percent);
    put_u32(output + 20, message->sample.gpu_temp_celsius);
}

bool frame_pacer_nvml_protocol_decode(const unsigned char *input, size_t size,
                                      uint32_t previous_sequence,
                                      bool have_previous,
                                      struct frame_pacer_nvml_message *message)
{
    uint32_t sequence, available, use, temperature;

    if (!input || !message || size != FRAME_PACER_NVML_PROTOCOL_SIZE ||
        get_u32(input) != FRAME_PACER_NVML_PROTOCOL_MAGIC ||
        get_u16(input + 4) != FRAME_PACER_NVML_PROTOCOL_VERSION ||
        get_u16(input + 6) != FRAME_PACER_NVML_PROTOCOL_SIZE)
        return false;
    sequence = get_u32(input + 8);
    available = get_u32(input + 12);
    use = get_u32(input + 16);
    temperature = get_u32(input + 20);
    if (!sequence ||
        (have_previous && (int32_t)(sequence - previous_sequence) <= 0) ||
        (available & ~(FRAME_PACER_NVML_GPU_USE | FRAME_PACER_NVML_GPU_TEMP)) ||
        use > 100 || temperature > 200)
        return false;
    memset(message, 0, sizeof(*message));
    message->sequence = sequence;
    message->sample.available = available;
    message->sample.gpu_use_percent = use;
    message->sample.gpu_temp_celsius = temperature;
    return true;
}
