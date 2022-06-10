#!/bin/bash
#
# Copyright 2022 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt


create_tar_file() {
  change_to_dir=$1
  shift
  tar_file_name=$1
  shift
  files_list=("$@")
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
  # Archive all existing files paths into bundle
  tar -czvf "$tar_file_name" -h -C "$change_to_dir" "${filtered_files_list[@]}"
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

"$@"
