#include "effective_config_report.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static struct frame_pacer_effective_config sample_config(void)
{
    struct frame_pacer_effective_config config;

    memset(&config, 0, sizeof(config));
    config.revision = 7;
    config.status = FRAME_PACER_CONFIG_VALID;
    config.reason = FRAME_PACER_REASON_NONE;
    config.fps_limit = 60;
    config.fps_source = FRAME_PACER_SOURCE_PER_GAME;
    config.hud_enabled = true;
    config.hud_source = FRAME_PACER_SOURCE_GLOBAL;
    config.thread_cpu_enabled = true;
    config.thread_cpu_percent = 50;
    config.thread_cpu_source = FRAME_PACER_SOURCE_PER_GAME;
    (void)snprintf(config.renderer, sizeof(config.renderer), "%s",
                   "Torchlight_Infinite.exe");
    (void)snprintf(config.matched_section, sizeof(config.matched_section), "%s",
                   "Torchlight Infinite");
    (void)snprintf(config.matched_executable, sizeof(config.matched_executable),
                   "%s", "Torchlight_Infinite.exe");
    return config;
}

static void test_exact_format(void)
{
    struct frame_pacer_effective_config config = sample_config();
    char output[FRAME_PACER_EFFECTIVE_REPORT_CAPACITY];
    size_t length = frame_pacer_effective_report_format(
        output, sizeof(output), &config, FRAME_PACER_REPORT_VULKAN, true);

    assert(length == strlen(output));
    assert(!strcmp(
        output,
        "frame-pacer: effective-config revision=7 trigger=startup "
        "backend=vulkan "
        "renderer=\"Torchlight_Infinite.exe\" config=valid rule=\"Torchlight "
        "Infinite\" "
        "match=\"Torchlight_Infinite.exe\" fps=60 fps_source=per-game hud=on "
        "hud_source=global thread_cpu=50% thread_cpu_source=per-game "
        "reason=none\n"));

    config.status = FRAME_PACER_CONFIG_MALFORMED;
    config.reason = FRAME_PACER_REASON_INVALID_VALUE;
    config.error_line = 42;
    config.fps_limit = 0;
    config.fps_source = FRAME_PACER_SOURCE_DEFAULT;
    config.hud_enabled = false;
    config.hud_source = FRAME_PACER_SOURCE_DEFAULT;
    config.thread_cpu_enabled = false;
    config.thread_cpu_source = FRAME_PACER_SOURCE_DEFAULT;
    config.renderer[0] = '\0';
    config.matched_section[0] = '\0';
    config.matched_executable[0] = '\0';
    assert(frame_pacer_effective_report_format(output, sizeof(output), &config,
                                               FRAME_PACER_REPORT_EGL, false));
    assert(
        strstr(output,
               "trigger=reload backend=egl renderer=unknown config=malformed"));
    assert(strstr(output, "rule=none match=none fps=off fps_source=default"));
    assert(strstr(output, "reason=invalid-value line=42\n"));
}

static void test_escaping_and_bounds(void)
{
    struct frame_pacer_effective_config config = sample_config();
    char output[FRAME_PACER_EFFECTIVE_REPORT_CAPACITY];
    size_t index, length;

    config.renderer[0] = '"';
    config.renderer[1] = '\\';
    config.renderer[2] = '\n';
    for (index = 3; index < sizeof(config.renderer) - 1; ++index)
        config.renderer[index] = 'x';
    config.renderer[sizeof(config.renderer) - 1] = '\0';
    memset(config.matched_section, 's', sizeof(config.matched_section) - 1);
    config.matched_section[sizeof(config.matched_section) - 1] = '\0';
    memset(config.matched_executable, 'm',
           sizeof(config.matched_executable) - 1);
    config.matched_executable[sizeof(config.matched_executable) - 1] = '\0';
    config.status = FRAME_PACER_CONFIG_MALFORMED;
    config.reason = FRAME_PACER_REASON_DUPLICATE_MATCHING_RULE;
    config.error_line = (size_t)-1;
    length = frame_pacer_effective_report_format(
        output, sizeof(output), &config, FRAME_PACER_REPORT_GLX, true);
    assert(length && length < sizeof(output));
    assert(length <= FRAME_PACER_EFFECTIVE_REPORT_CAPACITY - 1U);
    assert(output[length - 1] == '\n');
    assert(strstr(output, "renderer=\"\\\"\\\\\\x0A"));
    assert(strstr(output, "...\""));

    config.error_line = 0;
    for (index = FRAME_PACER_CONFIG_VALID;
         index <= FRAME_PACER_CONFIG_MALFORMED; ++index) {
        config.status = (enum frame_pacer_config_status)index;
        assert(frame_pacer_effective_report_format(
            output, sizeof(output), &config, FRAME_PACER_REPORT_GLX, true));
    }
    for (index = FRAME_PACER_REASON_NONE;
         index <= FRAME_PACER_REASON_DUPLICATE_MATCHING_RULE; ++index) {
        config.reason = (enum frame_pacer_config_reason)index;
        assert(frame_pacer_effective_report_format(
            output, sizeof(output), &config, FRAME_PACER_REPORT_GLX, true));
    }
    config.reason = (enum frame_pacer_config_reason)999;
    assert(!frame_pacer_effective_report_format(output, sizeof(output), &config,
                                                FRAME_PACER_REPORT_GLX, true));
}

