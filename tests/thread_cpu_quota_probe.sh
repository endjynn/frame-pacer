#!/bin/sh
set -eu

if [ "${FRAME_PACER_THREAD_CPU_QUOTA_INTEGRATION:-}" != 1 ]; then
    echo 'refusing: set FRAME_PACER_THREAD_CPU_QUOTA_INTEGRATION=1' >&2
    exit 77
fi
exec ./build/test-thread-cpu-quota-probe
