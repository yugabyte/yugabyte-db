#!/usr/bin/env bash

# Copyright (c) YugaByte, Inc.
set -euo pipefail

print_help() {
  cat <<EOT
Usage: ${0##*/} [<options>]
Options:
  -p, --logs_disk_percent_max <logsdiskpercent>
    max percentage of disk to use for logs (default=10).
  -z, --gzip_only
    only gzip files, don't purge.
  -h, --help
    Show usage
EOT
}

gzip_only=false
YB_LOG_DIR=/var/log/yugabyte
YB_TSERVER_CONF=/etc/yugabyte/tserver.conf
YB_MASTER_CONF=/etc/yugabyte/master.conf
MAX_LOG_SIZE_FLAG=max_log_size
logs_disk_percent_max=10
while [[ $# -gt 0 ]]; do
  case "$1" in
    -p|--logs_disk_percent_max)
      logs_disk_percent_max=$2
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
  echo "This script must be run as root or yugabyte" >&2
  exit 1
fi

if [[ $logs_disk_percent_max -lt 1 || $logs_disk_percent_max -gt 100 ]]; then
  echo "--logs_disk_percent_max needs to be [1, 100]" >&2
  exit 1
fi

# half for tserver and half for master.
logs_disk_percent_max=$(expr $logs_disk_percent_max / 2)

compute_num_log_files() {
  local log_dir=$1
  local conf_file=$2
  local logdirsize_kb=$(df --output=size $log_dir | tail -n1)
  local per_log_size_mb=$(grep $MAX_LOG_SIZE_FLAG $conf_file | cut -d'=' -f2)
  if [[ $per_log_size_mb -le 0 ]]; then
    echo "--$MAX_LOG_SIZE_FLAG needs to be greater than 0, found $per_log_size_mb" >&2
    exit 1
  fi

  local num_logs_to_keep=$(expr $logdirsize_kb / 1000 * $logs_disk_percent_max / 100 /\
  $per_log_size_mb)

  if [[ $num_logs_to_keep -lt 1 ]]; then
    echo "Computed invalid num_logs_to_keep (should be atleast 1): $num_logs_to_keep" >&2
    exit 1
  fi
  echo $num_logs_to_keep
}

tserver_num_logs_to_keep=$(compute_num_log_files $YB_LOG_DIR $YB_TSERVER_CONF)
master_num_logs_to_keep=$(compute_num_log_files $YB_LOG_DIR $YB_MASTER_CONF)
echo "Num logs to keep for tserver: $tserver_num_logs_to_keep"
echo "Num logs to keep for master: $master_num_logs_to_keep"

declare -A daemon_tonumlogs
daemon_tonumlogs["tserver"]=$tserver_num_logs_to_keep
daemon_tonumlogs["master"]=$master_num_logs_to_keep

log_levels="INFO ERROR WARNING"
daemon_types="tserver master"
for daemon_type in $daemon_types; do
  num_logs_to_keep=${daemon_tonumlogs[$daemon_type]}
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