struct write_context {
    _Atomic unsigned int calls;
    char message[FRAME_PACER_EFFECTIVE_REPORT_CAPACITY];
    pthread_mutex_t mutex;
};

static void capture(void *opaque, const char *message)
{
    struct write_context *context = opaque;

    (void)pthread_mutex_lock(&context->mutex);
    (void)snprintf(context->message, sizeof(context->message), "%s", message);
    (void)pthread_mutex_unlock(&context->mutex);
    (void)atomic_fetch_add_explicit(&context->calls, 1, memory_order_relaxed);
}

struct report_context {
    struct frame_pacer_effective_reporter *reporter;
    struct frame_pacer_limit *limit;
    struct write_context *writer;
};

static void *report_thread(void *opaque)
{
    struct report_context *context = opaque;

    (void)frame_pacer_effective_report_if_due(context->reporter, context->limit,
                                              FRAME_PACER_REPORT_GLX, capture,
                                              context->writer);
    return 0;
}

static void publish(struct frame_pacer_limit *limit,
                    const struct frame_pacer_effective_config *config)
{
    (void)pthread_mutex_lock(&limit->mutex);
    limit->effective = *config;
    atomic_store_explicit(&limit->fps, config->fps_limit, memory_order_relaxed);
    atomic_store_explicit(&limit->revision, config->revision,
                          memory_order_release);
    (void)pthread_mutex_unlock(&limit->mutex);
}

static void test_once_per_backend_and_revision(void)
{
    struct frame_pacer_effective_reporter reporter =
        FRAME_PACER_EFFECTIVE_REPORTER_INITIALIZER;
    struct frame_pacer_effective_config config = sample_config();
    struct write_context writer;
    struct report_context context;
    struct frame_pacer_limit limit;
    pthread_t threads[16];
    size_t index;

    memset(&writer, 0, sizeof(writer));
    atomic_init(&writer.calls, 0);
    assert(!pthread_mutex_init(&writer.mutex, 0));
    frame_pacer_limit_init(&limit);
    publish(&limit, &config);
    context.reporter = &reporter;
    context.limit = &limit;
    context.writer = &writer;
    for (index = 0; index < sizeof(threads) / sizeof(threads[0]); ++index)
        assert(!pthread_create(&threads[index], 0, report_thread, &context));
    for (index = 0; index < sizeof(threads) / sizeof(threads[0]); ++index)
        assert(!pthread_join(threads[index], 0));
    assert(atomic_load_explicit(&writer.calls, memory_order_relaxed) == 1);
    assert(strstr(writer.message, "trigger=startup backend=glx"));

    assert(frame_pacer_effective_report_if_due(
        &reporter, &limit, FRAME_PACER_REPORT_EGL, capture, &writer));
    assert(atomic_load_explicit(&writer.calls, memory_order_relaxed) == 2);
    assert(strstr(writer.message, "trigger=startup backend=egl"));
    assert(!frame_pacer_effective_report_if_due(
        &reporter, &limit, FRAME_PACER_REPORT_EGL, capture, &writer));

    ++config.revision;
    config.hud_enabled = false;
    publish(&limit, &config);
    assert(frame_pacer_effective_report_if_due(
        &reporter, &limit, FRAME_PACER_REPORT_GLX, capture, &writer));
    assert(strstr(writer.message, "revision=8 trigger=reload backend=glx"));
    frame_pacer_limit_destroy(&limit);
    assert(!pthread_mutex_destroy(&writer.mutex));
}

int main(void)
{
    test_exact_format();
    test_escaping_and_bounds();
    test_once_per_backend_and_revision();
    puts("effective configuration report tests passed");
    return 0;
}
