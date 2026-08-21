#!/bin/sh
set -eu
failure_trigger=${FRAME_PACER_TEST_CONTROLLER_FAIL_WRITES_WHEN:-}
cleanup()
{
    expected_trigger=$(pwd)/build/thread-cpu-write-failure.trigger
    if [ -n "$failure_trigger" ] && [ "$failure_trigger" = "$expected_trigger" ]; then
        unlink "$failure_trigger" 2>/dev/null || true
    fi
}
trap cleanup EXIT HUP INT TERM
if [ "${FRAME_PACER_THREAD_CPU_QUOTA_INTEGRATION:-}" != 1 ]; then
    echo 'refusing: set FRAME_PACER_THREAD_CPU_QUOTA_INTEGRATION=1' >&2
    exit 77
fi
program=${1:-./build/test-thread-cpu-quota-controller-integration}
"$program" &
target_pid=$!
set +e
wait "$target_pid"
result=$?
set -e
if [ "$result" -ne 0 ]; then
    exit "$result"
fi

# A helper-backed controller may outlive the target briefly so it can remove
# the delegated root after the final process leaves it.  The exact scope name
# contains the target's host PID, making this check independent of other runs.
scope_root="/sys/fs/cgroup/user.slice/user-$(id -u).slice"
attempt=0
while find "$scope_root" -maxdepth 8 -type d \
        -name "frame-pacer-thread-cpu-*-p${target_pid}.scope" \
        -print -quit 2>/dev/null | grep -q . && [ "$attempt" -lt 100 ]; do
    sleep 0.02
    attempt=$((attempt + 1))
done
if find "$scope_root" -maxdepth 8 -type d \
        -name "frame-pacer-thread-cpu-*-p${target_pid}.scope" \
        -print -quit 2>/dev/null | grep -q .; then
    echo "delegated scope for PID $target_pid was not removed" >&2
    exit 1
fi

state_root="${XDG_STATE_HOME:-$HOME/.local/state}/frame-pacer"
attempt=0
while find "$state_root" -maxdepth 1 -type f \
        -name "thread-cpu-*-p${target_pid}.scope*" \
        -print -quit 2>/dev/null | grep -q . && [ "$attempt" -lt 100 ]; do
    sleep 0.02
    attempt=$((attempt + 1))
done
if find "$state_root" -maxdepth 1 -type f \
        -name "thread-cpu-*-p${target_pid}.scope*" \
        -print -quit 2>/dev/null | grep -q .; then
    echo "controller state for PID $target_pid was not removed" >&2
    exit 1
fi
