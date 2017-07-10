#!/usr/bin/env bash

# Copyright (c) YugaByte, Inc.
set -euo pipefail

print_help() {
  cat <<EOT
Usage: ${0##*/} [<options>]
Options:
  -n, --num_logstokeep <numlogstokeep>
    number of log files to keep.
  -z, --gzip_only
    only gzip files, don't purge.
  -h, --help
    Show usage
EOT
}

num_logs_to_keep=""
gzip_only=false
YB_LOG_DIR=/var/log/yugabyte
while [[ $# -gt 0 ]]; do
  case "$1" in
    -n|--num_logstokeep)
      num_logs_to_keep=$2
      shift
    ;;
    -z|--gzip_only)
      gzip_only=true
    ;;
    -h|--help)
      print_help
      exit 0
    ;;
    *)
      echo "Invalid option: $1" >&2
      print_help
      exit 1
  esac
  shift
done

if [[ "$(id -u)" != "0" && $USER != "yugabyte" ]]; then
  echo "This script must be run as root or yugabyte"
  exit 1
fi

if [[ -z $num_logs_to_keep && "$gzip_only" == false ]]; then
  echo "Need to specify --num_logstokeep or --gzip_only"
  print_help
  exit 1
fi

log_levels="INFO ERROR WARNING"
daemon_types="tserver master"
for daemon_type in $daemon_types; do
  for log_level in $log_levels; do
    find_non_gz_files="find $YB_LOG_DIR/$daemon_type/ -type f -name
    'yb-$daemon_type*log.$log_level*' ! -name '*.gz' -printf '%T+\t%p\n' | sort | awk '{print \$2}'"
    non_gz_file_count=$(eval $find_non_gz_files | wc -l)

    # gzip all files but the current one.
    if [ $non_gz_file_count -gt 1 ]; then
      files_to_gzip=$(eval $find_non_gz_files | head -n$(($non_gz_file_count - 1)))
      for file in $files_to_gzip; do
        echo "Compressing file $file"
        gzip $file
      done
    fi

    if [ "$gzip_only" == false ]; then
      # now delete old gz files.
      find_gz_files="find $YB_LOG_DIR/$daemon_type/ -type f -name
      'yb-$daemon_type*log.$log_level*gz' -printf '%T+\t%p\n' | sort | awk '{print \$2}'"
      gz_file_count=$(eval $find_gz_files | wc -l)

      if [ $gz_file_count -gt $num_logs_to_keep ]; then
        files_to_delete=$(eval $find_gz_files | head -n$(($gz_file_count - $num_logs_to_keep)))
        for file in $files_to_delete; do
          echo "Delete file $file"
          rm $file
        done
      fi
    fi
  done
done
