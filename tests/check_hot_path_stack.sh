#!/bin/sh
set -eu

compiler=${CC:-gcc}
output=build/performance
mkdir -p "$output"
"$compiler" -std=c17 -O2 -Isrc -fstack-usage -c src/pacer_limit.c \
    -o "$output/pacer_limit.o"
"$compiler" -m32 -std=c17 -O2 -Isrc -fstack-usage -c src/pacer_limit.c \
    -o "$output/pacer_limit-i386.o"

check_stack()
{
    usage_file=$1
    function_name=$2
    maximum=$3
    usage=$(awk -F '\t' -v name="$function_name" \
        '$1 ~ (name "$") { print $2 }' "$usage_file")

    test -n "$usage"
    test "$usage" -le "$maximum"
}

# Large parser and report snapshots belong only to the once-per-second slow
# path. This ceiling permits compiler variation while catching their accidental
# return to a per-frame accessor.
for usage_file in "$output/pacer_limit.su" "$output/pacer_limit-i386.su"; do
    check_stack "$usage_file" frame_pacer_limit_poll 64
    check_stack "$usage_file" frame_pacer_limit_thread_cpu_quota 64
    check_stack "$usage_file" frame_pacer_limit_hud_enabled 64
done
