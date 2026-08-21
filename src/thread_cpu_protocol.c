#include "thread_cpu_protocol.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static bool parse_quota(const char *text, const char *prefix, uint32_t *quota)
{
    const char *cursor;
    uint32_t value = 0;
    size_t prefix_length;

    if (!text || !prefix || !quota) return false;
    prefix_length = strlen(prefix);
    if (strncmp(text, prefix, prefix_length)) return false;
    cursor = text + prefix_length;
    if (!isdigit((unsigned char)*cursor) ||
        (*cursor == '0' && isdigit((unsigned char)cursor[1])))
        return false;
    do {
        unsigned int digit = (unsigned int)(*cursor - '0');

        if (value > (UINT32_MAX - digit) / 10U) return false;
        value = value * 10U + digit;
        ++cursor;
    } while (isdigit((unsigned char)*cursor));
    if (value < 1 || value > 100 || cursor[0] != '\n' || cursor[1] != '\0')
        return false;
    *quota = value;
    return true;
}

bool frame_pacer_thread_cpu_format_state(char *output, size_t size,
                                         bool enabled, uint32_t quota)
{
    int written;

    if (!output || !size || (enabled && (quota < 1 || quota > 100)))
        return false;
    written = enabled ? snprintf(output, size, "on %u\n", quota) :
                        snprintf(output, size, "off\n");
    return written >= 0 && (size_t)written < size;
}

bool frame_pacer_thread_cpu_parse_state(const char *text, bool *enabled,
                                        uint32_t *quota)
{
    uint32_t parsed = 0;

    if (!text || !enabled || !quota) return false;
    if (!strcmp(text, "off\n")) {
        *enabled = false;
        *quota = 0;
        return true;
    }
    if (!parse_quota(text, "on ", &parsed)) return false;
    *enabled = true;
    *quota = parsed;
    return true;
}

bool frame_pacer_thread_cpu_format_status(char *output, size_t size,
                                          bool confirmed, uint32_t quota)
{
    int written;

    if (!output || !size || (confirmed && (quota < 1 || quota > 100)))
        return false;
    written = confirmed ? snprintf(output, size, "confirmed %u\n", quota) :
                          snprintf(output, size, "off\n");
    return written >= 0 && (size_t)written < size;
}

bool frame_pacer_thread_cpu_parse_confirmation(const char *text,
                                               uint32_t quota)
{
    uint32_t parsed;

    return quota >= 1 && quota <= 100 &&
           parse_quota(text, "confirmed ", &parsed) && parsed == quota;
}
