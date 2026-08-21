#include "thread_cpu_protocol.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    char text[32];
    bool enabled = false;
    uint32_t quota = 0;

    assert(frame_pacer_thread_cpu_format_state(text, sizeof(text), true, 1));
    assert(!strcmp(text, "on 1\n"));
    assert(frame_pacer_thread_cpu_parse_state(text, &enabled, &quota));
    assert(enabled && quota == 1);
    assert(frame_pacer_thread_cpu_format_state(text, sizeof(text), true, 100));
    assert(!strcmp(text, "on 100\n"));
    assert(frame_pacer_thread_cpu_parse_state(text, &enabled, &quota));
    assert(enabled && quota == 100);
    assert(frame_pacer_thread_cpu_format_state(text, sizeof(text), false, 99));
    assert(!strcmp(text, "off\n"));
    assert(frame_pacer_thread_cpu_parse_state(text, &enabled, &quota));
    assert(!enabled && quota == 0);

    assert(!frame_pacer_thread_cpu_format_state(text, sizeof(text), true, 0));
    assert(!frame_pacer_thread_cpu_format_state(text, sizeof(text), true, 101));
    assert(!frame_pacer_thread_cpu_format_state(text, 3, false, 0));
    assert(!frame_pacer_thread_cpu_parse_state("on 0\n", &enabled, &quota));
    assert(!frame_pacer_thread_cpu_parse_state("on 01\n", &enabled, &quota));
    assert(!frame_pacer_thread_cpu_parse_state("on +1\n", &enabled, &quota));
    assert(!frame_pacer_thread_cpu_parse_state("on 101\n", &enabled, &quota));
    assert(!frame_pacer_thread_cpu_parse_state("on 1", &enabled, &quota));
    assert(!frame_pacer_thread_cpu_parse_state("off\nextra", &enabled, &quota));
    assert(!frame_pacer_thread_cpu_parse_state(0, &enabled, &quota));

    assert(frame_pacer_thread_cpu_format_status(text, sizeof(text), true, 50));
    assert(!strcmp(text, "confirmed 50\n"));
    assert(frame_pacer_thread_cpu_parse_confirmation(text, 50));
    assert(frame_pacer_thread_cpu_format_status(text, sizeof(text), false, 0));
    assert(!strcmp(text, "off\n"));
    assert(!frame_pacer_thread_cpu_format_status(text, sizeof(text), true, 0));
    assert(!frame_pacer_thread_cpu_parse_confirmation("confirmed 050\n", 50));
    assert(!frame_pacer_thread_cpu_parse_confirmation("confirmed 50", 50));
    assert(!frame_pacer_thread_cpu_parse_confirmation("confirmed 50\n", 51));
    assert(!frame_pacer_thread_cpu_parse_confirmation("confirmed 50\n", 0));
    return 0;
}
