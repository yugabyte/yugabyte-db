#!/usr/bin/env bash

# Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#
set -euo pipefail

print_help() {
  cat <<EOT
Usage: ${0##*/} [<options>]
Options:
  -n, --num_corestokeep <numcorestokeep>
    number of latest core files to keep (default: 5).
  -h, --help
    Show usage
EOT
}

num_cores_to_keep=5
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
