#!/bin/bash

log_dir=$1
output_file=$2
grep_regex=$3
max_lines=$4

script_name="filtered_logs.sh"
script_name_regex="filtered_logs"
grep_regex_file="$output_file-regex"
temp_file="$output_file-temp"
log_line_start_pat="YW [0-9]{4}-[0-9]{2}-[0-9]{2} "

# temporary files to stores log lines
echo "$script_name - log_dir: $log_dir"
echo "$script_name - output_file: $output_file"
echo "$script_name - grep_regex: $grep_regex"

echo > "$output_file"
echo > "$grep_regex_file"
echo > "$temp_file"

# search from most recent to oldest files, returning max_lines lines with oldest log lines first
find "$log_dir" -type f -print0 | xargs -0 ls -t | while read -r file_path; do

  current_lines=$(wc -l < "$output_file")
  lines_remaining=$((max_lines + 1 - current_lines))
  if [ "$lines_remaining" -le "0" ]
  then
    break
  fi

  echo "$script_name - Currently reading log file: $file_path"
  CAT="cat"
  if [[ $file_path == *.gz ]]; then
    CAT="zcat"
  fi
  $CAT "$file_path" | \
      awk "BEGIN{IGNORECASE=1}/^${log_line_start_pat}.*${grep_regex}/{flag=1;print;next}/^${log_line_start_pat}/{flag=0}flag" | \
      grep -Eiv "$script_name_regex" | \
      tail -n "$lines_remaining" > "$grep_regex_file"
  cat "$grep_regex_file" "$output_file" > "$temp_file"
  mv "$temp_file" "$output_file"
done

rm "$grep_regex_file"
