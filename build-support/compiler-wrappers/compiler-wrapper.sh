#!/usr/bin/env bash

# Copyright (c) YugaByte, Inc.

# A wrapper script that pretends to be a C/C++ compiler and does some pre-processing of arguments
# and error checking on the output. Invokes GCC or Clang internally.

set -euo pipefail

fatal_error() {
  echo -e "$RED_COLOR[FATAL] $SCRIPT_NAME: $*$NO_COLOR"
  exit 1
}

treat_warning_pattern_as_error() {
  local pattern=$1
  # We are redirecting grep output to /dev/null, because it has already been shown in stderr.
  if egrep "$pattern" "$stderr_path" >/dev/null; then
    fatal_error "treating warning pattern as an error: '$pattern'."
  fi
}

show_compiler_command_line() {
  if [[ -f ${stderr_path:-} ]]; then
    local compiler_cmdline=$( head -1 "$stderr_path" | sed 's/^[+] //; s/\n$//' )
  else
    local compiler_cmdline="(failed to determine compiler command line)"
  fi
  local prefix=$1
  local suffix=${2:-}
  # Create a command line that can be copied and pasted.
  # As part of that, replace the ccache invocation with the actual compiler executable.
  compiler_cmdline="&& $compiler_cmdline"
  compiler_cmdline=${compiler_cmdline//&& ccache compiler/&& $compiler_executable}
  echo -e "$prefix( cd \"$PWD\" $compiler_cmdline )$suffix$NO_COLOR" >&2
}

SCRIPT_NAME="compiler-wrapper.sh"
RED_COLOR="\033[0;31m"
NO_COLOR="\033[0m"

fatal_error() {
  echo -e "$RED_COLOR[FATAL] $SCRIPT_NAME: $*$NO_COLOR"
  exit 1
}

treat_warning_pattern_as_error() {
  local pattern=$1
  # We are redirecting grep output to /dev/null, because it has already been shown in stderr.
  if egrep "$pattern" "$stderr_path" >/dev/null; then
    fatal_error "treating warning pattern as an error: '$pattern'."
  fi
}

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

stderr_path=/tmp/yb-$cc_or_cxx.$RANDOM-$RANDOM-$RANDOM.$$.stderr

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

using_ccache=false
# We use ccache if it is available and YB_NO_CCACHE is not set.
if which ccache >/dev/null && [[ -z ${YB_NO_CCACHE:-} ]]; then
  using_ccache=true
  export CCACHE_CC="$compiler_executable"
  cmd=( ccache compiler )
else
  cmd=( "$compiler_executable" )
fi

cmd+=( "${compiler_args[@]}" )

exit_handler() {
  local exit_code=$?
  if [[ $exit_code -eq 0 ]]; then
    if [[ -f ${stderr_path:-} ]]; then
      tail -n +2 "$stderr_path" >&2
    fi
  else
    # We output the compiler executable path because the actual command we're running will likely
    # contain ccache instead of the compiler executable.
    (
      show_compiler_command_line "\n$RED_COLOR" "  # Compiler exit code: $compiler_exit_code.\n"
      if [[ -f ${stderr_path:-} ]]; then
        if [[ -s ${stderr_path:-} ]]; then
          (
            red_color
            echo "/-------------------------------------------------------------------------------"
            echo "| COMPILATION FAILED"
            echo "|-------------------------------------------------------------------------------"
            IFS='\n'
            tail -n +2 "$stderr_path" | while read stderr_line; do
              echo "| $stderr_line"
            done
            unset IFS
            echo "\-------------------------------------------------------------------------------"
            no_color
          ) >&2
        else
          echo "Compiler standard error is empty." >&2
        fi
      fi
      log_empty_line
      echo "Input files:" >&2
      for input_file in "${input_files[@]}"; do
        echo "  $input_file" >&2
      done
      echo "Output file (from -o): $output_file" >&2
      log_empty_line
    ) >&2
  fi
  rm -f "${stderr_path:-}"
  exit "$exit_code"
}

trap exit_handler EXIT

set +e

( set -x; "${cmd[@]}" ) 2>"$stderr_path"
compiler_exit_code=$?

set -e

if [[ -n ${YB_SHOW_COMPILER_COMMAND_LINE:-} ]]; then
  show_compiler_command_line "$CYAN_COLOR"
fi

# Deal with failures when trying to use precompiled headers. Our current approach is to delete the
# precompiled header.
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
  echo -e "${RED_COLOR}Removing '$PCH_PATH' so that further builds have a chance to" \
          "succeed.${NO_COLOR}"
  ( rm -f "$PCH_PATH" )
fi

if [[ $compiler_exit_code -ne 0 ]]; then
  exit "$compiler_exit_code"
fi

# Selectively treat some warnings as errors. This is not very easily done using compiler options,
# because even though there is a -Wno-error that prevents a warning from being an error even if
# -Werror is in effect, the opposite does not seem to exist.

# TODO: look into enabling -Werror for the YB part of the codebase, then we won't need this.

if [[ ${#input_files[@]} -ne 1 ||
      # We don't treat unannotated fall-through in switch statement as an error for files generated
      # by Flex.
      ! ${input_files[0]} =~ [.]l[.]cc$ ]]; then
  for pattern in "warning: unannotated fall-through between switch labels" \
                 "warning: fallthrough annotation does not directly precede switch label"; do
    treat_warning_pattern_as_error "$pattern"
  done
fi

for pattern in \
    "no return statement in function returning non-void" \
    "control may reach end of non-void function" \
    "control reaches end of non-void function" \
    "warning: reference to local variable .* returned" \
    "warning: enumeration value .* not handled in switch" \
    "warning: comparison between .* and .* .*-Wenum-compare" \
    "warning: ignoring return value of function declared with warn_unused_result" \
    "will be initialized after .*Wreorder"; do
  treat_warning_pattern_as_error "$pattern"
done
