#!/usr/bin/env bash

# Copyright (c) YugaByte, Inc.

# A wrapper script that pretends to be a C/C++ compiler and does some pre-processing of arguments
# and error checking on the output. Invokes GCC or Clang internally.

set -euo pipefail

SCRIPT_NAME="compiler-wrapper.sh"
YB_SRC_DIR=$( cd "$( dirname "$0" )"/../.. && pwd )

# We currently assume a specific location of the precompiled header file (used in the RocksDB
# codebase). If we add more precompiled header files, we will need to change related error handling
# here.
PCH_NAME=precompiled_header.h.gch

. "$YB_SRC_DIR"/build-support/common-build-env.sh
# The above script ensures that YB_COMPILER_TYPE is set and is valid for the OS type.

thirdparty_install_dir=$YB_SRC_DIR/thirdparty/installed/bin

# This script is invoked through symlinks called "cc" or "c++".
cc_or_cxx=${0##*/}

stderr_path=/tmp/yb-$cc_or_cxx-stderr.$RANDOM-$RANDOM-$RANDOM.$$

compiler_args=( "$@" )
output_file=""
input_files=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    -o)
      output_file=${2:-}
      if [[ $# -gt 1 ]]; then
        shift
      fi
    ;;
    *.cc|*.h|*.o)
      input_files+=( "$1" )
    ;;
    *)
    ;;
  esac
  shift
done

set_default_compiler_type
case "$YB_COMPILER_TYPE" in
  gcc)
    cc_executable=gcc
    cxx_executable=g++
  ;;
  clang)
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
if which ccache >/dev/null && [[ -z ${YB_NO_CCACHE:-} ]]; then
  export CCACHE_CC="$compiler_executable"
  cmd=( ccache compiler )
else
  cmd=( "$compiler_executable" )
fi

cmd+=( "${compiler_args[@]}" )

if [[ -n ${YB_SHOW_COMPILER_COMMAND_LINE:-} ]]; then
  echo "Using compiler: $compiler_executable"
fi

exit_handler() {
  local exit_code=$?
  if [[ $exit_code -ne 0 ]]; then
    # We output the compiler executable path because the actual command we're running will likely
    # contain ccache instead of the compiler executable.
    (
      echo "Compiler command failed with exit code $exit_code: ${cmd[@]} ;" \
           "compiler executable: $compiler_executable ;" \
           "current directory: $PWD"
      if [[ -f ${stderr_path:-} ]]; then
        if [[ -s ${stderr_path:-} ]]; then
          echo "Compiler standard error:"
          echo "----------------------------------------------------------------------------------"
          cat "$stderr_path"
          echo "----------------------------------------------------------------------------------"
        else
          echo "Compiler standard error is empty."
        fi
      fi
      echo
      echo "Input files:"
      for input_file in "${input_files[@]}"; do
        echo "  $input_file"
      done
      echo "Output file (from -o): $output_file"
      echo
    ) >&2
  fi
  rm -f "${stderr_path:-}"
  exit "$exit_code"
}

trap exit_handler EXIT

set +e
# Swap stdout and stderr, capture stderr to a file, and swap them again.
(
  (
    if [[ -n ${YB_SHOW_COMPILER_COMMAND_LINE:-} ]]; then
      set -x
    fi
    "${cmd[@]}" 3>&2 2>&1 1>&3
  ) | tee "$stderr_path"
) 3>&2 2>&1 1>&3
compiler_exit_code=$?
set -e

if grep "$PCH_NAME: created by a different GCC executable" "$stderr_path" >/dev/null || \
   grep "$PCH_NAME: not used because " "$stderr_path" >/dev/null || \
   grep "fatal error: malformed or corrupted AST file:" "$stderr_path" >/dev/null || \
   grep "new operators was enabled in PCH file but is currently disabled" "$stderr_path" \
     >/dev/null || \
   egrep "definition of macro '.*' differs between the precompiled header .* and the command line" \
         "$stderr_path" >/dev/null || \
   grep " has been modified since the precompiled header " "$stderr_path" >/dev/null
then
  PCH_PATH=$PWD/$PCH_NAME
  echo "Removing '$PCH_PATH' so that further builds have a chance to" \
       "succeed." >&2
  ( rm -f "$PCH_PATH" )
fi

if [[ $compiler_exit_code -ne 0 ]]; then
  exit "$compiler_exit_code"
fi

# Selectively treat some warnings as errors. This is not very easily done using compiler options,
# because even though there is a -Wno-error that prevents a warning from being an error even if
# -Werror is in effect, the opposite does not seem to exist.

# TODO: look into enabling -Werror for the YB part of the codebase, then we won't need this.

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

for pattern in "warning: reference to local variable .* returned" \
               "warning: enumeration value .* not handled in switch" \
               "warning: unannotated fall-through between switch labels" \
               "warning: fallthrough annotation does not directly precede switch label" \
               "warning: comparison between .* and .* .*-Wenum-compare" \
               "will be initialized after .*Wreorder"; do
  if egrep "$pattern" "$stderr_path" >/dev/null; then
    echo "[FATAL] $SCRIPT_NAME: treating warning pattern as an error: '$pattern'." >&2
    exit 1
  fi
done
