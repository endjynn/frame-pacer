#!/bin/sh
set -eu

find build -name '*.gcno' -print | while IFS= read -r object; do
    gcov -j -b -t "$object" 2>/dev/null
done | jq -s -r '
    [.[].files[] | select(.file | startswith("src/"))] as $sources |
    ([$sources[] as $source | $source.lines[] |
        {file: $source.file, key: .line_number, count: .count}] |
      group_by([.file, .key]) |
      map({file: .[0].file, covered: (map(.count) | add > 0)})) as $lines |
    ([$sources[] as $source | $source.lines[] | .line_number as $line |
        .branches | to_entries[] |
        {file: $source.file,
         key: [$line, .value.source_block_id, .value.destination_block_id],
         count: .value.count}] |
      group_by([.file, .key]) |
      map({file: .[0].file, covered: (map(.count) | add > 0)})) as $branches |
    ([$sources[] as $source | $source.functions[] |
        {file: $source.file, key: [.name, .start_line],
         covered: (.blocks_executed > 0)}] |
      group_by([.file, .key]) |
      map({file: .[0].file, covered: (map(.covered) | any)})) as $functions |
    ([$lines[].file] | unique[]) as $file |
    ([$lines[] | select(.file == $file)]) as $file_lines |
    ([$branches[] | select(.file == $file)]) as $file_branches |
    ([$functions[] | select(.file == $file)]) as $file_functions |
    [$file,
     (($file_lines | map(select(.covered)) | length) | tostring) + "/" +
       (($file_lines | length) | tostring),
     (($file_branches | map(select(.covered)) | length) | tostring) + "/" +
       (($file_branches | length) | tostring),
     (($file_functions | map(select(.covered)) | length) | tostring) + "/" +
       (($file_functions | length) | tostring)] | @tsv
' | sort | awk 'BEGIN { print "source\tlines\tbranches\tfunctions" } { print }'
