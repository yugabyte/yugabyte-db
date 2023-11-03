#!/bin/bash
#
# Copyright 2022 YugaByte, Inc. and Contributors
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

find_paths_in_dir() {
  remote_dir_path=$1
  shift
  max_depth=$1
  shift
  file_type=$1
  shift
  temp_file_path=$1

  find "$remote_dir_path" -maxdepth "$max_depth" -type "$file_type" > "$temp_file_path"
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
