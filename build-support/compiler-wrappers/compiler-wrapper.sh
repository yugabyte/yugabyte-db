#!/usr/bin/env bash

# Copyright (c) YugaByte, Inc.

set -euo pipefail

SCRIPT_NAME="compiler-wrapper.sh"
YB_SRC_DIR=$( cd "$( dirname "$0" )"/../.. && pwd )

. "$YB_SRC_DIR"/build-support/common-build-env.sh
# The above script ensures that YB_COMPILER_TYPE is set and is valid for the OS type.

thirdparty_install_dir=$YB_SRC_DIR/thirdparty/installed/bin

# This script is invoked through symlinks called "cc" or "c++".
cc_or_cxx=${0##*/}

stderr_path=$( mktemp "/tmp/yb-$cc_or_cxx-stderr.XXXXXXXXXXXXXXXX" )
trap 'rm -f "$stderr_path"' EXIT

compiler_args=( "$@" )

case "$YB_COMPILER_TYPE" in
  gcc)
    cc_executable=gcc
    cxx_executable=g++
  ;;
  clang)
    # Remove some gcc arguments that clang does not understand.
    compiler_args=()
    if [ $# -gt 0 ]; then
      for arg in "$@"; do
        case "$arg" in
          -mno-abm|-mno-movbe)
            # skip
          ;;
          *)
            compiler_args+=( "$arg" )
        esac
      done
      unset arg
    fi

    if [[ "$OSTYPE" =~ ^darwin ]]; then
      cc_executable=/usr/bin/clang
      cxx_executable=/usr/bin/clang++
    else
      cc_executable=$thirdparty_install_dir/clang
      cxx_executable=$thirdparty_install_dir/clang++
    fi
  ;;
  *)
    echo "Invalid value for YB_COMPILER_TYPE: '$YB_COMPILER_TYPE' (must be gcc or clang)" >&2
    exit 1
esac

case "$cc_or_cxx" in
  cc) compiler_executable="$cc_executable" ;;
  c++) compiler_executable="$cxx_executable" ;;
  default)
    echo "The $SCRIPT_NAME script should be invoked through a symlink named 'cc' or 'c++', " \
         "found: $cc_or_cxx" >&2
    exit 1
esac

# We use ccache if it is available and YB_NO_CCACHE is not set.
if which ccache >/dev/null && [ -z "${YB_NO_CCACHE:-}" ]; then
  export CCACHE_CC="$compiler_executable"
  cmd=( ccache compiler )
else
  cmd=( "$compiler_executable" )
fi

cmd+=( "${compiler_args[@]}" )

if [ -n "${YB_SHOW_COMPILER_COMMAND_LINE:-}" ]; then
  echo "Using compiler: $compiler_executable"
fi

exit_handler() {
  local exit_code=$?
  if [[ "$exit_code" -ne 0 ]]; then
    echo "Compiler command failed with exit code $exit_code: ${cmd[@]} ;" \
         "compiler executable: $compiler_executable ;" \
         "current directory: $PWD" >&2
  fi
  exit "$exit_code"
}

trap 'exit_handler' EXIT

# Swap stdout and stderr, capture stderr to a file, and swap them again.
(
  (
    if [ -n "${YB_SHOW_COMPILER_COMMAND_LINE:-}" ]; then
      set -x
    fi
    "${cmd[@]}" 3>&2 2>&1 1>&3 
  ) | tee "$stderr_path"
) 3>&2 2>&1 1>&3

# Selectively treat some warnings as errors. This is not very easily done using compiler options,
# because even though there is a -Wno-error that prevents a warning from being an error even if
# -Werror is in effect, the opposite does not seem to exist.

# We are redirecting grep output to /dev/null, because it has already been shown in stderr.

if egrep "\
no return statement in function returning non-void|\
control may reach end of non-void function|\
control reaches end of non-void function" \
    "$stderr_path" >/dev/null
then
  echo "[FATAL] $SCRIPT_NAME: treating missing return value as an error." >&2
  exit 1
fi
