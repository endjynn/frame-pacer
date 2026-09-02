#include "effective_config_report.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define ENCODED_TEXT_CAPACITY 73U
#define ENCODED_TEXT_LIMIT (ENCODED_TEXT_CAPACITY - 1U)
#define LITERAL_BYTES(value) (sizeof(value) - 1U)
#define REPORT_FIXED_MAX ( \
    LITERAL_BYTES("frame-pacer: effective-config revision=") + 20U + \
    LITERAL_BYTES(" trigger=") + 7U + \
    LITERAL_BYTES(" backend=") + 6U + \
    LITERAL_BYTES(" renderer=") + 2U + \
    LITERAL_BYTES(" config=") + 10U + \
    LITERAL_BYTES(" rule=") + 2U + \
    LITERAL_BYTES(" match=") + 2U + \
    LITERAL_BYTES(" fps=") + 3U + \
    LITERAL_BYTES(" fps_source=") + 8U + \
    LITERAL_BYTES(" hud=") + 3U + \
    LITERAL_BYTES(" hud_source=") + 8U + \
    LITERAL_BYTES(" thread_cpu=") + 4U + \
    LITERAL_BYTES(" thread_cpu_source=") + 8U + \
    LITERAL_BYTES(" reason=") + 23U + \
    LITERAL_BYTES(" line=") + 20U + 1U)

_Static_assert(REPORT_FIXED_MAX + 3U * ENCODED_TEXT_LIMIT <=
               FRAME_PACER_EFFECTIVE_REPORT_CAPACITY - 1U,
               "effective configuration report exceeds its atomic-write budget");

static const char *backend_name(enum frame_pacer_report_backend backend)
{
    switch (backend) {
    case FRAME_PACER_REPORT_VULKAN: return "vulkan";
    case FRAME_PACER_REPORT_GLX: return "glx";
    case FRAME_PACER_REPORT_EGL: return "egl";
    case FRAME_PACER_REPORT_BACKEND_COUNT: break;
    }
    return 0;
}

static const char *status_name(enum frame_pacer_config_status status)
{
    switch (status) {
    case FRAME_PACER_CONFIG_VALID: return "valid";
    case FRAME_PACER_CONFIG_MISSING: return "missing";
    case FRAME_PACER_CONFIG_INSECURE: return "insecure";
    case FRAME_PACER_CONFIG_UNREADABLE: return "unreadable";
    case FRAME_PACER_CONFIG_MALFORMED: return "malformed";
    }
    return 0;
}

static const char *source_name(enum frame_pacer_value_source source)
{
    switch (source) {
    case FRAME_PACER_SOURCE_DEFAULT: return "default";
    case FRAME_PACER_SOURCE_GLOBAL: return "global";
    case FRAME_PACER_SOURCE_PER_GAME: return "per-game";
    }
    return 0;
}

static const char *reason_name(enum frame_pacer_config_reason reason)
{
    static const char *const names[] = {
        "none", "explicitly-off", "no-per-game-rules", "no-executable-match",
        "config-path-unavailable", "missing-file", "not-regular-file",
        "symbolic-link", "wrong-owner", "multiple-hard-links",
        "insecure-permissions", "empty-file", "file-too-large", "metadata-failed",
        "open-failed", "read-failed", "close-failed", "changed-during-read",
        "out-of-memory", "invalid-section", "missing-equals", "unknown-key",
        "duplicate-key", "invalid-value", "invalid-executable", "invalid-byte",
        "incomplete-rule", "duplicate-matching-rule"
    };

    if ((unsigned int)reason >= sizeof(names) / sizeof(names[0])) return 0;
    return names[reason];
}

static size_t escaped_byte(unsigned char byte, char output[4])
{
    static const char hexadecimal[] = "0123456789ABCDEF";

    if (byte == '"' || byte == '\\') {
        output[0] = '\\';
        output[1] = (char)byte;
        return 2;
    }
    if (byte >= 0x20 && byte <= 0x7e) {
        output[0] = (char)byte;
        return 1;
    }
    output[0] = '\\';
    output[1] = 'x';
    output[2] = hexadecimal[byte >> 4];
    output[3] = hexadecimal[byte & 0xf];
    return 4;
}

static void encode_text(const char *input, char output[ENCODED_TEXT_CAPACITY])
{
    size_t input_offset = 0, output_offset = 0;
    bool truncated = false;

    while (input[input_offset]) {
        char encoded[4];
        size_t bytes = escaped_byte((unsigned char)input[input_offset], encoded);
        bool more = input[input_offset + 1] != '\0';

        if (output_offset + bytes + (more ? 3U : 0U) > ENCODED_TEXT_LIMIT) {
            truncated = true;
            break;
        }
        memcpy(output + output_offset, encoded, bytes);
        output_offset += bytes;
        ++input_offset;
    }
    if (truncated) {
        memcpy(output + output_offset, "...", 3);
        output_offset += 3;
    }
    output[output_offset] = '\0';
}

