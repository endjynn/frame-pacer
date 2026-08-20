#include "thread_cpu_quota.h"

#include <assert.h>

int main(void)
{
    struct frame_pacer_thread_cpu_quota quota;
    uint32_t percent = 99;

    frame_pacer_thread_cpu_quota_init(&quota);
    assert(!frame_pacer_thread_cpu_quota_confirmed(&quota, &percent));
    assert(percent == 0);
    frame_pacer_thread_cpu_quota_publish(&quota, true, 75);
    assert(!frame_pacer_thread_cpu_quota_confirmed(&quota, &percent));
    assert(percent == 0);
    /* Confirmation is worker-owned and never asserted by hot-path callers. */
    assert(!frame_pacer_thread_cpu_quota_confirmed(&quota, &percent));
    assert(percent == 0);
    frame_pacer_thread_cpu_quota_publish(&quota, false, 0);
    assert(!frame_pacer_thread_cpu_quota_confirmed(&quota, &percent));
    frame_pacer_thread_cpu_quota_destroy(&quota);
    return 0;
}
