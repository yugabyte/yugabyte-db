#!/bin/bash
#
# Copyright 2022 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

# This function creates a tar file with a list of files paths on the DB node.
# Params:
# change_to_dir = directory to cd into for the file paths, usually yb-home dir.
# tar_file_name = name of the tar file to create.
# file_list_text_path = path to the text file containing list of file paths to collect.
create_tar_file() {
  change_to_dir=$1
  shift
  tar_file_name=$1
  shift
  file_list_text_path=$1
  mapfile -t files_list < "$file_list_text_path"
  filtered_files_list=()

  # Exit if no file paths are passed as arguments
  if [ ${#files_list[@]} -eq 0 ]
  then
    echo "Process exiting: No file names given as input to archive"
    exit 1
  fi

  # Check each file path if it exists or not
  for file_path in "${files_list[@]}"
  do
    if [ -e "$change_to_dir/$file_path" ]
    then
      filtered_files_list+=("$file_path")
    fi
  done

  # Exit if no file paths exist
  if [ ${#filtered_files_list[@]} -eq 0 ]
  then
    echo "Process exiting: None of the given files exist"
    exit 0
  fi

  echo "Starting tar process: Adding files to archive."
  printf '%s\n' "${filtered_files_list[@]}" > "$file_list_text_path"
  # Archive all existing files paths into bundle
  tar -czvf "$tar_file_name" -h -C "$change_to_dir" -T "$file_list_text_path"
  echo "Successfully added files to tar:  ${filtered_files_list[@]}"
}

check_file_exists() {
  if [ -e "$1" ]
  then
    echo "1"
  else
    echo "0"
  fi
}

# Verifies that a directory and all of its descendants up to max_depth are accessible to the
# current user. Echoes a single-line status:
#   OK            - the directory and every subdirectory within max_depth is readable and
#                   traversable.
#   MISSING       - the top-level path does not exist.
#   DENIED        - the top-level path exists but is not readable/traversable, OR some
#                   descendant within max_depth could not be read/traversed.
# Uses `stat` (instead of `test -e`) for the top-level probe so a missing path can be told
# apart from a permission error on a parent directory. For the recursive case, captures
# find's stderr - find prints "Permission denied" for every directory it cannot enter, so a
# non-empty stderr means at least one subtree within max_depth is inaccessible.
check_dir_accessible() {
  remote_dir_path=$1
  shift
  max_depth=$1

  if ! stat_err=$(stat "$remote_dir_path" 2>&1 >/dev/null); then
    if [[ "$stat_err" == *"No such file or directory"* ]]
    then
      echo "MISSING"
    else
      echo "DENIED"
    fi
    return
  fi

  if [ ! -r "$remote_dir_path" ] || [ ! -x "$remote_dir_path" ]
  then
    echo "DENIED"
    return
  fi

  # max_depth <= 0 means only the top-level path is in scope; the subsequent listing runs with
  # -maxdepth 0 too, which returns no files. Existence and readability of the top-level were
  # already verified above, so skip the recursive walk rather than relying on find's implicit
  # "start point only" behavior at -maxdepth 0.
  if [ "$max_depth" -le 0 ]
  then
    echo "OK"
    return
  fi

  perm_errors=$(find "$remote_dir_path" -maxdepth "$max_depth" -type d 2>&1 >/dev/null)
  if [ -z "$perm_errors" ]
  then
    echo "OK"
  else
    echo "DENIED"
  fi
}

get_paths_and_sizes() {
  remote_dir_path=$1
  shift
  max_depth=$1
  shift
  file_type=$1
  shift
  temp_file_path=$1

  find "$remote_dir_path" -maxdepth "$max_depth" -type "$file_type" \
  -exec ls -ltp {} + | awk '{print $5, $9}' > "$temp_file_path"
}

# This function returns a list of file names and their respective sizes, in a given directory.
# Sorts the list by modification time, with newest first.
get_paths_and_sizes_within_dates() {
  remote_dir_path=$1
  shift
  temp_file_path=$1
  shift
  start_date=$1
  shift
  end_date=$1

  # This displays the size in bytes ($5) and file path ($9) on a new line.
  cd "$remote_dir_path"
  find . -type f -newermt "$start_date" ! -newermt "$end_date" \
  -exec ls -ltp {} + | awk '{print $5, $9}' > "$temp_file_path"
}

# Function takes file path list as file input. It returns 1 for file exists
# and 0 for file does not exist corresponding to each file. Output format
# is space separated list of files and corresponding boolean value.
# file_list_text_path = path to the text file containing list of file paths to collect.
# change_dir = directory to save generated output file.
# output_filename = name of file generated containing output of files existence.
bulk_check_files_exist() {
  file_list_text_path=$1
  shift
  change_dir=$1
  shift
  output_filename=$1
  mapfile -t files_list < "$file_list_text_path"

  # Exit if no file paths are passed as arguments
  if [ ${#files_list[@]} -eq 0 ]
  then
    echo "Process exiting: No file names given as input to archive"
    exit 1
  fi

  file_list_result=()

  for file_path in "${files_list[@]}"
  do
    if [ -s "$file_path" ]
    then
      echo "$file_path 1"
    else
      echo "$file_path 0"
    fi
  done > $change_dir/$output_filename
}

"$@"