static void format_text_field(const char *encoded, const char *absent,
                              char output[ENCODED_TEXT_CAPACITY + 2U])
{
    if (encoded[0])
        (void)snprintf(output, ENCODED_TEXT_CAPACITY + 2U, "\"%s\"", encoded);
    else
        (void)snprintf(output, ENCODED_TEXT_CAPACITY + 2U, "%s", absent);
}

size_t frame_pacer_effective_report_format(
    char *output, size_t capacity, const struct frame_pacer_effective_config *config,
    enum frame_pacer_report_backend backend, bool startup)
{
    char renderer[ENCODED_TEXT_CAPACITY], rule[ENCODED_TEXT_CAPACITY];
    char match[ENCODED_TEXT_CAPACITY], renderer_field[ENCODED_TEXT_CAPACITY + 2U];
    char rule_field[ENCODED_TEXT_CAPACITY + 2U], match_field[ENCODED_TEXT_CAPACITY + 2U];
    char fps[16], thread_cpu[16], line[48];
    const char *backend_text = backend_name(backend);
    const char *status = config ? status_name(config->status) : 0;
    const char *reason = config ? reason_name(config->reason) : 0;
    const char *fps_source = config ? source_name(config->fps_source) : 0;
    const char *hud_source = config ? source_name(config->hud_source) : 0;
    const char *thread_source = config ? source_name(config->thread_cpu_source) : 0;
    int written;

    if (!output || !capacity || !config || !config->revision || !backend_text || !status ||
        !reason || !fps_source || !hud_source || !thread_source) return 0;
    encode_text(config->renderer, renderer);
    encode_text(config->matched_section, rule);
    encode_text(config->matched_executable, match);
    format_text_field(renderer, "unknown", renderer_field);
    format_text_field(rule, "none", rule_field);
    format_text_field(match, "none", match_field);
    if (config->fps_limit)
        (void)snprintf(fps, sizeof(fps), "%u", config->fps_limit);
    else
        (void)snprintf(fps, sizeof(fps), "off");
    if (config->thread_cpu_enabled)
        (void)snprintf(thread_cpu, sizeof(thread_cpu), "%u%%", config->thread_cpu_percent);
    else
        (void)snprintf(thread_cpu, sizeof(thread_cpu), "off");
    if (config->error_line)
        (void)snprintf(line, sizeof(line), " line=%zu", config->error_line);
    else
        line[0] = '\0';

    written = snprintf(output, capacity,
        "frame-pacer: effective-config revision=%" PRIu64
        " trigger=%s backend=%s renderer=%s config=%s rule=%s match=%s"
        " fps=%s fps_source=%s hud=%s hud_source=%s thread_cpu=%s"
        " thread_cpu_source=%s reason=%s%s\n",
        config->revision, startup ? "startup" : "reload", backend_text,
        renderer_field, status, rule_field, match_field,
        fps, fps_source, config->hud_enabled ? "on" : "off", hud_source,
        thread_cpu, thread_source, reason, line);
    if (written < 0 || (size_t)written >= capacity) return 0;
    return (size_t)written;
}

bool frame_pacer_effective_report_if_due(
    struct frame_pacer_effective_reporter *reporter, struct frame_pacer_limit *limit,
    enum frame_pacer_report_backend backend, frame_pacer_report_write_fn write_message,
    void *context)
{
    struct frame_pacer_effective_config snapshot;
    char message[FRAME_PACER_EFFECTIVE_REPORT_CAPACITY];
    uint64_t revision, reported;
    bool startup;

    if (!reporter || !limit || !write_message ||
        (unsigned int)backend >= FRAME_PACER_REPORT_BACKEND_COUNT) return false;
    revision = frame_pacer_limit_revision(limit);
    reported = atomic_load_explicit(&reporter->reported[backend], memory_order_acquire);
    if (!revision || revision == reported) return false;
    (void)pthread_mutex_lock(&reporter->mutex);
    reported = atomic_load_explicit(&reporter->reported[backend], memory_order_relaxed);
    if (!frame_pacer_limit_snapshot(limit, &snapshot) || snapshot.revision == reported) {
        (void)pthread_mutex_unlock(&reporter->mutex);
        return false;
    }
    startup = reported == 0;
    if (!frame_pacer_effective_report_format(message, sizeof(message), &snapshot,
                                             backend, startup)) {
        (void)pthread_mutex_unlock(&reporter->mutex);
        return false;
    }
    write_message(context, message);
    atomic_store_explicit(&reporter->reported[backend], snapshot.revision,
                          memory_order_release);
    (void)pthread_mutex_unlock(&reporter->mutex);
    return true;
}
