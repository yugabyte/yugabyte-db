# Copyright (c) YugaByte, Inc.

# This is common between build and test scripts.

if [ "$BASH_SOURCE" == "$0" ]; then
  echo "$BASH_SOURCE must be sourced, not executed" >&2
  exit 1
fi

# Guard against multiple inclusions.
if [ -n "${YB_COMMON_BUILD_ENV_SOURCED:-}" ]; then
  # Return to the executing script.
  return
fi
YB_COMMON_BUILD_ENV_SOURCED=1

# -------------------------------------------------------------------------------------------------
# Functions
# -------------------------------------------------------------------------------------------------

log() {
  echo "[$( date +%Y-%m-%dT%H:%M:%S )] $*"
}

expect_vars_to_be_set() {
  local calling_func_name=${FUNCNAME[1]}
  local var_name
  for var_name in "$@"; do
    if [ -z "${!var_name:-}" ]; then
      echo "The '$var_name' variable must be set by the caller of $calling_func_name." >&2
      echo "$calling_func_name expects the following variables to be set: $@." >&2
      exit 1
    fi
  done
}

# Usage: expect_some_args "$@"
# Fatals if there are no arguments.
expect_some_args() {
  local calling_func_name=${FUNCNAME[1]}
  if [ $# -eq 0 ]; then
    echo "$calling_func_name expects at least one argument" >&2
    exit 1
  fi
}

# Validates the number of arguments passed to its caller. Should also be passed all the caller's
# arguments using "$@".
# Example:
#   expect_num_args 1 "$@"
expect_num_args() {
  expect_some_args "$@"
  local caller_expected_num_args=$1
  local calling_func_name=${FUNCNAME[1]}
  shift
  if [ $# -ne "$caller_expected_num_args" ]; then
    echo "$calling_func_name expects $caller_expected_num_args arguments, got $#." >&2
    if [ $# -gt 0 ]; then
      echo "Actual arguments:" >&2
      local arg
      for arg in "$@"; do
        echo "  - $arg" >&2
      done
    fi
    exit 1
  fi
}

# Set the OS-specific dynamically linked library path. We should make it unnecessary to do this by
# setting rpath properly in all generated executables and libraries.
set_dll_path() {
  local thirdparty_library_path="$project_dir/thirdparty/installed/lib"
  local thirdparty_library_path+=":$project_dir/thirdparty/installed-deps/lib"
  echo "Adding $thirdparty_library_path to library path before the build"
  case "$OSTYPE" in
    linux*)
      LD_LIBRARY_PATH+=:$thirdparty_library_path
      export LD_LIBRARY_PATH
    ;;
    darwin*)
      DYLD_FALLBACK_LIBRARY_PATH+=:$thirdparty_library_path
      export DYLD_FALLBACK_LIBRARY_PATH
    ;;
    *)
      echo "Unknown OSTYPE: $OSTYPE" >&2
      exit 1
  esac
}

# Make a regular expression from a list.
regex_from_list() {
  expect_some_args "$@"
  local regex=""
  # no quotes around $@ on purpose: we want to break arguments containing spaces.
  for item in $@; do
    if [ -z "$item" ]; then
      continue
    fi
    if [ -n "$regex" ]; then
      regex+="|"
    fi
    regex+="$item"
  done
  echo "^($regex)$"
}

# Sets the build directory based on the given build type and the value of the YB_COMPILER_TYPE
# environment variable.
set_build_root() {
  expect_num_args 1 "$@"
  local build_type=$1
  validate_build_type "$build_type"
  build_type=$( echo "$build_type" | tr A-Z a-z )
  BUILD_ROOT=$YB_SRC_ROOT/build/$build_type-$YB_COMPILER_TYPE
}

validate_build_type() {
  expect_num_args 1 "$@"
  local build_type=$1
  build_type=$( echo "$build_type" | tr A-Z a-z )
  if [[ ! "$build_type" =~ \
        ^(debug|fastdebug|release|profile_gen|profile_build|tsan|asan)$ ]]; then
    echo "Invalid CMake build type: '$build_type'" >&2
    exit 1
  fi
}

validate_cmake_build_type() {
  expect_num_args 1 "$@"
  local cmake_build_type=$1
  cmake_build_type=$( echo "$cmake_build_type" | tr A-Z a-z )
  if [[ ! "$cmake_build_type" =~ ^(debug|fastdebug|release|profile_gen|profile_build)$ ]]; then
    echo "Invalid CMake build type: '$cmake_build_type'" >&2
    exit 1
  fi
}

# -------------------------------------------------------------------------------------------------
# Initialization
# -------------------------------------------------------------------------------------------------

# This script is expected to be in build-support.
YB_SRC_ROOT=$( cd "$( dirname "$BASH_SOURCE" )"/.. && pwd )

if [ ! -d "$YB_SRC_ROOT/build-support" ]; then
  echo "Could not determine YB source directory from '$BASH_SOURCE':" \
       "$YB_SRC_ROOT/build-support does not exist" >&2
  exit 1
fi

thirdparty_dir=$YB_SRC_ROOT/thirdparty

# Ensure the YB_COMPILER_TYPE environment variable is set. It is used by our compiler-wrapper.sh
# script to invoke the appropriate C/C++ compiler.
if [[ "$OSTYPE" =~ ^darwin ]]; then
  if [[ -z "${YB_COMPILER_TYPE:-}" ]]; then
    YB_COMPILER_TYPE=clang
  elif [[ "$YB_COMPILER_TYPE" != "clang" ]]; then
    echo "YB_COMPILER_TYPE can only be clang on Mac OS X," \
         "found YB_COMPILER_TYPE=$YB_COMPILER_TYPE" >&2
    exit 1
  fi
elif [[ -z "${YB_COMPILER_TYPE:-}" ]]; then
  YB_COMPILER_TYPE=gcc
fi
if [[ ! "$YB_COMPILER_TYPE" =~ ^(gcc|clang)$ ]]; then
  echo "Invalid compiler type: '$YB_COMPILER_TYPE' (gcc or clang expected)"
  exit 1
fi
export YB_COMPILER_TYPE
