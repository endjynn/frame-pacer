#!/bin/sh
set -eu

find . -path './.git' -prune -o -path './build' -prune -o -path './.cache' -prune -o \
    -name '*.md' -type f -print | while IFS= read -r document; do
    directory=$(dirname "$document")
    awk '
        {
            line = $0
            while (match(line, /\]\([^)]*\)/)) {
                print substr(line, RSTART + 2, RLENGTH - 3)
                line = substr(line, RSTART + RLENGTH)
            }
        }
    ' "$document" | while IFS= read -r target; do
        case "$target" in
            *://* | mailto:* | \#*) continue ;;
        esac
        target=${target%%\#*}
        if [ ! -e "$directory/$target" ]; then
            echo "$document: broken link: $target" >&2
            exit 1
        fi
    done
done

echo 'Markdown links passed'
