#!/usr/bin/env bash

set -euo pipefail

. ${0%/*}/common-build-env.sh

# This is only used on Mac OS X.

# This is a wrapper around install_name_tool invoked from the FIX_ROCKSDB_RPATH function in the
# top-level CMakeLists.txt. We need to replace the "librocksdb_..." dynamic library dependency
# with the "@rpath/librocksdb_..." (of course, if there is a way to do that at build time,
# that would be preferred).

# Arguments: <candidate_file1> .. <candidate_fileN> -- <install_name_tool_cmd_line>
# Only one of the given "candidate files" is expected to exist. This is done so we can handle
# builds from CLion and the command line, as well as executables vs. dynamic libraries, uniformly.

has_invalid_librocksdb_dependency() {
  otool -L "$1" | egrep '[[:space:]]librocksdb' >/dev/null
}

if ! is_mac; then
  fatal "$0 can only run on Mac OS X"
fi

binary=""
candidate_files=()
while [[ $# -gt 0 && $1 != "--" ]]; do
  candidate_files+=( "$1" )
  shift
done

if [[ ${1:-} == "--" ]]; then
  shift
fi

binary=""
for candidate_file in "${candidate_files[@]}"; do
  if [[ -f $candidate_file ]]; then
    if [[ -n $binary ]]; then
      fatal "More than one of these files exist: ${candidate_files[@]}. Current directory: $PWD."
    fi
    binary=$candidate_file
    break
  fi
done

if [[ -z $binary ]]; then
  fatal "None of these files exists: ${candidate_files[@]}. Current directory: $PWD."
fi

# Only perform the fix on binaries that include a librocksdb dependency without an "@rpath/" suffix.
# This may be unnecessary, because .
if has_invalid_librocksdb_dependency "$binary"; then
  log "Fixing librocksdb dependency in $binary (current directory: $PWD)."
  "$@" "$binary"
  if has_invalid_librocksdb_dependency "$binary"; then
    fatal "Failed to fix invalid librocksdb dependency in '$binary'. Current directory: $PWD."
  fi
fi
