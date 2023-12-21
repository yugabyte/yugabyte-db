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

# shellcheck source=build-support/common-test-env.sh
. "${BASH_SOURCE%/*}"/common-test-env.sh

print_usage() {
  cat <<-EOT
Usage: ${0##*/} <options>
Prints the stack trace from the given core file using gdb or lldb.
Options:
  --core, -c <core_file_path>
    Core file

  --executable, -e <executable_path>
    Executable that generated the core file

  -h, --help
    Show usage
EOT
}

core_file_path=""
executable_path=""
while [[ $# -gt 0 ]]; do
  case $1 in
    --core|-c)
      core_file_path=$2
      shift
    ;;
    --executable|-e)
      executable_path=$2
      shift
    ;;
    -h|--help)
      print_usage
      exit 0
  esac
  shift
done

if [[ -z $core_file_path ]]; then
  fatal "--core not specified"
fi
if [[ -z $executable_path ]]; then
  fatal "--executable not specified"
fi
ensure_file_exists "$core_file_path"
ensure_file_exists "$executable_path"

analyze_existing_core_file "$core_file_path" "$executable_path" 2>&1
