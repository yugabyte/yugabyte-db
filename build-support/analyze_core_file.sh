#!/usr/bin/env bash

# Copyright (c) YugaByte, Inc.

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
delete_core_file=false
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

analyze_existing_core_file "$core_file_path" "$executable_path"
