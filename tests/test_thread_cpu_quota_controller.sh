#!/bin/sh
set -eu

controller=./build/frame-pacer-thread-cpu-controller
temporary_directory=$(mktemp -d)
state_directory="$temporary_directory/frame-pacer"
mkdir -m 700 "$state_directory"
state=
status=
lock=
controller_pid=

set_paths()
{
    target_pid=$1
    scope_name="frame-pacer-thread-cpu-u$(id -u)-b000000000000-p${target_pid}.scope"
    scope="/user.slice/user-$(id -u).slice/$scope_name"
    state="$state_directory/thread-cpu-$scope_name"
    status="$state.status"
    lock="$state.lock"
}

cleanup()
{
    if [ -n "$controller_pid" ]; then
        kill -TERM "$controller_pid" 2>/dev/null || true
        wait "$controller_pid" 2>/dev/null || true
    fi
    rm -rf -- "$temporary_directory"
}
trap cleanup EXIT HUP INT TERM

expect_usage_error() {
    set +e
    "$controller" "$@"
    result=$?
    set -e
    if [ "$result" -ne 64 ]; then
        echo "expected usage error (64), got $result: $*" >&2
        exit 1
    fi
}

expect_usage_error
set_paths 2
expect_usage_error --wrong 2 --scope "$scope" "$state"
expect_usage_error --pid 1 --scope "$scope" "$state"
expect_usage_error --pid 2junk --scope "$scope" "$state"
expect_usage_error --pid 2 --scope /system.slice/test.scope "$state"
expect_usage_error --pid 2 --scope /user.slice/user-0.slice/../test.scope "$state"
expect_usage_error --pid 3 --scope "$scope" "$state"
expect_usage_error --pid 2 --scope "$scope" "$state_directory/wrong-state"
chmod 0755 "$state_directory"
expect_usage_error --pid 2 --scope "$scope" "$state"
chmod 0700 "$state_directory"

set_paths 1
printf 'sentinel\n' > "$state"
expect_usage_error --pid 1 --scope "$scope" "$state"
test "$(cat "$state")" = sentinel
test ! -e "$status"

# A target that has already exited is a normal controller shutdown. Its owned
# state and status paths are removed without attempting cgroup mutation.
set_paths 2147483647
printf 'off\n' > "$state"
printf 'stale\n' > "$status"
"$controller" --pid 2147483647 --scope "$scope" "$state"
test ! -e "$state"
test ! -e "$status"

# State/status protocol paths must not follow or truncate attacker-controlled
# links, even when another process with the same UID can write the state dir.
set_paths $$
sentinel="$temporary_directory/sentinel"
printf 'sentinel\n' > "$sentinel"
ln "$sentinel" "$state"
"$controller" --pid $$ --scope "$scope" "$state"
test "$(cat "$sentinel")" = sentinel
test ! -e "$state"
ln -s "$sentinel" "$state"
"$controller" --pid $$ --scope "$scope" "$state"
test "$(cat "$sentinel")" = sentinel
test ! -e "$state"

printf 'off\n' > "$state"
"$controller" --pid $$ --scope "$scope" "$state" &
controller_pid=$!
kill -STOP "$controller_pid" 2>/dev/null || true
status_temporary="$status.tmp-$controller_pid"
ln "$sentinel" "$status_temporary"
kill -CONT "$controller_pid" 2>/dev/null || true
attempt=0
while [ ! -e "$status" ] && [ "$attempt" -lt 40 ]; do
    sleep 0.05
    attempt=$((attempt + 1))
done
test -e "$status"
test "$(cat "$sentinel")" = sentinel
kill -TERM "$controller_pid"
wait "$controller_pid"
controller_pid=
test ! -e "$state"
test ! -e "$status"
test ! -e "$status_temporary"

# The predictable controller lock must reject a hard link without truncating
# its target or consuming the valid state owned by another controller.
printf 'off\n' > "$state"
ln "$sentinel" "$lock"
set +e
"$controller" --pid $$ --scope "$scope" "$state"
result=$?
set -e
test "$result" -eq 1
test "$(cat "$sentinel")" = sentinel
test "$(cat "$state")" = off
rm "$lock" "$state"
rm "$sentinel"

# State larger than the fixed protocol buffer must be rejected as a whole,
# never accepted through a valid-looking truncated prefix.
set_paths $$
printf 'off%080d\n' 0 > "$state"
"$controller" --pid $$ --scope "$scope" "$state"
test ! -e "$state"
test ! -e "$status"

# SIGTERM is how the transient service is normally stopped. The controller
# must leave through its cleanup path rather than the default signal action.
set_paths $$
printf 'off\n' > "$state"
"$controller" --pid $$ --scope "$scope" "$state" &
controller_pid=$!
attempt=0
while [ ! -e "$status" ] && [ "$attempt" -lt 40 ]; do
    sleep 0.05
    attempt=$((attempt + 1))
done
test -e "$status"
kill -TERM "$controller_pid"
wait "$controller_pid"
controller_pid=
test ! -e "$state"
test ! -e "$status"

echo 'thread CPU quota controller CLI tests passed'
