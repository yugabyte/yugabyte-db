#!/bin/bash

log_dir=$1
output_file=$2
grep_regex=$3
max_lines=$4
if [ -n "$5" ]; then
  min_time=$5
else
  min_time="0000-00-00T00:00:00"
fi
if [ -n "$6" ]; then
  max_time=$6
else
  max_time="9999-99-99T99:99:99"
fi

log_base_name="application"
script_name="filtered_logs.sh"
script_name_regex="filtered_logs"
grep_regex_file="$output_file-regex"
temp_file="$output_file-temp"
min_date=$(cut -c 1-10 <<< "$min_time")
max_date=$(cut -c 1-10 <<< "$max_time")

# temporary files to stores log lines
echo "$script_name - log_dir: $log_dir"
echo "$script_name - output_file: $output_file"
echo "$script_name - grep_regex: $grep_regex"

echo > "$output_file"
echo > "$grep_regex_file"
echo > "$temp_file"

# search from most recent to oldest files, returning max_lines lines with oldest log lines first
find "$log_dir" -type f -print0 | xargs -0 ls -t | grep $log_base_name | while read -r file_path; do

  current_lines=$(wc -l < "$output_file")
  lines_remaining=$((max_lines + 1 - current_lines))
  if [ "$lines_remaining" -le "0" ]
  then
    break
  fi

  file_date=$(echo $file_path | sed -n 's|.*log-\([0-9]\{4\}-[0-9]\{2\}-[0-9]\{2\}\).*|\1|p')
  echo "$script_name - Currently reading log file: $file_path, date: $file_date"

  if [ ! -z "$file_date" ] && [[ $file_date < $min_date || $file_date > $max_date ]]
  then
    echo "$file_date is not inside [$min_date - $max_date], skipping.."
    continue
  fi

  CAT="cat"
  if [[ $file_path == *.gz ]]; then
    CAT="zcat"
  fi
  $CAT "$file_path" | \
      awk -v min_time=$min_time -v max_time=$max_time -v regex="$grep_regex" 'BEGIN{IGNORECASE=1}
            /^YW [0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T/{
              if(matches && valid_time)
                print buf
              buf=matches=valid_time=null;
              time=substr($2,0,19);
              if(time >= min_time && time <= max_time){
                valid_time=1;
              }
            }
            valid_time{
              if ($0 ~ regex){matches=1}
              buf = buf ? buf ORS $0 : $0
            }
            END {
              if (matches && valid_time)
                print buf;
            }' | \
      grep -Eiv "$script_name_regex" | \
      tail -n "$lines_remaining" > "$grep_regex_file"
  cat "$grep_regex_file" "$output_file" > "$temp_file"
  mv "$temp_file" "$output_file"
done

rm "$grep_regex_file"
