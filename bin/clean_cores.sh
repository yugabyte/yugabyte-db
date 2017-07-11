#!/usr/bin/env bash

# Copyright (c) YugaByte, Inc.
set -euo pipefail

print_help() {
  cat <<EOT
Usage: ${0##*/} [<options>]
Options:
  -n, --num_corestokeep <numcorestokeep>
    number of latest core files to keep.
  -h, --help
    Show usage
EOT
}

num_cores_to_keep=""
YB_CRASH_DIR=/var/crash/yugabyte
while [[ $# -gt 0 ]]; do
  case "$1" in
    -n|--num_corestokeep)
      num_cores_to_keep=$2
      shift
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

if [[ -z $num_cores_to_keep ]]; then
  echo "Need to specify --num_cores_to_keep"
  print_help
  exit 1
fi

find_core_files="find $YB_CRASH_DIR -name 'core_yb*' -type f -printf '%T+\t%p\n' | sort |
awk '{print \$2}'"
num_core_files=$(eval $find_core_files | wc -l)
if [ $num_core_files -gt $num_cores_to_keep ]; then
  core_files_to_delete=$(eval $find_core_files | head -n$(($num_core_files - $num_cores_to_keep)))
  for file in $core_files_to_delete; do
    echo "Deleting core file $file"
    rm $file
  done
fi
