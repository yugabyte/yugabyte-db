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
# or implied.  See the License for the specific language governing permissions and limitations under
# the License.
#

# This is common between build and test scripts.
set -euo pipefail

if [[ ${BASH_SOURCE[0]} == "$0" ]]; then
  echo "${BASH_SOURCE[0]} must be sourced, not executed" >&2
  exit 1
fi

# Guard against multiple inclusions.
if [[ -n ${YB_COMMON_BUILD_ENV_SOURCED:-} ]]; then
  # Return to the executing script.
  return
fi

readonly YB_COMMON_BUILD_ENV_SOURCED=1

yb_script_paths_are_set=false

# -------------------------------------------------------------------------------------------------
# Functions used during initialization
# -------------------------------------------------------------------------------------------------

set_script_paths() {
  if [[ $yb_script_paths_are_set == "true" ]]; then
    return
  fi
  yb_script_paths_are_set=true

  # We check that all of these scripts actually exist in common-build-env-test.sh, that's why it is
  # useful to invoke them using these constants.
  export YB_SCRIPT_PATH_AGGREGATE_TEST_REPORTS=\
$YB_SRC_ROOT/python/yugabyte/aggregate_test_reports.py
  export YB_SCRIPT_PATH_BUILD_POSTGRES=$YB_SRC_ROOT/python/yugabyte/build_postgres.py
  export YB_SCRIPT_PATH_CCMD_TOOL=$YB_SRC_ROOT/python/yugabyte/ccmd_tool.py
  export YB_SCRIPT_PATH_CHECK_PYTHON_SYNTAX=$YB_SRC_ROOT/python/yugabyte/check_python_syntax.py
  export YB_SCRIPT_PATH_DEDUP_THREAD_STACKS=$YB_SRC_ROOT/python/yugabyte/dedup_thread_stacks.py
  export YB_SCRIPT_PATH_DEPENDENCY_GRAPH=$YB_SRC_ROOT/python/yugabyte/dependency_graph.py
  export YB_SCRIPT_PATH_DOWNLOAD_AND_EXTRACT_ARCHIVE=\
$YB_SRC_ROOT/python/yugabyte/download_and_extract_archive.py
  export YB_SCRIPT_PATH_FIX_PATHS_IN_COMPILE_ERRORS=\
$YB_SRC_ROOT/python/yugabyte/fix_paths_in_compile_errors.py
  export YB_SCRIPT_PATH_GEN_AUTO_FLAGS_JSON=$YB_SRC_ROOT/python/yugabyte/gen_auto_flags_json.py
  export YB_SCRIPT_PATH_GEN_FLAGS_METADATA=$YB_SRC_ROOT/python/yugabyte/gen_flags_metadata.py
  export YB_SCRIPT_PATH_GEN_INITIAL_SYS_CATALOG_SNAPSHOT=\
$YB_SRC_ROOT/python/yugabyte/gen_initial_sys_catalog_snapshot.py
  export YB_SCRIPT_PATH_GEN_VERSION_INFO=$YB_SRC_ROOT/python/yugabyte/gen_version_info.py
  export YB_SCRIPT_PATH_IS_SAME_PATH=$YB_SRC_ROOT/python/yugabyte/is_same_path.py
  export YB_SCRIPT_PATH_KILL_LONG_RUNNING_MINICLUSTER_DAEMONS=\
$YB_SRC_ROOT/python/yugabyte/kill_long_running_minicluster_daemons.py
  export YB_SCRIPT_PATH_PARSE_TEST_FAILURE=$YB_SRC_ROOT/python/yugabyte/parse_test_failure.py
  export YB_SCRIPT_PATH_POSTPROCESS_TEST_RESULT=\
$YB_SRC_ROOT/python/yugabyte/postprocess_test_result.py
  export YB_SCRIPT_PATH_PROCESS_TREE_SUPERVISOR=\
$YB_SRC_ROOT/python/yugabyte/process_tree_supervisor.py
  export YB_SCRIPT_PATH_REWRITE_TEST_LOG=$YB_SRC_ROOT/python/yugabyte/rewrite_test_log.py
  export YB_SCRIPT_PATH_RUN_PVS_STUDIO_ANALYZER=\
$YB_SRC_ROOT/python/yugabyte/run_pvs_studio_analyzer.py
  export YB_SCRIPT_PATH_RUN_TESTS_ON_SPARK=$YB_SRC_ROOT/python/yugabyte/run_tests_on_spark.py
  export YB_SCRIPT_PATH_SPLIT_LONG_COMMAND_LINE=\
$YB_SRC_ROOT/python/yugabyte/split_long_command_line.py
  export YB_SCRIPT_PATH_THIRDPARTY_TOOL=$YB_SRC_ROOT/python/yugabyte/thirdparty_tool.py
  export YB_SCRIPT_PATH_UPDATE_TEST_RESULT_XML=\
$YB_SRC_ROOT/python/yugabyte/update_test_result_xml.py
  export YB_SCRIPT_PATH_YB_RELEASE_CORE_DB=$YB_SRC_ROOT/python/yugabyte/yb_release_core_db.py
  export YB_SCRIPT_PATH_LIST_PACKAGED_TARGETS=$YB_SRC_ROOT/python/yugabyte/list_packaged_targets.py
  export YB_SCRIPT_PATH_ANALYZE_TEST_RESULTS=$YB_SRC_ROOT/python/yugabyte/analyze_test_results.py
}

set_yb_src_root() {
  export YB_SRC_ROOT=$1
  YB_BUILD_SUPPORT_DIR=$YB_SRC_ROOT/build-support
  if [[ ! -d $YB_SRC_ROOT ]]; then
    fatal "YB_SRC_ROOT directory '$YB_SRC_ROOT' does not exist"
  fi
  YB_COMPILER_WRAPPER_CC=$YB_BUILD_SUPPORT_DIR/compiler-wrappers/cc
  YB_COMPILER_WRAPPER_CXX=$YB_BUILD_SUPPORT_DIR/compiler-wrappers/c++
  yb_java_project_dirs=( "$YB_SRC_ROOT/java" )

  set_script_paths
}

# Puts the current Git SHA1 in the current directory into the current_sha1 variable.
# Remember, this variable could also be local to the calling function.
get_current_sha1() {
  current_sha1=$( git rev-parse HEAD )
  if [[ ! $current_sha1 =~ ^[0-9a-f]{40}$ ]]; then
    # We can't use the "fatal" function yet.
    echo >&2 "Could not get current Git SHA1 in $PWD"
    exit 1
  fi
}

initialize_yugabyte_bash_common() {
  local target_sha1
  target_sha1=$(<"$YB_SRC_ROOT/build-support/yugabyte-bash-common-sha1.txt")
  if [[ ! $target_sha1 =~ ^[0-9a-f]{40}$ ]]; then
    echo >&2 "Invalid yugabyte-bash-common SHA1: $target_sha1"
    exit 1
  fi

  # Put this submodule-like directory under "build".
  YB_BASH_COMMON_DIR=$YB_SRC_ROOT/build/yugabyte-bash-common

  if [[ ! -d $YB_BASH_COMMON_DIR ]]; then
    mkdir -p "$YB_SRC_ROOT/build"
    git clone https://github.com/yugabyte/yugabyte-bash-common.git "$YB_BASH_COMMON_DIR"
  fi

  pushd "$YB_BASH_COMMON_DIR" >/dev/null
  local current_sha1
  get_current_sha1
  if [[ $current_sha1 != "$target_sha1" ]]; then
    if ! ( set -x; git checkout "$target_sha1" ); then
      (
        set -x
        git fetch
        git checkout "$target_sha1"
      )
    fi
    get_current_sha1
    if [[ $current_sha1 != "$target_sha1" ]]; then
      echo >&2 "Failed to check out target SHA1 $target_sha1 in directory $PWD." \
                "Current SHA1: $current_sha1."
      exit 1
    fi
  fi
  popd +0 >/dev/null
}

# This script is expected to be in build-support, a subdirectory of the repository root directory.
set_yb_src_root "$( cd "$( dirname "${BASH_SOURCE[0]}" )"/.. && pwd )"

if [[ $YB_SRC_ROOT == */ ]]; then
  fatal "YB_SRC_ROOT ends with '/' (not allowed): '$YB_SRC_ROOT'"
fi

initialize_yugabyte_bash_common

# shellcheck source=build/yugabyte-bash-common/src/yugabyte-bash-common.sh
. "$YB_BASH_COMMON_DIR/src/yugabyte-bash-common.sh"

# -------------------------------------------------------------------------------------------------
# Constants
# -------------------------------------------------------------------------------------------------

declare -i MAX_JAVA_BUILD_ATTEMPTS=5

# Reuse the C errno value for this.
# shellcheck disable=SC2034
declare -r -i YB_EXIT_CODE_NO_SUCH_FILE_OR_DIRECTORY=2

readonly YB_JENKINS_NFS_HOME_DIR=/Volumes/n/jenkins

# This is the parent directory for all kinds of thirdparty and toolchain tarballs.
readonly OPT_YB_BUILD_DIR="/opt/yb-build"

readonly LOCAL_THIRDPARTY_DIR_PARENT="$OPT_YB_BUILD_DIR/thirdparty"

readonly YSQL_SNAPSHOTS_DIR_PARENT="$OPT_YB_BUILD_DIR/ysql-sys-catalog-snapshots"

# Parent directories for different compiler toolchains that we know how to download and install.
readonly TOOLCHAIN_PARENT_DIR_LINUXBREW="$OPT_YB_BUILD_DIR/brew"
readonly TOOLCHAIN_PARENT_DIR_LLVM="$OPT_YB_BUILD_DIR/llvm"

readonly LOCAL_DOWNLOAD_DIR="${LOCAL_DOWNLOAD_DIR:-$OPT_YB_BUILD_DIR/download_cache}"

# The assumed number of cores per build worker. This is used in the default make parallelism level
# calculation in yb_build.sh. This does not have to be the exact number of cores per worker, but
# will affect whether or not we force the auto-scaling group of workers to expand.
readonly YB_NUM_CORES_PER_BUILD_WORKER=8

# The "number of build workers" that we'll end up using to compute the parallelism (by multiplying
# it by YB_NUM_CORES_PER_BUILD_WORKER) will be first brought into this range.
readonly MIN_EFFECTIVE_NUM_BUILD_WORKERS=5
readonly MAX_EFFECTIVE_NUM_BUILD_WORKERS=10

if [[ -z ${is_run_test_script:-} ]]; then
  is_run_test_script=false
fi
readonly is_run_test_script

YB_NFS_DOWNLOAD_CACHE_DIR=${YB_NFS_DOWNLOAD_CACHE_DIR:-$YB_JENKINS_NFS_HOME_DIR/download_cache}

readonly -a VALID_BUILD_TYPES=(
  asan
  compilecmds
  debug
  fastdebug
  release
  tsan
  tsan_slow
  pvs
  prof_gen
  prof_use
)
make_regex_from_list VALID_BUILD_TYPES "${VALID_BUILD_TYPES[@]}"

# Valid values of CMAKE_BUILD_TYPE passed to the top-level CMake build. This is the same as the
# above with the exclusion of ASAN/TSAN.
readonly -a VALID_CMAKE_BUILD_TYPES=(
  debug
  fastdebug
  release
)
make_regex_from_list VALID_CMAKE_BUILD_TYPES "${VALID_CMAKE_BUILD_TYPES[@]}"

readonly -a VALID_COMPILER_TYPES=(
  gcc
  gcc11
  gcc12
  gcc13
  clang
  clang14
  clang15
  clang16
  clang17
  clang18
)
make_regex_from_list VALID_COMPILER_TYPES "${VALID_COMPILER_TYPES[@]}"

readonly -a VALID_LINKING_TYPES=(
  dynamic
  thin-lto
  full-lto
)
make_regex_from_list VALID_LINKING_TYPES "${VALID_LINKING_TYPES[@]}"

readonly -a VALID_ARCHITECTURES=(
  x86_64
  aarch64
  arm64
)
make_regex_from_list VALID_ARCHITECTURES "${VALID_ARCHITECTURES[@]}"

readonly BUILD_ROOT_BASENAME_RE=\
"^($VALID_BUILD_TYPES_RAW_RE)-\
($VALID_COMPILER_TYPES_RAW_RE)\
(-linuxbrew)?\
(-($VALID_LINKING_TYPES_RAW_RE))\
(-($VALID_ARCHITECTURES_RAW_RE))?\
(-ninja)?\
(-clion)?$"

declare -i -r DIRECTORY_EXISTENCE_WAIT_TIMEOUT_SEC=100

declare -i -r YB_DOWNLOAD_LOCK_TIMEOUT_SEC=120

readonly YB_DOWNLOAD_LOCKS_DIR=/tmp/yb_download_locks

readonly YB_NFS_PATH_RE="^/(n|z|u|net|Volumes/net|servers|nfusr)/"

if is_mac; then
  if [[ -x /usr/local/bin/flock ]]; then
    readonly FLOCK="/usr/local/bin/flock"
    readonly FLOCK_MSG="File locked"
  else
    readonly FLOCK="/usr/bin/true"
    readonly FLOCK_MSG="Skipped file lock on macOS"
  fi
else
  readonly FLOCK="/usr/bin/flock"
  readonly FLOCK_MSG="File locked"
fi

readonly DELAY_ON_BUILD_WORKERS_LIST_HTTP_ERROR_SEC=0.5
declare -i -r MAX_ATTEMPTS_TO_GET_BUILD_WORKER=10

readonly YB_VIRTUALENV_BASENAME=venv

# -------------------------------------------------------------------------------------------------
# Maven related constants
# -------------------------------------------------------------------------------------------------

readonly YB_DEFAULT_MVN_LOCAL_REPO=$HOME/.m2/repository

readonly YB_SHARED_MVN_SETTINGS_PATH="$YB_JENKINS_NFS_HOME_DIR/m2_settings.xml"
readonly YB_DEFAULT_MVN_SETTINGS_PATH=$HOME/.m2/settings.xml

# What matches these expressions will be filtered out of Maven output.
MVN_OUTPUT_FILTER_REGEX='^\[INFO\] (Download(ing|ed)( from [-a-z0-9.]+)?): '
MVN_OUTPUT_FILTER_REGEX+='|^\[INFO\] [^ ]+ already added, skipping$'
MVN_OUTPUT_FILTER_REGEX+='|^\[INFO\] Copying .*[.]jar to .*[.]jar$'
MVN_OUTPUT_FILTER_REGEX+='|^\[INFO\] Resolved: .*$'
MVN_OUTPUT_FILTER_REGEX+='|^\[INFO\] Resolved plugin: .*$'
MVN_OUTPUT_FILTER_REGEX+='|^\[INFO\] Resolved dependency: .*$'
MVN_OUTPUT_FILTER_REGEX+='|^\[INFO\] Installing .* to .*$'
MVN_OUTPUT_FILTER_REGEX+='|^Generating .*[.]html[.][.][.]$'
readonly MVN_OUTPUT_FILTER_REGEX

# This is used in yb_build.sh and build-and-test.sh.
# shellcheck disable=SC2034
readonly -a MVN_OPTS_TO_DOWNLOAD_ALL_DEPS=(
  dependency:go-offline
  dependency:resolve
  dependency:resolve-plugins
  "-DoutputFile=/dev/null"
)

# -------------------------------------------------------------------------------------------------
# Global variables
# -------------------------------------------------------------------------------------------------

# This is needed so we can ignore thirdparty_path.txt and linuxbrew_path.txt
# in the build directory and not pick up old paths from those files in
# a clean build.
is_clean_build=false

# A human-readable description of how we set the respective variables.
yb_thirdparty_dir_origin=""

if [[ -n ${YB_THIRDPARTY_DIR:-} ]]; then
  yb_thirdparty_dir_origin="from environment"
fi

yb_thirdparty_url_origin=""
if [[ -n ${YB_THIRDPARTY_URL:-} ]]; then
  yb_thirdparty_url_origin="from environment"
fi

yb_thirdparty_checksum_url_origin=""
if [[ -n ${YB_THIRDPARTY_CHECKSUM_URL:-} ]]; then
  yb_thirdparty_checksum_url_origin="from environment"
fi

yb_linuxbrew_dir_origin=""
if [[ -n ${YB_LINUXBREW_DIR:-} ]]; then
  yb_linuxbrew_dir_origin="from environment"
fi

yb_llvm_toolchain_url_origin=""
if [[ -n ${YB_LLVM_TOOLCHAIN_URL:-} ]]; then
  yb_llvm_toolchain_url_origin="from environment"
fi

yb_llvm_toolchain_dir_origin=""
if [[ -n ${YB_LLVM_TOOLCHAIN_DIR:-} ]]; then
  yb_llvm_toolchain_dir_origin="from environment"
fi

# To deduplicate Maven arguments
yb_mvn_parameters_already_set=false

is_apple_silicon=""  # Will be set to true or false when necessary.

# -------------------------------------------------------------------------------------------------
# Functions
# -------------------------------------------------------------------------------------------------

# Usage: expect_no_args "$#"
# Alternative to expect_num_args 0 "$@", because it will trigger
# https://www.shellcheck.net/wiki/SC2119 when passing no arguments.
expect_no_args() {
  if (( $1 != 0 )); then
    fatal "${FUNCNAME[1]} expects at least zero arguments. Got: $1"
  fi
}

yb_activate_debug_mode() {
  PS4='[${BASH_SOURCE[0]}:${LINENO} ${FUNCNAME[0]:-}] '
  set -x
}

normalize_build_type() {
  if [[ -z ${build_type:-} ]]; then
    if [[ -n ${BUILD_TYPE:-} ]]; then
      build_type=$BUILD_TYPE
    else
      fatal "Neither build_type or BUILD_TYPE are set"
    fi
  fi
  validate_build_type "$build_type"
  local lowercase_build_type
  lowercase_build_type=$( echo "$build_type" | to_lowercase )
  if [[ "$build_type" != "$lowercase_build_type" ]]; then
    # Only assign if we actually need to, because the build_type variable may already be read-only.
    build_type=$lowercase_build_type
  fi
}

decide_whether_to_use_linuxbrew() {
  expect_vars_to_be_set YB_COMPILER_TYPE build_type
  if [[ -z ${YB_USE_LINUXBREW:-} ]]; then
    if [[ -n ${predefined_build_root:-} ]]; then
      if [[ ${predefined_build_root##*/} == *-linuxbrew-* ]]; then
        YB_USE_LINUXBREW=1
      fi
    fi
    # Default is no linuxbrew
    export YB_USE_LINUXBREW=${YB_USE_LINUXBREW:-0}
  fi
}

# Sets the build directory based on the given build type (the build_type variable) and the value of
# the YB_COMPILER_TYPE environment variable.
set_build_root() {
  detect_architecture

  if [[ ${1:-} == "--no-readonly" ]]; then
    local -r make_build_root_readonly=false
    shift
  else
    local -r make_build_root_readonly=true
  fi

  expect_no_args "$#"
  normalize_build_type
  readonly build_type

  if [[ -z ${YB_COMPILER_TYPE:-} ]]; then
    fatal "YB_COMPILER_TYPE is not set"
  fi
  validate_compiler_type "$YB_COMPILER_TYPE"

  BUILD_ROOT=$YB_BUILD_PARENT_DIR/$build_type-$YB_COMPILER_TYPE

  decide_whether_to_use_linuxbrew

  if using_linuxbrew; then
    BUILD_ROOT+="-linuxbrew"
  fi

  BUILD_ROOT+="-${YB_LINKING_TYPE:-dynamic}"
  if is_apple_silicon; then
    # Append the target architecture (x86_64 or arm64).
    BUILD_ROOT+=-${YB_TARGET_ARCH}
  fi

  if using_ninja; then
    BUILD_ROOT+="-ninja"
  fi

  normalize_build_root

  if [[ ${make_build_root_readonly} == "true" ]]; then
    readonly BUILD_ROOT
  fi

  if [[ -n ${predefined_build_root:-} &&
        $predefined_build_root != "$BUILD_ROOT" ]] &&
     ! "$YB_SCRIPT_PATH_IS_SAME_PATH" "$predefined_build_root" "$BUILD_ROOT"; then
    fatal "An inconsistency between predefined BUILD_ROOT ('$predefined_build_root') and" \
          "computed BUILD_ROOT ('$BUILD_ROOT')."
  fi

  export BUILD_ROOT
  export YB_BUILD_ROOT=$BUILD_ROOT
  decide_whether_to_use_ninja
}

# Resolve the BUILD_ROOT symlink and save the result to the real_build_root_path variable.
set_real_build_root_path() {
  if [[ -h $BUILD_ROOT ]]; then
    real_build_root_path=$( readlink "$BUILD_ROOT" )
  else
    real_build_root_path="$BUILD_ROOT"
  fi

  real_build_root_path=$( cd "$real_build_root_path" && pwd )
  readonly real_build_root_path
}

ensure_build_root_is_set() {
  if [[ -z ${BUILD_ROOT:-} ]]; then
    fatal "The BUILD_ROOT environment variable is not set. This must point to the absolute path" \
          "of the build root directory, e.g. '<yugabyte_src_dir>/build/debug'."
  fi
}

ensure_build_root_exists() {
  ensure_build_root_is_set
  if [[ ! -d $BUILD_ROOT ]]; then
    fatal "The directory BUILD_ROOT ('$BUILD_ROOT') does not exist"
  fi
}

normalize_build_root() {
  ensure_build_root_is_set
  if [[ -d $BUILD_ROOT ]]; then
    BUILD_ROOT=$( cd "$BUILD_ROOT" && pwd )
  fi
}

validate_build_type() {
  expect_num_args 1 "$@"
  # Local variable named _build_type to avoid a collision with the global build_type variable.
  local _build_type=$1
  if ! is_valid_build_type "$_build_type"; then
    fatal "Invalid build type: '$_build_type'. Valid build types are: ${VALID_BUILD_TYPES[*]}" \
          "(case-insensitive)."
  fi
}

is_valid_build_type() {
  expect_num_args 1 "$@"
  local -r _build_type=$( echo "$1" | to_lowercase )
  [[ "$_build_type" =~ $VALID_BUILD_TYPES_RE ]]
}

set_build_type_based_on_jenkins_job_name() {
  if [[ -n "${build_type:-}" ]]; then
    if [[ -n "${JOB_NAME:-}" ]]; then
      # This message only makes sense if JOB_NAME is set.
      log "Build type is already set to '$build_type', not setting it based on Jenkins job name."
    fi
    normalize_build_type
    readonly build_type
    return
  fi

  build_type=debug
  if [[ -z "${JOB_NAME:-}" ]]; then
    log "Using build type '$build_type' by default because JOB_NAME is not set."
    readonly build_type
    return
  fi
  local _build_type  # to avoid collision with the global build_type variable
  local jenkins_job_name
  jenkins_job_name=$( echo "$JOB_NAME" | to_lowercase )
  for _build_type in "${VALID_BUILD_TYPES[@]}"; do
    if [[ "-$jenkins_job_name-" =~ [-_]${_build_type}[-_] ]]; then
      log "Using build type '$_build_type' based on Jenkins job name '$JOB_NAME'."
      readonly build_type=$_build_type
      return
    fi
  done
  readonly build_type
  log "Using build type '$build_type' by default: could not determine from Jenkins job name" \
      "'$JOB_NAME'."
}

set_default_compiler_type() {
  expect_vars_to_be_set build_type
  if [[ -z ${YB_COMPILER_TYPE:-} ]]; then
    if is_mac; then
      YB_COMPILER_TYPE=clang
      adjust_compiler_type_on_mac
    elif [[ $OSTYPE =~ ^linux ]]; then
      YB_COMPILER_TYPE=clang17
    else
      fatal "Cannot set default compiler type on OS $OSTYPE"
    fi
    export YB_COMPILER_TYPE
    readonly YB_COMPILER_TYPE
  fi
}

is_clang() {
  if [[ $YB_COMPILER_TYPE == clang* ]]; then
    return 0
  else
    return 1
  fi
}

is_gcc() {
  if [[ $YB_COMPILER_TYPE == "gcc" ]]; then
    return 0
  else
    return 1
  fi
}

is_ubuntu() {
  [[ -f /etc/issue ]] && grep -q Ubuntu /etc/issue
}

set_compiler_type_based_on_jenkins_job_name() {
  if [[ -n "${YB_COMPILER_TYPE:-}" ]]; then
    if [[ -n "${JOB_NAME:-}" ]]; then
      log "The YB_COMPILER_TYPE variable is already set to '${YB_COMPILER_TYPE}', not setting it" \
          "based on the Jenkins job name."
    fi
  else
    local compiler_type
    local jenkins_job_name
    jenkins_job_name=$( echo "$JOB_NAME" | to_lowercase )
    YB_COMPILER_TYPE=""
    for compiler_type in "${VALID_COMPILER_TYPES[@]}"; do
      if [[ "-$jenkins_job_name-" =~ [-_]${compiler_type}[-_] ]]; then
        log "Setting YB_COMPILER_TYPE='$compiler_type' based on Jenkins job name '$JOB_NAME'."
        YB_COMPILER_TYPE=$compiler_type
        break
      fi
    done
    if [[ -z "$YB_COMPILER_TYPE" ]]; then
      log "Could not determine compiler type from Jenkins job name '$JOB_NAME'," \
          "will use the default."
      return
    fi
  fi
  adjust_compiler_type_on_mac
  validate_compiler_type
  readonly YB_COMPILER_TYPE
  export YB_COMPILER_TYPE
}

validate_compiler_type() {
  local compiler_type
  if [[ $# -eq 0 ]]; then
    if [[ -z ${YB_COMPILER_TYPE:-} ]]; then
      fatal "${FUNCNAME[0]} is called with no arguments but YB_COMPILER_TYPE is not set or is empty"
    fi
    compiler_type=$YB_COMPILER_TYPE
  elif [[ $# -eq 1 ]]; then
    compiler_type=$1
  else
    fatal "${FUNCNAME[0]} can only be called with 0 or 1 argument, got $# arguments: $*"
  fi

  if [[ ! $compiler_type =~ $VALID_COMPILER_TYPES_RE ]]; then
    fatal "Invalid compiler type: YB_COMPILER_TYPE='$compiler_type'" \
          "(expected one of: ${VALID_COMPILER_TYPES[*]})."
  fi
}

validate_cmake_build_type() {
  expect_num_args 1 "$@"
  local _cmake_build_type=$1
  _cmake_build_type=$( echo "$_cmake_build_type" | to_lowercase )
  if [[ ! "$_cmake_build_type" =~ $VALID_CMAKE_BUILD_TYPES_RE ]]; then
    fatal "Invalid CMake build type (what we're about to pass to our CMake build as" \
          "_cmake_build_type): '$_cmake_build_type'." \
          "Valid CMake build types are: ${VALID_CMAKE_BUILD_TYPES[*]}."
  fi
}

# This performs two configuration actions:
# - Sets cmake_build_type based on build_type. cmake_build_type is what's being passed to CMake
#   using the CMAKE_BUILD_TYPE variable. CMAKE_BUILD_TYPE can't be "asan" or "tsan".
# - Ensure the YB_COMPILER_TYPE environment variable is set. It is used by our compiler-wrapper.sh
#   script to invoke the appropriate C/C++ compiler.
set_cmake_build_type_and_compiler_type() {
  if [[ -z "${cmake_opts:-}" ]]; then
    cmake_opts=()
  fi

  if [[ -z ${build_type:-} ]]; then
    if [[ ${YB_LINKING_TYPE:-} == *-lto ]]; then
      if [[ ${yb_set_build_type_quietly:-} != "true" ]]; then
        log "Setting build type to 'release' by default (YB_LINKING_TYPE=${YB_LINKING_TYPE})"
      fi
      build_type=release
    else
      if [[ ${yb_set_build_type_quietly:-} != "true" ]]; then
        log "Setting build type to 'debug' by default"
      fi
      build_type=debug
    fi
  fi

  normalize_build_type
  # We're relying on build_type to set more variables, so make sure it does not change later.
  readonly build_type

  case "$build_type" in
    asan)
      cmake_build_type=fastdebug
    ;;
    compilecmds)
      cmake_build_type=debug
      export YB_EXPORT_COMPILE_COMMANDS=1
    ;;
    pvs)
      cmake_build_type=debug
      export YB_EXPORT_COMPILE_COMMANDS=1
      export YB_DO_NOT_BUILD_TESTS=1
      export YB_SKIP_INITIAL_SYS_CATALOG_SNAPSHOT=1
      export YB_REMOTE_COMPILATION=0
    ;;
    tsan)
      cmake_build_type=fastdebug
    ;;
    tsan_slow)
      cmake_build_type=debug
    ;;
    prof_gen|prof_use)
      cmake_build_type=release
    ;;
    *)
      cmake_build_type=$build_type
  esac
  validate_cmake_build_type "$cmake_build_type"
  readonly cmake_build_type

  set_default_compiler_type

  validate_compiler_type
  readonly YB_COMPILER_TYPE
  export YB_COMPILER_TYPE

  if [[ $build_type =~ ^(asan|tsan|tsan_slow)$ && $YB_COMPILER_TYPE == gcc* ]]; then
    fatal "Build type $build_type not supported with compiler type $YB_COMPILER_TYPE." \
          "Sanitizers are only supported with Clang."
  fi

  if [[ $build_type =~ ^(prof_gen|prof_use)$ && $YB_COMPILER_TYPE == gcc* ]]; then
    fatal "Build type $build_type not supported with compiler type $YB_COMPILER_TYPE." \
          "PGO works only with Clang for now."
  fi

  # We need to set CMAKE_C_COMPILER and CMAKE_CXX_COMPILER outside of CMake. We used to do that from
  # CMakeLists.txt, and got into an infinite loop where CMake kept saying:
  #
  #   You have changed variables that require your cache to be deleted.
  #   Configure will be re-run and you may have to reset some variables.
  #   The following variables have changed:
  #   CMAKE_CXX_COMPILER= /usr/bin/c++
  #
  # Not sure why it printed the old value there, since we tried to assign it the new value, the
  # same as what's given below.
  #
  # So our new approach is to pass the correct command-line options to CMake, and still let CMake
  # use the default compiler in CLion-triggered builds.

  cmake_opts+=( "-DCMAKE_BUILD_TYPE=$cmake_build_type" )
  cmake_opts+=( "${YB_DEFAULT_CMAKE_OPTS[@]}" )
}

find_make_or_ninja_and_update_cmake_opts() {
  if using_ninja; then
    cmake_opts+=( -G Ninja )
    find_ninja_executable
    make_program=$YB_NINJA_PATH
    # make_file is used in yb_build.sh
    # shellcheck disable=SC2034
    make_file="build.ninja"
  else
    make_program="make"
    # shellcheck disable=SC2034
    make_file="Makefile"
  fi

  cmake_opts+=( "-DCMAKE_MAKE_PROGRAM=$make_program" )
}

create_mvn_repo_path_file() {
  if [[ -n ${YB_MVN_LOCAL_REPO:-} ]]; then
    local mvn_repo_file_path=$BUILD_ROOT/mvn_repo
    log "Saving YB_MVN_LOCAL_REPO ($YB_MVN_LOCAL_REPO) to $mvn_repo_file_path"
    echo "$YB_MVN_LOCAL_REPO" > "$mvn_repo_file_path"
  fi
}

set_mvn_parameters() {
  if [[ ${yb_mvn_parameters_already_set} == "true" ]]; then
    return
  fi
  if is_jenkins; then
    local m2_repository_in_build_root=$BUILD_ROOT/m2_repository
    if [[ $is_run_test_script == "true" && -d $m2_repository_in_build_root ]]; then
      YB_MVN_LOCAL_REPO=$m2_repository_in_build_root
      # Do not use the "shared Maven settings" path even if it is available.
      YB_MVN_SETTINGS_PATH=$YB_DEFAULT_MVN_SETTINGS_PATH
      log "Will use Maven repository from build root ($YB_MVN_LOCAL_REPO) and the" \
          "default Maven settings path ($YB_MVN_SETTINGS_PATH)"
    elif [[ -z ${YB_MVN_SETTINGS_PATH:-} ]]; then
      export YB_MVN_SETTINGS_PATH=$YB_SHARED_MVN_SETTINGS_PATH
      log "Will use shared Maven settings file ($YB_MVN_SETTINGS_PATH)."
    fi
  fi

  if [[ -z ${YB_MVN_LOCAL_REPO:-} ]]; then
    YB_MVN_LOCAL_REPO=$YB_DEFAULT_MVN_LOCAL_REPO
  fi
  export YB_MVN_LOCAL_REPO

  if [[ -z ${YB_MVN_SETTINGS_PATH:-} ]]; then
    YB_MVN_SETTINGS_PATH=$YB_DEFAULT_MVN_SETTINGS_PATH
  fi
  export YB_MVN_SETTINGS_PATH

  mvn_common_options=(
    "--batch-mode"
    "-DbinDir=$BUILD_ROOT/bin"
    "-Dmaven.repo.local=$YB_MVN_LOCAL_REPO"
    "-Dyb.thirdparty.dir=$YB_THIRDPARTY_DIR"
  )
  log "The result of set_mvn_parameters:" \
      "YB_MVN_LOCAL_REPO=$YB_MVN_LOCAL_REPO," \
      "YB_MVN_SETTINGS_PATH=$YB_MVN_SETTINGS_PATH"
  create_mvn_repo_path_file
  yb_mvn_parameters_already_set=true
}

# Put a retry loop here since it is possible that multiple concurrent builds will try to do this
# at the same time. However, this should converge quickly.
rsync_with_retries() {
  declare -i attempt=1
  declare -i -r max_attempts=5
  while true; do
    if ( set -x; rsync "$@" ); then
      return
    fi
    if [[ $attempt -eq $max_attempts ]]; then
      log "rsync failed after $max_attempts attempts, giving up"
      return 1
    fi
    log "This was rsync attempt $attempt out of $max_attempts. Re-trying after a delay."
    sleep 1
    (( attempt+=1 ))
  done
}

# Appends the settings path specified by $YB_MVN_SETTINGS_PATH (in case that path exists), as well
# as other common Maven options used across all invocations of Maven, to the mvn_opts array. The
# caller of this function usually declares mvn_opts as a local array.
append_common_mvn_opts() {
  mvn_opts+=(
    "${mvn_common_options[@]}"
  )
  if [[ -f $YB_MVN_SETTINGS_PATH ]]; then
    mvn_opts+=(
      --settings "$YB_MVN_SETTINGS_PATH"
    )
  elif [[ $YB_MVN_SETTINGS_PATH != $HOME/.m2/settings.xml ]]; then
    log "Non-default maven user settings file specified by YB_MVN_SETTINGS_PATH does not exist:" \
        "'$YB_MVN_SETTINGS_PATH'"
  fi
}

# A utility function called by both 'build_yb_java_code' and 'build_yb_java_code_with_retries'.
build_yb_java_code_filter_save_output() {
  set_mvn_parameters
  local msg_prefix="Building Java code in $PWD"
  if [[ -n ${java_code_build_purpose:-} ]]; then
    log "$msg_prefix for $java_code_build_purpose"
  else
    log "$msg_prefix"
  fi

  # --batch-mode hides download progress.
  # We are filtering out some patterns from Maven output, e.g.:
  # [INFO] META-INF/NOTICE already added, skipping
  # [INFO] Downloaded: https://repo.maven.apache.org/maven2/org/codehaus/plexus/plexus-classworlds/2.4/plexus-classworlds-2.4.jar (46 KB at 148.2 KB/sec)
  # [INFO] Downloading: https://repo.maven.apache.org/maven2/org/apache/maven/doxia/doxia-logging-api/1.1.2/doxia-logging-api-1.1.2.jar
  local has_local_output=false # default is output path variable is set by calling function
  if [[ -z ${java_build_output_path:-} ]]; then
    local java_build_output_path
    java_build_output_path=/tmp/yb-java-build-$( get_timestamp ).$$.tmp
    has_local_output=true
  fi
  local -a mvn_opts=()
  append_common_mvn_opts
  if ! is_jenkins; then
    mvn_opts+=( -Dmaven.javadoc.skip )
  fi
  set +e -x  # +e: do not fail on grep failure, -x: print the command to stderr.
  if mvn "${mvn_opts[@]}" "$@" 2>&1 | \
      grep -Ev --line-buffered "$MVN_OUTPUT_FILTER_REGEX" | \
      tee "$java_build_output_path"; then
    set +x # stop printing commands
    # We are testing for mvn build failure with grep, since we run mvn with '--fail-never' which
    # always returns success. '--fail-at-end' could have been another possibility, but that mode
    # skips dependent modules so most tests are often not run. Therefore, we resort to grep.
    grep -Eq "BUILD SUCCESS" "$java_build_output_path"
    local mvn_exit_code=$?
    set -e
    if [[ $has_local_output == "true" ]]; then
      rm -f "$java_build_output_path" # cleaning up
    fi
    if [[ $mvn_exit_code -eq 0 ]]; then
      log "Java build SUCCEEDED"
    else
      # Useful for searching in console output.
      log "Java build FAILED: could not find 'BUILD SUCCESS' in Maven output"
    fi
    return $mvn_exit_code
  fi
  set -e +x
  log "Java build or one of its output filters failed"
  if [[ -f $java_build_output_path ]]; then
    log "Java build output (from '$java_build_output_path'):"
    cat "$java_build_output_path"
    log "(End of Java build output)"
    rm -f "$java_build_output_path"
  else
    log "Java build output path file not found at '$java_build_output_path'"
  fi
  return 1
}

build_yb_java_code() {
  local java_build_output_path
  java_build_output_path=/tmp/yb-java-build-$( get_timestamp ).$$.tmp
  build_yb_java_code_filter_save_output "$@"
  local mvn_exit_code=$?
  rm -f "$java_build_output_path"
  return $mvn_exit_code
}

build_yb_java_code_with_retries() {
  local java_build_output_path
  java_build_output_path=/tmp/yb-java-build-$( get_timestamp ).$$.tmp
  declare -i attempt=1

  while [[ $attempt -le $MAX_JAVA_BUILD_ATTEMPTS ]]; do
    if build_yb_java_code_filter_save_output "$@"; then
      rm -f "$java_build_output_path"
      return 0
    fi

    if grep "Could not transfer artifact" "$java_build_output_path" >/dev/null; then
      log "Java build attempt $attempt failed due to temporary connectivity issues, re-trying."
    else
      return 1
    fi

    rm -f "$java_build_output_path"

    (( attempt+=1 ))
  done
  return 1
}

build_yb_java_code_in_all_dirs() {
  local java_project_dir
  for java_project_dir in "${yb_java_project_dirs[@]}"; do
    pushd "$java_project_dir"
    if ! time build_yb_java_code_with_retries "$@"; then
      log "Failed to build Java code in directory '$java_project_dir'" \
          "with these Maven arguments: $*"
      return 1
    fi
    # shellcheck disable=SC2119
    popd +0
  done
}

# Skip the most part of the normal C++ build output. Still keep the "100%" lines so we can see
# if the build runs to completion. This only filters stdin, so it is expected that stderr is
# redirected to stdout when invoking the C++ build.
filter_boring_cpp_build_output() {
  # For Ninja, keep every 10th successful C/C++ compilation message.
  grep -Ev --line-buffered "\
^(\[ *[0-9]{1,2}%\] +)*(\
Building C(XX)? object |\
Running C[+][+] protocol buffer compiler (with YRPC plugin )?on |\
Linking CXX ((static|shared )?library|executable) |\
Built target \
)|\
Scanning dependencies of target |\
^ssh: connect to host .* port [0-9]+: Connection (timed out|refused)|\
Host .* seems to be down, retrying on a different host|\
Connection to .* closed by remote host.|\
ssh: Could not resolve hostname build-workers-.*: Name or service not known|\
^\[[0-9]+?[1-9]/[0-9]+\] (\
Building CXX object|\
Running C[+][+] protocol buffer compiler|\
Linking CXX shared library|\
Linking CXX executable)"
}

put_path_entry_first() {
  expect_num_args 1 "$@"
  local path_entry=$1
  remove_path_entry "$path_entry"
  export PATH=$path_entry:$PATH
}

add_path_entry_last() {
  expect_num_args 1 "$@"
  local path_entry=$1
  if [[ $PATH != *:$path_entry && $PATH != $path_entry:* && $PATH != *:$path_entry:* ]]; then
    export PATH+=:$path_entry
  fi
}

# Removes the ccache wrapper directory from PATH so we can find the real path to a compiler, e.g.
# /usr/bin/gcc instead of /usr/lib64/ccache/gcc.  This is expected to run in a subshell so that we
# don't make any unexpected changes to the script's PATH.
# TODO: how to do this properly on Mac OS X?
remove_ccache_dir_from_path() {
  remove_path_entry /usr/lib64/ccache
}

log_diagnostics_about_local_thirdparty() {
  if [[ $YB_THIRDPARTY_DIR == /opt/yb-build/* ]]; then
    log "[Host $(hostname)]" \
        "See diagnostic information below about subdirectories of /opt/yb-build:"
    (
      set -x +e
      ls -l /opt/yb-build/brew >&2
      ls -l /opt/yb-build/thirdparty >&2
    )
  fi
}

# Given a compiler type, e.g. gcc or clang, find the actual compiler executable (not a wrapper
# provided by ccache).  Takes into account YB_GCC_PREFIX and YB_CLANG_PREFIX variables that allow to
# use custom gcc and clang installations. Sets cc_executable and cxx_executable variables. This is
# used in compiler-wrapper.sh.
find_compiler_by_type() {
  expect_no_args "$#"
  expect_vars_to_be_set YB_COMPILER_TYPE
  if [[ -n ${YB_RESOLVED_C_COMPILER:-} && -n ${YB_RESOLVED_CXX_COMPILER:-} ]]; then
    cc_executable=$YB_RESOLVED_C_COMPILER
    cxx_executable=$YB_RESOLVED_CXX_COMPILER
    return
  fi

  validate_compiler_type "$YB_COMPILER_TYPE"
  unset cc_executable
  unset cxx_executable
  case "$YB_COMPILER_TYPE" in
    gcc|gcc5)
      if [[ -n ${YB_GCC_PREFIX:-} ]]; then
        if [[ ! -d $YB_GCC_PREFIX/bin ]]; then
          fatal "Directory YB_GCC_PREFIX/bin ($YB_GCC_PREFIX/bin) does not exist"
        fi
        cc_executable=$YB_GCC_PREFIX/bin/gcc
        cxx_executable=$YB_GCC_PREFIX/bin/g++
      elif using_linuxbrew; then
        cc_executable=$YB_LINUXBREW_DIR/bin/gcc
        cxx_executable=$YB_LINUXBREW_DIR/bin/g++
      else
        cc_executable=gcc
        cxx_executable=g++
      fi
      cc_executable+=${YB_GCC_SUFFIX:-}
      cxx_executable+=${YB_GCC_SUFFIX:-}
    ;;
    gcc*)
      local gcc_major_version=${YB_COMPILER_TYPE#gcc}
      if [[ ! $gcc_major_version =~ ^[0-9]+$ ]]; then
        fatal "Invalid GCC major version: '$gcc_major_version'" \
              "(from compiler type '$YB_COMPILER_TYPE')."
      fi
      if is_redhat_family; then
        local gcc_bin_dir
        if [[ -d /opt/rh/gcc-toolset-$gcc_major_version ]]; then
          gcc_bin_dir=/opt/rh/gcc-toolset-$gcc_major_version/root/usr/bin
        else
          gcc_bin_dir=/opt/rh/devtoolset-$gcc_major_version/root/usr/bin
        fi
        cc_executable=$gcc_bin_dir/gcc
        cxx_executable=$gcc_bin_dir/g++
        # This is needed for other tools, such as "as" (the assembler).
        put_path_entry_first "$gcc_bin_dir"
      else
        # shellcheck disable=SC2230
        cc_executable=$(which "gcc-$gcc_major_version")
        # shellcheck disable=SC2230
        cxx_executable=$(which "g++-$gcc_major_version")
      fi
    ;;
    # Default Clang compiler on macOS, or a custom Clang installation with explicitly specified
    # prefix.
    clang)
      if [[ -n ${YB_CLANG_PREFIX:-} ]]; then
        if [[ ! -d $YB_CLANG_PREFIX/bin ]]; then
          fatal "Directory \$YB_CLANG_PREFIX/bin ($YB_CLANG_PREFIX/bin) does not exist"
        fi
        cc_executable=$YB_CLANG_PREFIX/bin/clang
      elif [[ $OSTYPE =~ ^darwin ]]; then
        cc_executable=/usr/bin/clang
      else
        fatal "Cannot determine Clang executable for YB_COMPILER_TYPE=${YB_COMPILER_TYPE}"
      fi
      if [[ -z ${cxx_executable:-} ]]; then
        cxx_executable=$cc_executable++  # clang -> clang++
      fi
      cc_executable+=${YB_CLANG_SUFFIX:-}
      cxx_executable+=${YB_CLANG_SUFFIX:-}
    ;;
    # Clang of a specific version. We will download our pre-built LLVM package if necessary.
    clang*)
      if [[ -n ${YB_LLVM_TOOLCHAIN_DIR:-} ]]; then
        cc_executable=$YB_LLVM_TOOLCHAIN_DIR/bin/clang
        cxx_executable=$YB_LLVM_TOOLCHAIN_DIR/bin/clang++
      else
        local clang_prefix_candidate
        local clang_cc_compiler_basename=${YB_COMPILER_TYPE//clang/clang-}
        local clang_cxx_compiler_basename=${YB_COMPILER_TYPE//clang/clang++-}
        for clang_prefix_candidate in /usr/local/bin /usr/bin; do
          if [[ -e $clang_prefix_candidate/$clang_cc_compiler_basename &&
                -e $clang_prefix_candidate/$clang_cxx_compiler_basename ]]; then
            cc_executable="$clang_prefix_candidate/$clang_cc_compiler_basename"
            cxx_executable="$clang_prefix_candidate/$clang_cxx_compiler_basename"
            if [[ -L $cc_executable ]]; then
              cc_executable=$( readlink "$cc_executable" )
              if [[ ! $cc_executable =~ ^/ ]]; then
                cc_executable="$clang_prefix_candidate/$cc_executable"
              fi
            fi
            if [[ -L $cxx_executable ]]; then
              cxx_executable=$( readlink "$cxx_executable" )
              if [[ ! $cxx_executable =~ ^/ ]]; then
                cxx_executable="$clang_prefix_candidate/$cxx_executable"
              fi
            fi
            break
          fi
        done
      fi
    ;;
    *)
      fatal "Unknown compiler type '$YB_COMPILER_TYPE'"
  esac

  # -----------------------------------------------------------------------------------------------
  # Validate existence of compiler executables.
  # -----------------------------------------------------------------------------------------------

  local compiler_var_name
  for compiler_var_name in cc_executable cxx_executable; do
    if [[ -n ${!compiler_var_name:-} ]]; then
      local compiler_path=${!compiler_var_name}
      if [[ ! -x $compiler_path && $compiler_path =~ ^[a-z+]+$ ]]; then
        # This is a plain "gcc/g++/clang/clang++" compiler command name. Try to find the exact
        # compiler path using the "which" command.
        set +e
        # shellcheck disable=SC2230
        compiler_path=$( remove_ccache_dir_from_path && which "${!compiler_var_name}" )
        # shellcheck disable=SC2181
        if [[ $? -ne 0 ]]; then
          # "which" did not work, revert to the old value.
          compiler_path=${!compiler_var_name}
        fi
        set -e
      fi

      if [[ ! -x $compiler_path ]]; then
        log_diagnostics_about_local_thirdparty
        fatal "[Host $(hostname)]" \
              "Compiler does not exist or is not executable at the path we set" \
              "$compiler_var_name to" \
              "(possibly applying 'which' expansion): $compiler_path" \
              "(trying to use compiler type '$YB_COMPILER_TYPE')."
      fi
      eval $compiler_var_name=\""$compiler_path"\"
    fi
  done

  export YB_RESOLVED_C_COMPILER=$cc_executable
  export YB_RESOLVED_CXX_COMPILER=$cxx_executable
}

# Make pushd and popd quiet.
# http://stackoverflow.com/questions/25288194/dont-display-pushd-popd-stack-accross-several-bash-scripts-quiet-pushd-popd
pushd() {
  local dir_name=$1
  if [[ ! -d $dir_name ]]; then
    fatal "Directory '$dir_name' does not exist"
  fi
  command pushd "$@" > /dev/null
}

# shellcheck disable=SC2120
popd() {
  command popd "$@" > /dev/null
}

# Creates files such as thirdparty_url.txt, thirdparty_path.txt, linuxbrew_path.txt in the build
# directory. This is only being done if the file does not exist.
save_var_to_file_in_build_dir() {
  expect_num_args 2 "$@"
  local value=$1
  if [[ -z ${value:-} ]]; then
    return
  fi

  local file_name=$2
  if [[ -z ${BUILD_ROOT:-} ]]; then
    fatal "BUILD_ROOT is not set"
  fi
  if [[ ! -f $BUILD_ROOT/$file_name ]]; then
    if [[ ! -d $BUILD_ROOT ]]; then
      mkdir -p "$BUILD_ROOT"
    fi
    if ! echo "$value" >"$BUILD_ROOT/$file_name"; then
      fatal "Could not save value '$value' to file '$BUILD_ROOT/$file_name'"
    fi
  fi
}

get_thirdparty_archive_name() {
  expect_num_args 1 "$@"

  local url=$1
  if [[ "$url" == *.tar.gz ]]; then
    local tar_gz_name=${url##*/}
    thirdparty_archive_name="${tar_gz_name%.tar.gz}"
  else
    local zip_removed=${url%/zip}
    thirdparty_archive_name="${zip_removed##*/}"
  fi
}

# -------------------------------------------------------------------------------------------------
# Downloading third-party dependencies from GitHub releases
# -------------------------------------------------------------------------------------------------

download_and_extract_archive() {
  expect_num_args 2-3 "$@"
  extracted_dir=""

  local url=$1
  local dest_dir_parent=$2
  local checksum_url=${3:-}

  local thirdparty_archive_name
  get_thirdparty_archive_name "$url"

  local install_dir_name=$thirdparty_archive_name
  local dest_dir=$dest_dir_parent/$install_dir_name
  if [[ ! -d $dest_dir && ! -L $dest_dir ]]; then
    if [[ ! -d $YB_DOWNLOAD_LOCKS_DIR ]]; then
      ( umask 0; mkdir -p "$YB_DOWNLOAD_LOCKS_DIR" )
    fi
    (
      umask 0
      lock_path=$YB_DOWNLOAD_LOCKS_DIR/$install_dir_name
      (
        "$FLOCK" -w "$YB_DOWNLOAD_LOCK_TIMEOUT_SEC" 200
        if [[ ! -d $dest_dir && ! -L $dest_dir ]]; then
          log "[Host $(hostname)] $FLOCK_MSG: $lock_path, proceeding with archive installation."
          (
            set -x
            "$YB_SCRIPT_PATH_DOWNLOAD_AND_EXTRACT_ARCHIVE" \
              --url "$url" \
              --checksum-url "$checksum_url" \
              --dest-dir-parent "$dest_dir_parent" \
              --local-cache-dir "$LOCAL_DOWNLOAD_DIR"
          )
        else
          log "[Host $(hostname)] $FLOCK_MSG $lock_path but directory $dest_dir already exists."
        fi
      ) 200>"$lock_path"
    )
  fi
  extracted_dir=$dest_dir
}

download_thirdparty() {
  if [[ ! -w $OPT_YB_BUILD_DIR ]]; then
    echo >&2 "
  ERROR:  Cannot download pre-built thirdparty dependencies.
          Due to embedded paths, they must be installed under: $OPT_YB_BUILD_DIR

          Option 1) To enable downloading: sudo mkdir -m 777 $OPT_YB_BUILD_DIR

          Option 2) To build dependencies from source, use build option --ndltp
    "
    fatal "Cannot download pre-built thirdparty dependencies."
  fi
  download_and_extract_archive "$YB_THIRDPARTY_URL" "$LOCAL_THIRDPARTY_DIR_PARENT" \
                               "$YB_THIRDPARTY_CHECKSUM_URL"
  if [[ -n ${YB_THIRDPARTY_DIR:-} &&
        $YB_THIRDPARTY_DIR != "$extracted_dir" ]]; then
    log_thirdparty_and_toolchain_details
    fatal "YB_THIRDPARTY_DIR is already set to '$YB_THIRDPARTY_DIR', cannot set it to" \
          "'$extracted_dir'"
  fi
  export YB_THIRDPARTY_DIR=$extracted_dir
  yb_thirdparty_dir_origin="downloaded from $YB_THIRDPARTY_URL"
  save_thirdparty_info_to_build_dir
  download_toolchain
}

create_llvm_toolchain_symlink() {
  local symlink_path=${BUILD_ROOT}/toolchain
  if [[ ${YB_SKIP_LLVM_TOOLCHAIN_SYMLINK_CREATION:-0} != "1" &&
        -n ${YB_LLVM_TOOLCHAIN_DIR:-} &&
        ! -L ${symlink_path} ]]; then
    if ! ln -s "${YB_LLVM_TOOLCHAIN_DIR}" "${symlink_path}" &&
       # If someone else created this symlink in the meantime, that's OK.
       [[ ! -L ${symlink_path} ]]; then
      fatal "Could not create symlink from ${symlink_path} to ${YB_LLVM_TOOLCHAIN_DIR}"
    fi
  fi
}

download_toolchain() {
  expect_vars_to_be_set YB_COMPILER_TYPE YB_THIRDPARTY_DIR
  local toolchain_urls=()
  local linuxbrew_url=""
  if [[ -n ${YB_THIRDPARTY_DIR:-} && -f "$YB_THIRDPARTY_DIR/linuxbrew_url.txt" ]]; then
    local linuxbrew_url_file_path="${YB_THIRDPARTY_DIR}/linuxbrew_url.txt"
    linuxbrew_url="$(<"${linuxbrew_url_file_path}")"
  elif [[ -n ${YB_THIRDPARTY_URL:-} && ${YB_THIRDPARTY_URL##*/} == *linuxbrew* ||
          -n ${YB_THIRDPARTY_DIR:-} && ${YB_THIRDPARTY_DIR##*/} == *linuxbrew* ]]; then
    # TODO: get rid of the hard-coded URL below and always include linuxbrew_url.txt in the
    # thirdparty archives that are built for Linuxbrew.
    linuxbrew_url="https://github.com/yugabyte/brew-build/releases/download/"
    linuxbrew_url+="20181203T161736v9/linuxbrew-20181203T161736v9.tar.gz"
  fi

  if [[ -n ${linuxbrew_url:-} ]]; then
    toolchain_urls+=( "$linuxbrew_url" )
  fi
  if [[ -z ${YB_LLVM_TOOLCHAIN_URL:-} &&
        -z ${YB_LLVM_TOOLCHAIN_DIR:-} &&
        ${YB_COMPILER_TYPE:-} =~ ^clang[0-9]+$ ]]; then
    local llvm_major_version=${YB_COMPILER_TYPE#clang}
    if [[ ${build_type} =~ ^(asan|tsan)$ ]]; then
      # For ASAN and possibly TSAN builds, we need to use the same LLVM toolchain that was used
      # to build the third-party dependencies, so that the compiler-rt libraries match.
      local thirdparty_llvm_url_file_path=${YB_THIRDPARTY_DIR}/toolchain_url.txt
      if [[ -e $thirdparty_llvm_url_file_path ]]; then
        YB_LLVM_TOOLCHAIN_URL=$(<"$thirdparty_llvm_url_file_path")
        if [[ ${YB_LLVM_TOOLCHAIN_URL} != */yb-llvm-v${llvm_major_version}.* ]]; then
          fatal "LLVM toolchain URL ${YB_LLVM_TOOLCHAIN_URL} from the third-party directory" \
                "${YB_THIRDPARTY_DIR} does not match the compiler type ${YB_COMPILER_TYPE}:" \
                "${YB_LLVM_TOOLCHAIN_URL}"
        fi
      else
        log "Warning: could not find ${thirdparty_llvm_url_file_path}, will try to use the" \
            "llvm-installer utility to determine the LLVM toolchain URL to download. Note that a" \
            "mismatch between LLVM versions used to build yugabyte-db-thirdparty and YugabyteDB" \
            "can cause ASAN/TSAN tests to fail."
      fi
    fi

    if [[ -z ${YB_LLVM_TOOLCHAIN_URL:-} ]]; then
      YB_LLVM_TOOLCHAIN_URL=$(
        activate_virtualenv &>/dev/null
        python3 -m llvm_installer --print-url "--llvm-major-version=$llvm_major_version"
      )
    fi
    if [[ ${YB_LLVM_TOOLCHAIN_URL} != https://* ]]; then
      fatal "Failed to determine LLVM toolchain URL using the llvm-installer utility." \
            "YB_LLVM_TOOLCHAIN_URL=${YB_LLVM_TOOLCHAIN_URL}. See" \
            "https://github.com/yugabyte/llvm-installer for details."
    fi
    export YB_LLVM_TOOLCHAIN_URL
  fi
  if [[ -n ${YB_LLVM_TOOLCHAIN_URL:-} ]]; then
    toolchain_urls+=( "${YB_LLVM_TOOLCHAIN_URL}" )
  fi
  create_llvm_toolchain_symlink

  if [[ ${#toolchain_urls[@]} -eq 0 ]]; then
    return
  fi

  for toolchain_url in "${toolchain_urls[@]}"; do
    local toolchain_url_basename=${toolchain_url##*/}
    local is_llvm=false
    local is_linuxbrew=false
    if [[ $toolchain_url_basename == yb-llvm-* ]]; then
      toolchain_dir_parent=$TOOLCHAIN_PARENT_DIR_LLVM
      is_llvm=true
    elif [[ $toolchain_url_basename =~ ^(yb-)?linuxbrew-.*$ ]]; then
      toolchain_dir_parent=$TOOLCHAIN_PARENT_DIR_LINUXBREW
      is_linuxbrew=true
    else
      fatal "Unable to determine the installation parent directory for the toolchain archive" \
            "named '$toolchain_url_basename'. Toolchain URL: '${toolchain_url}'."
    fi

    download_and_extract_archive "$toolchain_url" "$toolchain_dir_parent"
    if [[ ${is_linuxbrew} == "true" ]]; then
      if [[ -n ${YB_LINUXBREW_DIR:-} &&
            $YB_LINUXBREW_DIR != "$extracted_dir" ]]; then
        log_thirdparty_and_toolchain_details
        fatal "YB_LINUXBREW_DIR is already set to '$YB_LINUXBREW_DIR', cannot set it to" \
              "'$extracted_dir'"
      fi
      export YB_LINUXBREW_DIR=$extracted_dir
      yb_linuxbrew_dir_origin="downloaded from $toolchain_url"
      save_brew_path_to_build_dir
    fi

    if [[ ${is_llvm} == "true" ]]; then
      if [[ -n ${YB_LLVM_TOOLCHAIN_DIR:-} &&
            ${YB_LLVM_TOOLCHAIN_DIR} != "${extracted_dir}" ]]; then
        if [[ ${YB_LLVM_TOOLCHAIN_MISMATCH_WARNING_LOGGED:-0} == "0" &&
              ${YB_SUPPRESS_LLVM_TOOLCHAIN_MISMATCH_WARNING:-0} != "1" ]]; then
          log_thirdparty_and_toolchain_details
          log "Warning: YB_LLVM_TOOLCHAIN_DIR is already set to '${YB_LLVM_TOOLCHAIN_DIR}'," \
              "cannot set it to '${extracted_dir}'. This may happen in case the LLVM toolchain" \
              "version used to build third-party dependencies is different from the one we are" \
              "using now to build YugabyteDB, normally determined by the llvm-installer Python" \
              "module. To fix this permanently, third-party dependencies should be rebuilt using" \
              "our most recent build of the LLVM toolchain for this major version, but in most" \
              "cases this is not a problem. To suppress this warning, set the" \
              "YB_SUPPRESS_LLVM_TOOLCHAIN_MISMATCH_WARNING env var to 1."
          export YB_LLVM_TOOLCHAIN_MISMATCH_WARNING_LOGGED=1
        fi
      else
        export YB_LLVM_TOOLCHAIN_DIR=$extracted_dir
        yb_llvm_toolchain_dir_origin="downloaded from $toolchain_url"
        save_llvm_toolchain_info_to_build_dir
      fi
    fi
  done
}

# -------------------------------------------------------------------------------------------------
# Detecting Homebrew/Linuxbrew
# -------------------------------------------------------------------------------------------------

disable_linuxbrew() {
  export YB_USE_LINUXBREW=0
  unset YB_LINUXBREW_DIR
}

detect_toolchain() {
  detect_brew
  detect_llvm_toolchain
}

detect_brew() {
  if [[ ${YB_USE_LINUXBREW:-} == "0" ]]; then
    disable_linuxbrew
    return
  fi
  if is_linux; then
    local cpu_type
    cpu_type=$( uname --processor )
    if [[ $cpu_type == "x86_64" ]]; then
      detect_linuxbrew
    else
      disable_linuxbrew
    fi
  fi
}

try_set_linuxbrew_dir() {
  local linuxbrew_dir=$1
  if [[ -d "$linuxbrew_dir" &&
        -d "$linuxbrew_dir/bin" &&
        -d "$linuxbrew_dir/lib" &&
        -d "$linuxbrew_dir/include" ]]; then
    YB_LINUXBREW_DIR=$(realpath "$linuxbrew_dir")
    export YB_LINUXBREW_DIR
    save_brew_path_to_build_dir
    return 0
  else
    return 1
  fi
}

wait_for_directory_existence() {
  expect_num_args 1 "$@"
  local dir_path=$1
  declare -i attempt=0
  while [[ ! -d $dir_path ]]; do
    if [[ $attempt -ge $DIRECTORY_EXISTENCE_WAIT_TIMEOUT_SEC ]]; then
      fatal "Gave up waiting for directory '$dir_path' to appear after $attempt seconds"
    fi
    log "Directory '$dir_path' not found, waiting for it to mount"
    (( attempt+=1 ))
    sleep 1
  done
}

save_brew_path_to_build_dir() {
  if is_linux; then
    save_var_to_file_in_build_dir "${YB_LINUXBREW_DIR:-}" "linuxbrew_path.txt"
  fi
}

save_llvm_toolchain_info_to_build_dir() {
  if is_linux; then
    save_var_to_file_in_build_dir "${YB_LLVM_TOOLCHAIN_DIR:-}" "llvm_path.txt"
    save_var_to_file_in_build_dir "${YB_LLVM_TOOLCHAIN_URL:-}" "llvm_url.txt"
  fi
}

save_thirdparty_info_to_build_dir() {
  save_var_to_file_in_build_dir "${YB_THIRDPARTY_DIR:-}" "thirdparty_path.txt"
  save_var_to_file_in_build_dir "${YB_THIRDPARTY_URL:-}" "thirdparty_url.txt"
}

save_paths_and_archive_urls_to_build_dir() {
  save_brew_path_to_build_dir
  save_thirdparty_info_to_build_dir
  save_llvm_toolchain_info_to_build_dir
}

detect_linuxbrew() {
  expect_vars_to_be_set YB_COMPILER_TYPE
  if ! is_linux; then
    return
  fi
  if [[ -n ${YB_LINUXBREW_DIR:-} ]]; then
    export YB_LINUXBREW_DIR
    return
  fi
  if [[ -n ${YB_USE_LINUXBREW:-} && ${YB_USE_LINUXBREW:-} != "1" ]]; then
    return
  fi

  if [[ ${is_clean_build} != "true" &&
        -n ${BUILD_ROOT:-} &&
        -f $BUILD_ROOT/linuxbrew_path.txt ]]
  then
    YB_LINUXBREW_DIR=$(<"$BUILD_ROOT/linuxbrew_path.txt")
    export YB_LINUXBREW_DIR
    yb_linuxbrew_dir_origin="from file '$BUILD_ROOT/linuxbrew_path.txt')"
    return
  fi
}

detect_llvm_toolchain() {
  if [[ -n ${YB_LLVM_TOOLCHAIN_DIR:-} ]]; then
    export YB_LLVM_TOOLCHAIN_DIR
    return
  fi

  if [[ $is_clean_build != "true" && -n ${BUILD_ROOT:-} && -f $BUILD_ROOT/llvm_path.txt ]]; then
    YB_LLVM_TOOLCHAIN_DIR=$(<"$BUILD_ROOT/llvm_path.txt")
    export YB_LLVM_TOOLCHAIN_DIR
    yb_llvm_toolchain_dir_origin="from file '$BUILD_ROOT/llvm_path.txt')"
  fi
}

using_linuxbrew() {
  if is_linux && [[ -n ${YB_LINUXBREW_DIR:-} || ${YB_USE_LINUXBREW:-} == "1" ]]; then
    return 0  # true in bash
  fi
  return 1
}

ensure_linuxbrew_dir_is_set() {
  if [[ -z ${YB_LINUXBREW_DIR:-} ]]; then
    fatal "YB_LINUXBREW_DIR is not set. YB_USE_LINUXBREW=${YB_USE_LINUXBREW:-undefined}"
  fi
}

decide_whether_to_use_ninja() {
  if [[ -z ${YB_USE_NINJA:-} ]]; then
    # Autodetect whether we need to use Ninja at all, based on whether it is available.
    export YB_USE_NINJA=0
    if [[ -n ${BUILD_ROOT:-} ]]; then
      if [[ $BUILD_ROOT == *-ninja ]]; then
        export YB_USE_NINJA=1
      fi
    elif command -v ninja >/dev/null || [[ -x /usr/local/bin/ninja ]]; then
      export YB_USE_NINJA=1
    elif using_linuxbrew; then
      ensure_linuxbrew_dir_is_set
      local yb_ninja_path_candidate=$YB_LINUXBREW_DIR/bin/ninja
      if [[ -x $yb_ninja_path_candidate ]]; then
        export YB_USE_NINJA=1
      fi
    fi
  fi

  find_ninja_executable
}

find_ninja_executable() {
  if ! using_ninja || [[ -n ${YB_NINJA_PATH:-} ]] ||
     [[ ${yb_ninja_executable_not_needed:-} == "true" ]]; then
    return
  fi

  if [[ -x /usr/local/bin/ninja ]]; then
    # This will eventually be the preferred Ninja executable on all platforms in our build
    # environment.
    export YB_NINJA_PATH=/usr/local/bin/ninja
    return
  fi

  set +e
  local which_ninja
  which_ninja=$( command -v ninja )
  set -e
  if [[ -x $which_ninja ]]; then
    export YB_NINJA_PATH=$which_ninja
    return
  fi

  # We used to get Ninja from Linuxbrew here as a last resort but we don't do that anymore.

  # -----------------------------------------------------------------------------------------------
  # Ninja not found
  # -----------------------------------------------------------------------------------------------

  log "PATH: $PATH"
  if is_linux; then
    log "YB_LINUXBREW_DIR: ${YB_LINUXBREW_DIR:-undefined}"
  fi

  fatal "ninja executable not found"
}

using_ninja() {
  if [[ ${YB_USE_NINJA:-} == "1" ]]; then
    return 0
  else
    return 1
  fi
}

add_brew_bin_to_path() {
  if using_linuxbrew; then
    ensure_linuxbrew_dir_is_set
    # We need to add Linuxbrew's bin directory to PATH so that we can find the right compiler and
    # linker.
    put_path_entry_first "$YB_LINUXBREW_DIR/bin"
  fi
  # When building for arm64 on macOS, we need to make sure we find arm64 versions of various tools
  # such as automake. This is especially relevant for Postgres build.
  if is_mac; then
    local homebrew_path=""
    case "${YB_TARGET_ARCH:-}" in
      x86_64) homebrew_path="/usr/local" ;;
      arm64) homebrew_path="/opt/homebrew" ;;
    esac
    if [[ -n ${homebrew_path} ]]; then
      put_path_entry_first "$homebrew_path/bin"
    fi
  fi
}

remove_linuxbrew_bin_from_path() {
  if using_linuxbrew; then
    ensure_linuxbrew_dir_is_set
    remove_path_entry "$YB_LINUXBREW_DIR/bin"
  fi
}

detect_num_cpus() {
  if [[ ! ${YB_NUM_CPUS:-} =~ ^[0-9]+$ ]]; then
    if is_linux; then
      YB_NUM_CPUS=$(grep -c processor /proc/cpuinfo)
    elif is_mac; then
      YB_NUM_CPUS=$(sysctl -n hw.ncpu)
    else
      fatal "Don't know how to detect the number of CPUs on OS $OSTYPE."
    fi

    if [[ ! $YB_NUM_CPUS =~ ^[0-9]+$ ]]; then
      fatal "Invalid number of CPUs detected: '$YB_NUM_CPUS' (expected a number)."
    fi
  fi
}

# Gets a random build worker host name. Output variable: build_workers (array).
# shellcheck disable=SC2120
get_build_worker_list() {
  expect_no_args "$#"
  if [[ -z ${YB_BUILD_WORKERS_LIST_URL:-} ]]; then
    fatal "YB_BUILD_WORKERS_LIST_URL not set"
  fi

  declare -i attempt
  for (( attempt=1; attempt <= MAX_ATTEMPTS_TO_GET_BUILD_WORKER; attempt++ )); do
    # Note: ignoring bad exit codes and HTTP status codes here. We only look at the output and
    # if each build worker name is of the right format ("build-worker-..."), we consider it valid.
    #
    # Typically, when advice at https://github.com/koalaman/shellcheck/wiki/SC2207 is followed,
    # the code ends up being unnecessarily complex for simple word splitting where all whitespace
    # (new lines, spaces, etc.) has to be treated the same way.
    #
    # shellcheck disable=SC2207
    if [[ -n ${YB_BUILD_WORKERS_FILE:-} ]]; then
      build_workers=( $( cat "$YB_BUILD_WORKERS_FILE" ))
    else
      build_workers=( $( curl -s "$YB_BUILD_WORKERS_LIST_URL" ) ) \
        || fatal "Failed to curl $YB_BUILD_WORKERS_LIST_URL: check your network connection or use" \
                 "--no-remote"
    fi
    if [[ ${#build_workers[@]} -eq 0 ]]; then
      log "Got an empty list of build workers from $YB_BUILD_WORKERS_LIST_URL," \
          "waiting for $DELAY_ON_BUILD_WORKERS_LIST_HTTP_ERROR_SEC sec."
    else
      local build_worker_name
      local all_worker_names_valid=true
      for build_worker_name in "${build_workers[@]}"; do
        if [[ $build_worker_name != build-worker* ]]; then
          log "Got an invalid build worker name from $YB_BUILD_WORKERS_LIST_URL:" \
              "'$build_worker_name', expected all build worker names to start with" \
              "'build-worker', waiting for $DELAY_ON_BUILD_WORKERS_LIST_HTTP_ERROR_SEC sec."
          all_worker_names_valid=false
          break
        fi
      done
      if [[ ${all_worker_names_valid} == "true" ]]; then
        return
      fi
    fi
    sleep "$DELAY_ON_BUILD_WORKERS_LIST_HTTP_ERROR_SEC"
  done

  fatal "Could not get a build worker name from $YB_BUILD_WORKERS_LIST_URL in" \
        "$MAX_ATTEMPTS_TO_GET_BUILD_WORKER attempts. Last output from curl: '${build_workers[*]}'"
}

detect_num_cpus_and_set_make_parallelism() {
  detect_num_cpus
  if [[ -z ${YB_MAKE_PARALLELISM:-} ]]; then
    if [[ ${YB_REMOTE_COMPILATION:-} == "1" ]]; then
      declare -i num_build_workers
      declare -a build_workers  # This is set by the get_build_worker_list function.
      get_build_worker_list
      num_build_workers=${#build_workers[@]}
      # Add one to the number of workers so that we cause the auto-scaling group to scale up a bit
      # by stressing the CPU on each worker a bit more.
      declare -i effective_num_build_workers
      effective_num_build_workers=$(( num_build_workers + 1 ))

      # However, make sure this number is within a reasonable range.
      if [[ $effective_num_build_workers -lt $MIN_EFFECTIVE_NUM_BUILD_WORKERS ]]; then
        effective_num_build_workers=$MIN_EFFECTIVE_NUM_BUILD_WORKERS
      fi
      if [[ $effective_num_build_workers -gt $MAX_EFFECTIVE_NUM_BUILD_WORKERS ]]; then
        effective_num_build_workers=$MAX_EFFECTIVE_NUM_BUILD_WORKERS
      fi

      YB_MAKE_PARALLELISM=$(( effective_num_build_workers * YB_NUM_CORES_PER_BUILD_WORKER ))
    else
      YB_MAKE_PARALLELISM=$YB_NUM_CPUS
    fi
  fi
  export YB_MAKE_PARALLELISM
}

validate_thirdparty_dir() {
  ensure_file_exists "$YB_THIRDPARTY_DIR/build_thirdparty.sh"
  ensure_directory_exists "$YB_THIRDPARTY_DIR/installed"
}

# Detect if we're running on Google Compute Platform. We perform this check lazily as there might be
# a bit of a delay resolving the domain name.
detect_gcp() {
  # How to detect if we're running on Google Compute Engine:
  # https://cloud.google.com/compute/docs/instances/managing-instances#dmi
  if [[ -n ${YB_PRETEND_WE_ARE_ON_GCP:-} ]] || \
     curl metadata.google.internal --silent --output /dev/null --connect-timeout 1; then
    readonly is_running_on_gcp_exit_code=0  # "true" exit code
  else
    readonly is_running_on_gcp_exit_code=1  # "false" exit code
  fi
}

is_running_on_gcp() {
  if [[ -z ${is_running_on_gcp_exit_code:-} ]]; then
    detect_gcp
  fi
  return "$is_running_on_gcp_exit_code"
}

is_jenkins_user() {
  [[ $USER == "jenkins" ]]
}

is_jenkins() {
  if [[ -n ${JOB_NAME:-} ]] && is_jenkins_user; then
    return 0  # Yes, we're running on Jenkins.
  fi
  return 1  # Probably running locally.
}

should_gzip_test_logs() {
  is_jenkins || [[ ${YB_GZIP_TEST_LOGS:-0} == "1" ]]
}

# For each file provided as an argument, gzip the given file if it exists and is not already
# compressed.
gzip_if_exists() {
  local f
  for f in "$@"; do
    if [[ -f $f && $f != *.gz && $f != *.bz2 ]]; then
      gzip "$f"
    fi
  done
}

# Check if we're in a Jenkins master build.
is_jenkins_master_build() {
  if [[ -n ${JOB_NAME:-} && $JOB_NAME = *-master-* ]]; then
    return 0
  fi
  return 1
}

# Check if we're in a Jenkins Phabricator build (a pre-commit build).
is_jenkins_phabricator_build() {
  if [[ -z ${JOB_NAME:-} ]]; then
    return 1  # No, not running on Jenkins.
  fi

  if [[ $JOB_NAME == *-phabricator-* || $JOB_NAME == *-phabricator ]]; then
    return 0  # Yes, this is a Phabricator build.
  fi

  return 1  # No, some other kind of Jenkins job.
}

# Check if we're using an NFS partition in YugaByte's build environment.
is_src_root_on_nfs() {
  if [[ $YB_SRC_ROOT =~ $YB_NFS_PATH_RE ]]; then
    return 0
  fi
  return 1
}

using_remote_compilation() {
  if [[ ${YB_REMOTE_COMPILATION:-} == "1" ]]; then
    return 0  # "true" return value
  fi
  return 1  # "false" return value
}

debugging_remote_compilation() {
  [[ ${YB_DEBUG_REMOTE_COMPILATION:-undefined} == "1" ]]
}

cmd_line_to_env_vars_for_remote_cmd() {
  YB_ENCODED_REMOTE_CMD_LINE=""
  # This must match the separator in remote_cmd.sh.
  declare -r ARG_SEPARATOR=$'=:\t:='
  for arg in "$@"; do
    # The separator used here must match the separator used in remote_cmd.sh.
    YB_ENCODED_REMOTE_CMD_LINE+=$arg$ARG_SEPARATOR
  done
  # This variable must be accessible to remote_cmd.sh on the other side of ssh.
  export YB_ENCODED_REMOTE_CMD_LINE
}

run_remote_cmd() {
  local build_host=$1
  local executable=$2
  shift 2
  cmd_line_to_env_vars_for_remote_cmd "$@"
  local ssh_args=(
    "$build_host"
    "$YB_BUILD_SUPPORT_DIR/remote_cmd.sh"
    "$PWD"
    "$PATH"
    "$executable"
  )
  if debugging_remote_compilation; then
    ( set -x; ssh "${ssh_args[@]}" ) 2>&1
  else
    ssh "${ssh_args[@]}"
  fi
}

configure_remote_compilation() {
  if [[ ! ${YB_REMOTE_COMPILATION:-auto} =~ ^(0|1|auto)$ ]]; then
    fatal "Invalid value of the YB_REMOTE_COMPILATION environment variable: can be '0', '1', or" \
          "'auto'. Actual value: ${YB_REMOTE_COMPILATION:-undefined}."
  fi
  # Automatically set YB_REMOTE_COMPILATION in an NFS GCP environment.
  if [[ ${YB_REMOTE_COMPILATION:-auto} == "auto" ]]; then
    if is_running_on_gcp && is_src_root_on_nfs; then
      log "Automatically enabling remote compilation (running in an NFS GCP environment). " \
          "Use YB_REMOTE_COMPILATION=0 (or the --no-remote ybd option) to disable this behavior."
      YB_REMOTE_COMPILATION=1
    else
      YB_REMOTE_COMPILATION=0
      if is_jenkins; then
        # Make it easier to diagnose why we're not using the distributed build. Only enable this on
        # Jenkins to avoid confusing output during development.
        log "Not using remote compilation: " \
            "YB_REMOTE_COMPILATION=${YB_REMOTE_COMPILATION:-undefined}. " \
            "See additional diagnostics below."
        if is_running_on_gcp; then
          log "Running on GCP."
        else
          log "This is not GCP."
        fi
        if is_src_root_on_nfs; then
          log "YB_SRC_ROOT ($YB_SRC_ROOT) appears to be on NFS in YugaByte's distributed" \
              "build setup."
        fi
      fi
    fi
  fi
  export YB_REMOTE_COMPILATION
}

read_file_and_trim() {
  expect_num_args 1 "$@"
  local file_name=$1
  if [[ -f $file_name ]]; then
    sed 's/^[[:space:]]*//; s/[[:space:]]*$//' "$file_name"
  else
    log "File '$file_name' does not exist"
    return 1
  fi
}

# -------------------------------------------------------------------------------------------------
# Finding the third-party directory
# -------------------------------------------------------------------------------------------------

using_default_thirdparty_dir() {
  if [[ -n ${YB_THIRDPARTY_DIR:-} &&
        $YB_THIRDPARTY_DIR != "$YB_SRC_ROOT/thirdparty" ]]; then
    # YB_THIRDPARTY_DIR is specified and is not the default location
    return 1
  fi
  return 0
}

finalize_yb_thirdparty_dir() {
  if [[ -d $YB_THIRDPARTY_DIR ]]; then
    export YB_THIRDPARTY_DIR
    if ! using_default_thirdparty_dir; then
      export NO_REBUILD_THIRDPARTY=1
    fi
    # For local third-party builds we might need to download the toolchain referenced by the
    # third-party directory.
    download_toolchain
  elif [[ ! $YB_THIRDPARTY_DIR == $OPT_YB_BUILD_DIR/* ]]; then
    fatal "YB_THIRDPARTY_DIR is set to '$YB_THIRDPARTY_DIR' but it does not exist and is not" \
          "within '$OPT_YB_BUILD_DIR' so we would not be able to download it."
  fi
}

# This function can be called in the beginning of a build, but also by various scripts that
# participate in the build, e.g. run-test.sh and compiler-wrapper.sh, so it is important to handle
# the case where the third-party dependencies and toolchain are already downloaded efficiently.
find_or_download_thirdparty() {
  if [[ -z ${YB_COMPILER_TYPE:-} ]]; then
    fatal "YB_COMPILER_TYPE is not set"
  fi

  # Fast path: YB_THIRDPARTY_DIR set and it exists.
  if [[ -n ${YB_THIRDPARTY_DIR:-} ]]; then
    finalize_yb_thirdparty_dir
    if [[ -d $YB_THIRDPARTY_DIR ]]; then
      if [[ ! -f "${BUILD_ROOT}/thirdparty_path.txt" ]]; then
        save_thirdparty_info_to_build_dir
      fi
      return
    fi
  fi

  if [[ $is_clean_build == "true" ]]; then
    log "This is a clean build, not loading thirdparty URL or path from files in the build" \
        "directory."
  else
    if [[ -f $BUILD_ROOT/thirdparty_path.txt ]]; then
      local thirdparty_dir_from_file
      thirdparty_dir_from_file=$(<"$BUILD_ROOT/thirdparty_path.txt")
      if [[ -n ${YB_THIRDPARTY_DIR:-} && "$YB_THIRDPARTY_DIR" != "$thirdparty_dir_from_file" ]]
      then
        fatal "YB_THIRDPARTY_DIR is explicitly set to '$YB_THIRDPARTY_DIR' but file" \
              "'$BUILD_ROOT/thirdparty_path.txt' contains '$thirdparty_dir_from_file'"
      fi
      export YB_THIRDPARTY_DIR=$thirdparty_dir_from_file
      yb_thirdparty_dir_origin="from file '$BUILD_ROOT/thirdparty_path.txt')"

      # Check if we've succeeded in setting YB_THIRDPARTY_DIR now.
      if [[ -n ${YB_THIRDPARTY_DIR:-} ]]; then
        finalize_yb_thirdparty_dir
        if [[ -d $YB_THIRDPARTY_DIR ]]; then
          return
        fi
      fi
    fi

    if [[ -f $BUILD_ROOT/thirdparty_checksum_url.txt ]]; then
      local thirdparty_checksum_url_from_file
      thirdparty_checksum_url_from_file=$(<"$BUILD_ROOT/thirdparty_checksum_url.txt")
      if [[ -n ${YB_THIRDPARTY_CHECKSUM_URL:-} &&
            "$YB_THIRDPARTY_CHECKSUM_URL" != "$thirdparty_checksum_url_from_file" ]]; then
        fatal "YB_THIRDPARTY_CHECKSUM_URL is explicitly set to '$YB_THIRDPARTY_CHECKSUM_URL' but " \
              "file '$BUILD_ROOT/thirdparty_checksum_url.txt' contains " \
              "'$thirdparty_checksum_url_from_file'"
      fi
      export YB_THIRDPARTY_CHECKSUM_URL=$thirdparty_checksum_url_from_file
      yb_thirdparty_checksum_url_origin="from file '$BUILD_ROOT/thirdparty_checksum_url.txt')"
      if [[ ${YB_DOWNLOAD_THIRDPARTY:-} == "0" ]]; then
        fatal "YB_DOWNLOAD_THIRDPARTY is explicitly set to 0 but file" \
              "$BUILD_ROOT/thirdparty_checksum_url.txt exists"
      fi
    fi

    if [[ -f $BUILD_ROOT/thirdparty_url.txt ]]; then
      local thirdparty_url_from_file
      thirdparty_url_from_file=$(<"$BUILD_ROOT/thirdparty_url.txt")
      if [[ -n ${YB_THIRDPARTY_URL:-} &&
            "$YB_THIRDPARTY_URL" != "$thirdparty_url_from_file" ]]; then
        fatal "YB_THIRDPARTY_URL is explicitly set to '$YB_THIRDPARTY_URL' but file" \
              "'$BUILD_ROOT/thirdparty_url.txt' contains '$thirdparty_url_from_file'"
      fi
      export YB_THIRDPARTY_URL=$thirdparty_url_from_file
      if [[ -z ${YB_THIRDPARTY_CHECKSUM_URL:-} ]]; then
        export YB_THIRDPARTY_CHECKSUM_URL="$YB_THIRDPARTY_URL.sha256"
      fi
      yb_thirdparty_url_origin="from file '$BUILD_ROOT/thirdparty_url.txt')"
      if [[ ${YB_DOWNLOAD_THIRDPARTY:-} == "0" ]]; then
        fatal "YB_DOWNLOAD_THIRDPARTY is explicitly set to 0 but file" \
              "$BUILD_ROOT/thirdparty_url.txt exists"
      fi
      export YB_DOWNLOAD_THIRDPARTY=1
    fi
  fi

  # Even if YB_THIRDPARTY_DIR is set but it does not exist, it is possible that we need to download
  # the third-party archive.

  if [[ ${YB_DOWNLOAD_THIRDPARTY:-} == "1" ]]; then
    set_prebuilt_thirdparty_url
    download_thirdparty
    export NO_REBUILD_THIRDPARTY=1
    log "Using downloaded third-party directory: $YB_THIRDPARTY_DIR"
    if using_linuxbrew; then
      log "Using Linuxbrew directory: ${YB_LINUXBREW_DIR:-undefined}"
    fi
  fi

  if [[ -z ${YB_THIRDPARTY_DIR:-} ]]; then
    export YB_THIRDPARTY_DIR=$YB_SRC_ROOT/thirdparty
    yb_thirdparty_dir_origin="default"
  fi
  save_thirdparty_info_to_build_dir
}

find_or_download_ysql_snapshots() {
  local repo_url="https://github.com/yugabyte/yugabyte-db-ysql-catalog-snapshots"
  local prefix="initial_sys_catalog_snapshot"

  mkdir -p "$YSQL_SNAPSHOTS_DIR_PARENT"

  # Just one snapshot for now.
  # (disabling a code checker error about a singular loop iteration)
  # shellcheck disable=SC2043
  for ver in "2.25.0.0-pg15-alpha-2"; do
    for bt in "release" "sanitizers" "mac"; do
      local name="${prefix}_${ver}_${bt}"
      if [[ ! -d "$YSQL_SNAPSHOTS_DIR_PARENT/$name" ]]; then
        local url="${repo_url}/releases/download/v${ver}/${name}.tar.gz"
        download_and_extract_archive "$url" "$YSQL_SNAPSHOTS_DIR_PARENT"
      fi
    done
  done
}

log_env_var() {
  expect_num_args 2 "$@"
  local env_var_name=$1
  local env_var_value=${!env_var_name:-}
  if [[ -z ${env_var_value} ]]; then
    return
  fi
  local description=$2
  if [[ -n ${description} ]]; then
    description=" (${description})"
  fi
  echo "    ${env_var_name}: ${env_var_value}${description}"
}

log_thirdparty_and_toolchain_details() {
  (
    echo "Details of third-party dependencies:"
    log_env_var YB_THIRDPARTY_DIR "${yb_thirdparty_dir_origin}"
    log_env_var YB_THIRDPARTY_URL "${yb_thirdparty_url_origin}"
    log_env_var YB_THIRDPARTY_CHECKSUM_URL "${yb_thirdparty_checksum_url_origin}"

    if is_linux; then
      log_env_var YB_LINUXBREW_DIR "${yb_linuxbrew_dir_origin}"
    fi
    log_env_var YB_LLVM_TOOLCHAIN_URL "${yb_llvm_toolchain_url_origin}"
    log_env_var YB_LLVM_TOOLCHAIN_DIR "${yb_llvm_toolchain_dir_origin}"
    log_env_var YB_DOWNLOAD_THIRDPARTY ""
    log_env_var NO_REBUILD_THIRDPARTY ""
  ) >&2
}

handle_predefined_build_root_quietly=false

# shellcheck disable=SC2120
handle_predefined_build_root() {
  expect_no_args "$#"
  if [[ -z ${predefined_build_root:-} ]]; then
    return
  fi

  if [[ -L $predefined_build_root ]]; then
    predefined_build_root=$( readlink "$predefined_build_root" )
  fi

  if [[ -d $predefined_build_root ]]; then
    predefined_build_root=$( cd "$predefined_build_root" && pwd )
  fi

  # Sometimes $predefined_build_root contains symlinks on its path.
  local expanded_build_root
  expanded_build_root=$(realpath -q "$predefined_build_root")
  if [[ "${expanded_build_root}" != "$(realpath -q "$YB_BUILD_INTERNAL_PARENT_DIR")"/* && \
        "${expanded_build_root}" != "$(realpath -q "$YB_BUILD_EXTERNAL_PARENT_DIR")"/* ]]
    then
    fatal "Build root '$predefined_build_root' is not within either " \
          "'$YB_BUILD_INTERNAL_PARENT_DIR' or '$YB_BUILD_EXTERNAL_PARENT_DIR'"
  fi

  local basename=${predefined_build_root##*/}

  if [[ $basename =~ $BUILD_ROOT_BASENAME_RE ]]; then
    local group_idx=1
    local _build_type=${BASH_REMATCH[$group_idx]}

    (( group_idx+=1 ))
    local _compiler_type=${BASH_REMATCH[$group_idx]}

    (( group_idx+=1 ))
    local _dash_linuxbrew=${BASH_REMATCH[$group_idx]}

    (( group_idx+=2 ))
    # shellcheck disable=SC2034
    local _linking_type=${BASH_REMATCH[$group_idx]}

    # The architecture field is of the form (-(...))?, so it utilizes two capture groups. The first
    # one has a leading dash and the second one does not. We jump directly to the second one.
    (( group_idx+=2 ))
    local _architecture=${BASH_REMATCH[$group_idx]}

    (( group_idx+=1 ))
    local _dash_ninja=${BASH_REMATCH[$group_idx]}
  else
    fatal "Could not parse build root directory name '$basename'" \
          "(full path: '$predefined_build_root'). Expected to match '$BUILD_ROOT_BASENAME_RE'."
  fi

  if [[ -z ${build_type:-} ]]; then
    build_type=$_build_type
    if [[ ${handle_predefined_build_root_quietly} == "false" ]]; then
      log "Setting build type to '$build_type' based on predefined build root ('$basename')"
    fi
    validate_build_type "$build_type"
  elif [[ $build_type != "$_build_type" ]]; then
    fatal "Build type from the build root ('$_build_type' from '$predefined_build_root') does " \
          "not match current build type ('$build_type')."
  fi

  if [[ -z ${YB_COMPILER_TYPE:-} ]]; then
    export YB_COMPILER_TYPE=$_compiler_type
    if [[ ${handle_predefined_build_root_quietly} == "false" ]]; then
      log "Automatically setting compiler type to '$YB_COMPILER_TYPE' based on predefined build" \
          "root ('$basename')"
    fi
  elif [[ $YB_COMPILER_TYPE != "$_compiler_type" ]]; then
    fatal "Compiler type from the build root ('$_compiler_type' from '$predefined_build_root') " \
          "does not match YB_COMPILER_TYPE ('$YB_COMPILER_TYPE')."
  fi

  if [[ -z ${YB_LINKING_TYPE:-} ]]; then
    export YB_LINKING_TYPE=$_linking_type
    if [[ ${handle_predefined_build_root_quietly} == "false" ]]; then
      log "Automatically setting linking type to '$YB_LINKING_TYPE' based on predefined build" \
          "root ('$basename')"
    fi
  elif [[ $YB_LINKING_TYPE != "$_linking_type" ]]; then
    fatal "Compiler type from the build root ('$_linking_type' from '$predefined_build_root') " \
          "does not match YB_LINKING_TYPE ('$YB_LINKING_TYPE')."
  fi

  local use_linuxbrew
  if [[ -z ${_dash_linuxbrew:-} ]]; then
    use_linuxbrew=false
    if [[ -n ${YB_LINUXBREW_DIR:-} ]]; then
      fatal "YB_LINUXBREW_DIR is set but the build root directory name '$basename' does not" \
            "contain a '-linuxbrew-' component."
    fi
  else
    # We will ensure that YB_LINUXBREW_DIR is set later. We might need to set it based on the
    # build root itself.
    use_linuxbrew=true
  fi

  if [[ -n $_architecture ]]; then
    if [[ -z ${YB_TARGET_ARCH:-} ]]; then
      export YB_TARGET_ARCH=$_architecture
      if [[ ! $YB_TARGET_ARCH =~ $VALID_ARCHITECTURES_RE ]]; then
        fatal "Invalid architecture '$YB_TARGET_ARCH' based on predefined build root " \
              "('$basename')"
      fi
    elif [[ $YB_TARGET_ARCH != "$_architecture" ]]; then
      fatal "YB_TARGET_ARCH is already set to $YB_TARGET_ARCH but the predefined build root " \
            "('$basename') has '$_architecture'."
    fi
  fi

  local should_use_ninja
  if [[ $_dash_ninja == "-ninja" ]]; then
    should_use_ninja=1
  else
    should_use_ninja=0
  fi
  if [[ $handle_predefined_build_root_quietly == "false" ]]; then
    log "Setting YB_USE_NINJA to 1 based on predefined build root ('$basename')"
  fi
  if [[ -n ${YB_USE_NINJA:-} && $YB_USE_NINJA != "$should_use_ninja" ]]; then
    fatal "The use of ninja from build root ('$predefined_build_root') does not match that" \
          "of the YB_USE_NINJA env var ('$YB_USE_NINJA')"
  fi
  export YB_USE_NINJA=$should_use_ninja

  decide_whether_to_use_ninja
  detect_brew

  if [[ ${use_linuxbrew} == "true" && -z ${YB_LINUXBREW_DIR:-} ]]; then
    if [[ -f "$predefined_build_root/linuxbrew_path.txt" ]]; then
      YB_LINUXBREW_DIR=$(<"$predefined_build_root/linuxbrew_path.txt")
      export YB_LINUXBREW_DIR
    else
      fatal "YB_LINUXBREW_DIR is not set but the build root directory name '$basename' contains a" \
            "'-linuxbrew-' component."
    fi
  fi
}

# Remove the build/latest symlink to prevent Jenkins from showing every test twice in test results.
# We call this from a few different places just in case.
remove_latest_symlink() {
  local latest_build_link=$YB_BUILD_PARENT_DIR/latest
  if [[ -h $latest_build_link ]]; then
    log "Removing the latest symlink at '$latest_build_link'"
    ( set -x; unlink "$latest_build_link" )
  fi
}

# Assigns a random "test invocation id" that allows to kill stuck processes corresponding to this
# instance of a particular test or the whole test suite.
set_test_invocation_id() {
  local timestamp
  timestamp=$( get_timestamp_for_filenames )
  export YB_TEST_INVOCATION_ID=test_invocation_${timestamp}_${RANDOM}_${RANDOM}_$$
}

# Kills any processes that have YB_TEST_INVOCATION_ID in their command line. Sets
# killed_stuck_processes=true in case that happens.
kill_stuck_processes() {
  expect_no_args "$#"
  killed_stuck_processes=false
  if [[ -z ${YB_TEST_INVOCATION_ID:-} ]]; then
    return
  fi
  local pid
  for pid in $( pgrep -f "$YB_TEST_INVOCATION_ID" ); do
    log "Found pid $pid from this test suite (YB_TEST_INVOCATION_ID=$YB_TEST_INVOCATION_ID)," \
        "killing it with SIGKILL."
    ps -p "$pid" -f
    if kill -9 "$pid"; then
      # killed_stuck_processes is used in run-test.sh
      # shellcheck disable=SC2034
      killed_stuck_processes=true
      log "Killed process $pid with SIGKILL."
    fi
  done
}

handle_build_root_from_current_dir() {
  predefined_build_root=""
  local handle_predefined_build_root_quietly=true
  local d=$PWD
  while [[ $d != "/" && $d != "" ]]; do
    basename=${d##*/}
    if [[ ${YB_DEBUG_BUILD_ROOT_BASENAME_VALIDATION:-0} == "1" ]]; then
      log "Trying to match basename $basename to regex: $BUILD_ROOT_BASENAME_RE"
    fi
    if [[ $basename =~ $BUILD_ROOT_BASENAME_RE ]]; then
      predefined_build_root=$d
      handle_predefined_build_root
      return
    fi
    d=${d%/*}
  done

  fatal "Working directory of the compiler '$PWD' is not within a valid Yugabyte build root: " \
        "'$BUILD_ROOT_BASENAME_RE'"
}

validate_numeric_arg_range() {
  expect_num_args 4 "$@"
  local arg_name=$1
  local arg_value=$2
  local -r -i min_value=$3
  local -r -i max_value=$4
  if [[ ! $arg_value =~ ^[0-9]+$ ]]; then
    fatal "Invalid numeric argument value for --$arg_name: '$arg_value'"
  fi
  if [[ $arg_value -lt $min_value || $arg_value -gt $max_value ]]; then
    fatal "Value out of range for --$arg_name: $arg_value, must be between $min_value and" \
          "$max_value."
  fi
}

# -------------------------------------------------------------------------------------------------
# Python support
# -------------------------------------------------------------------------------------------------

# Checks syntax of all Python scripts in the repository.
check_python_script_syntax() {
  if [[ -n ${YB_VERBOSE:-} ]]; then
    log "Checking syntax of Python scripts"
  fi
  pushd "$YB_SRC_ROOT"
  local IFS=$'\n'
  # Get all .py files in git, ignoring files with skip-worktree bit set (e.g.
  # through git sparse-checkout), and check their syntax.
  git ls-files -t '*.py' \
    | grep -v '^S' \
    | sed 's/^[[:alpha:]] //' \
    | xargs -P 8 -n 1 "$YB_SCRIPT_PATH_CHECK_PYTHON_SYNTAX"
  popd +0
}

run_shellcheck() {
  pushd "$YB_SRC_ROOT"
  local scripts_to_check=(
    bin/*.sh
    build-support/*.sh
    build-support/jenkins/*.sh
    yb_build.sh
    yugabyted-ui/build.sh
  )
  local script_path
  local shellcheck_had_errors=false
  if shellcheck --version | grep -q '^version: 0.3'; then
    # Centos7 distro has old version of shellcheck that does not support --external-sources.
    log "Warning: Skipping shellcheck. Requires at least version 0.4.0."
    popd +0
    return 0
  fi
  # We skip errors 2030 and 2031 that say that a variable has been modified in a subshell and that
  # the modification is local to the subshell. Seeing a lot of false positivies for these with
  # the version 0.7.2 of Shellcheck.
  # SC2230 (avoid 'which" command) is supposed to be optional check, but enabled in some distros.
  local excodes="2030,2031,2230"
  for script_path in "${scripts_to_check[@]}"; do
    if ! ( set -x; shellcheck --external-sources --exclude="$excodes" --shell=bash "$script_path" )
    then
      shellcheck_had_errors=true
    fi
  done
  popd +0
  if [[ $shellcheck_had_errors == "true" ]]; then
    exit 1
  fi
}

activate_virtualenv() {
  detect_architecture
  local virtualenv_parent_dir=$YB_BUILD_PARENT_DIR
  local virtualenv_dir=$virtualenv_parent_dir/$YB_VIRTUALENV_BASENAME

  log "Activating python virtual env"

  # Symlink the requirements files into the top level build directory
  # We assume $YB_SRC_ROOT/build is already created by initialize_yugabyte_bash_common having
  # already been called.
  [[ -f "${YB_SRC_ROOT}/build/requirements.txt" ]] \
    || ln -sf "${YB_SRC_ROOT}/requirements.txt" "${YB_SRC_ROOT}/build/"
  [[ -f "${YB_SRC_ROOT}/build/requirements_frozen.txt" ]] \
    || ln -sf "${YB_SRC_ROOT}/requirements_frozen.txt" "${YB_SRC_ROOT}/build/"

  yb_activate_virtualenv "${virtualenv_parent_dir}"


  if [[ ${YB_DEBUG_VIRTUALENV:-0} == "1" ]]; then
    echo >&2 "
VIRTUALENV DEBUGGING
--------------------

    Activated virtualenv in: $virtualenv_dir
    Executable: $0
    PATH: $PATH
    PYTHONPATH: ${PYTHONPATH:-undefined}
    VIRTUAL_ENV: ${VIRTUAL_ENV:-undefined}

"
  fi

  export VIRTUAL_ENV
}

set_pythonpath_called=false

set_pythonpath() {
  if [[ $set_pythonpath_called == "true" ]]; then
    return
  fi
  set_pythonpath_called=true

  if [[ ! -d ${YB_SRC_ROOT:-} ]]; then
    fatal "YB_SRC_ROOT is not set or does not exist; ${YB_SRC_ROOT:-undefined}"
  fi

  local new_entry=$YB_SRC_ROOT/python
  if [[ -z ${PYTHONPATH:-} ]]; then
    PYTHONPATH=$new_entry
  else
    PYTHONPATH=$new_entry:$PYTHONPATH
  fi
  export PYTHONPATH
}

log_file_existence() {
  expect_num_args 1 "$@"
  local file_name=$1
  if [[ -L $file_name && -f $file_name ]]; then
    log "Symlink exists and points to a file: $file_name"
  elif [[ -L $file_name && -d $file_name ]]; then
    log "Symlink exists and points to a directory: $file_name"
  elif [[ -L $file_name ]]; then
    log "Symlink exists but it might be broken: $file_name"
  elif [[ -f $file_name ]]; then
    log "File exists: $file_name"
  elif [[ -d $file_name ]]; then
    log "Directory exists: $file_name"
  elif [[ ! -e $file_name ]]; then
    log "File does not exist: $file_name"
  else
    log "File exists but we could not determine its type: $file_name"
  fi
}

is_valid_git_sha1() {
  [[ $1 =~ ^[0-9a-f]{40} ]]
}

# Returns current git SHA1 in the variable current_git_sha1.
get_current_git_sha1() {
  current_git_sha1=$( git rev-parse HEAD )
  if ! is_valid_git_sha1 "$current_git_sha1"; then
    fatal "Could not get current git SHA1 in $PWD, got: $current_git_sha1"
  fi
}

# sed -i works differently on Linux vs macOS.
sed_i() {
  if is_mac; then
    sed -i "" "$@"
  else
    sed -i "$@"
  fi
}

lint_java_code() {
  local java_project_dir
  declare -i num_errors=0

  for java_project_dir in "${yb_java_project_dirs[@]}"; do
    local IFS=$'\n'
    local java_test_files
    # The proposed syntax in SC2207 is too confusing.
    # shellcheck disable=SC2207
    java_test_files=( $(
      find "$java_project_dir" \( -name "Test*.java" -or -name "*Test.java" \) -and \
          -not -name "TestUtils.java" -and \
          -not -name "*Base.java" -and \
          -not -name "Base*Test.java"
    ) )
    local java_test_file
    for java_test_file in "${java_test_files[@]}"; do
      local log_prefix="YB JAVA LINT: $java_test_file"
      if ! grep -Eq '@RunWith\((value[ ]*=[ ]*)?YBParameterizedTestRunner\.class\)' \
             "$java_test_file" &&
         ! grep -Eq '@RunWith\((value[ ]*=[ ]*)?YBTestRunner\.class\)' \
             "$java_test_file" &&
         ! grep -Eq '@RunWith\((value[ ]*=[ ]*)?YBTestRunnerNonTsanOnly\.class\)' \
             "$java_test_file" &&
         ! grep -Eq '@RunWith\((value[ ]*=[ ]*)?YBTestRunnerNonTsanAsan\.class\)' \
             "$java_test_file" &&
         ! grep -Eq '@RunWith\((value[ ]*=[ ]*)?YBTestRunnerNonSanitizersOrMac\.class\)' \
             "$java_test_file" &&
         ! grep -Eq '@RunWith\((value[ ]*=[ ]*)?YBTestRunnerNonSanOrAArch64Mac\.class\)' \
             "$java_test_file" &&
         ! grep -Eq '@RunWith\((value[ ]*=[ ]*)?YBTestRunnerReleaseOnly\.class\)' \
             "$java_test_file" &&
         ! grep -Eq '@RunWith\((value[ ]*=[ ]*)?YBTestRunnerYsqlConnMgr\.class\)' \
             "$java_test_file" &&
         ! grep -Eq '@RunWith\((value[ ]*=[ ]*)?YBTestRunnerNonMac\.class\)' \
             "$java_test_file"
      then
        log "$log_prefix: neither YBTestRunner, YBParameterizedTestRunner, " \
            "YBTestRunnerNonTsanOnly, YBTestRunnerNonTsanAsan, YBTestRunnerNonSanitizersOrMac, " \
            "YBTestRunnerNonSanOrAArch64Mac, " \
            "YBTestRunnerReleaseOnly, YBTestRunnerYsqlConnMgr, nor YBTestRunnerNonMac are being " \
            "used in test"
        num_errors+=1
      fi
      if grep -Fq 'import static org.junit.Assert' "$java_test_file" ||
         grep -Fq 'import org.junit.Assert' "$java_test_file"; then
        log "$log_prefix: directly importing org.junit.Assert. Should use org.yb.AssertionWrappers."
        num_errors+=1
      fi
    done
  done
  if [[ $num_errors -eq 0 ]]; then
    log "Light-weight lint of YB Java code: SUCCESS"
  else
    log "Light-weight lint of YB Java code: FAILURE ($num_errors errors found)"
    return 1
  fi
}

run_with_retries() {
  if [[ $# -lt 2 ]]; then
    fatal "run_with_retries requires at least three arguments: max_attempts, delay_sec, and " \
          "the command to run (at least one additional argument)."
  fi
  declare -i -r max_attempts=$1
  declare -r delay_sec=$2
  shift 2

  declare -i attempt_index=1
  while [[ $attempt_index -le $max_attempts ]]; do
    set +e
    "$@"
    declare exit_code=$?
    set -e
    if [[ $exit_code -eq 0 ]]; then
      return
    fi
    log "Warning: command failed with exit code $exit_code at attempt $attempt_index: $*." \
        "Waiting for $delay_sec sec, will then re-try for up to $max_attempts attempts."
    (( attempt_index+=1 ))
    sleep "$delay_sec"
  done
  fatal "Failed to execute command after $max_attempts attempts: $*"
}

debug_log_boolean_function_result() {
  expect_num_args 1 "$@"
  local fn_name=$1
  if [[ ${fn_name} == "true" ]]; then
    log "$fn_name is true"
  else
    log "$fn_name is false"
  fi
}

set_java_home() {
  if ! is_mac; then
    return
  fi
  if [[ -n ${JAVA_HOME:-} ]]; then
    return
  fi
  # macOS has a peculiar way of setting JAVA_HOME
  local cmd_to_get_java_home
  cmd_to_get_java_home="/usr/libexec/java_home --version 1.8"
  local new_java_home
  new_java_home=$( $cmd_to_get_java_home )
  if [[ ! -d $new_java_home ]]; then
    fatal "Directory returned by '$cmd_to_get_java_home' does not exist: $new_java_home"
  fi
  log "Setting JAVA_HOME: $new_java_home"
  export JAVA_HOME=$new_java_home
  put_path_entry_first "$JAVA_HOME/bin"
}

set_prebuilt_thirdparty_url() {
  expect_vars_to_be_set YB_COMPILER_TYPE build_type
  if [[ ${YB_DOWNLOAD_THIRDPARTY:-} == "1" ]]; then
    if [[ -z ${YB_THIRDPARTY_URL:-} ]]; then
      local thirdparty_url_file_path="$BUILD_ROOT/thirdparty_url.txt"
      local thirdparty_checksum_url_file_path="$BUILD_ROOT/thirdparty_checksum_url.txt"
      local llvm_url_file_path="$BUILD_ROOT/llvm_url.txt"
      if [[ -f $thirdparty_url_file_path ]]; then
        rm -f "$thirdparty_url_file_path"
      fi
      if [[ -f $thirdparty_checksum_url_file_path ]]; then
        rm -f "$thirdparty_checksum_url_file_path"
      fi
      local thirdparty_tool_cmd_line=(
        "$YB_BUILD_SUPPORT_DIR/thirdparty_tool"
        --save-thirdparty-url-to-file "$thirdparty_url_file_path"
        --save-thirdparty-checksum-url-to-file "$thirdparty_checksum_url_file_path"
        --compiler-type "${YB_COMPILER_TYPE_FOR_THIRDPARTY:-$YB_COMPILER_TYPE}"
      )
      if [[ -n ${YB_USE_LINUXBREW:-} ]]; then
        # See arg_str_to_bool in Python code for how the boolean parameter is interpreted.
        thirdparty_tool_cmd_line+=( "--is-linuxbrew=$YB_USE_LINUXBREW" )
      fi
      if [[ ${YB_LINKING_TYPE:-dynamic} != "dynamic" ]]; then
        # Transform "thin-lto" or "full-lto" into "thin" or "full" respectively.
        thirdparty_tool_cmd_line+=( "--lto=${YB_LINKING_TYPE%%-lto}" )
      fi
      if [[ ! ${build_type} =~ ^(asan|tsan)$ && ${YB_COMPILER_TYPE} == clang* ]]; then
        thirdparty_tool_cmd_line+=( "--allow-older-os" )
      fi
      "${thirdparty_tool_cmd_line[@]}"
      YB_THIRDPARTY_URL=$(<"$thirdparty_url_file_path")
      export YB_THIRDPARTY_URL
      YB_THIRDPARTY_CHECKSUM_URL=$(<"$thirdparty_checksum_url_file_path")
      export YB_THIRDPARTY_CHECKSUM_URL
      yb_thirdparty_url_origin="determined automatically based on the OS and compiler type"
      if [[ -z $YB_THIRDPARTY_URL ]]; then
        fatal "Could not automatically determine the third-party archive URL to download."
      fi
      log "Setting third-party URL to $YB_THIRDPARTY_URL"
      save_var_to_file_in_build_dir "$YB_THIRDPARTY_URL" thirdparty_url.txt
      yb_thirdparty_checksum_url_origin="determined automatically based on the OS and compiler type"
      if [[ -z ${YB_THIRDPARTY_CHECKSUM_URL:-} ]]; then
        YB_THIRDPARTY_CHECKSUM_URL="$YB_THIRDPARTY_URL.sha256"
        fatal "Could not automatically determine the third-party archive URL to download."
      fi
      log "Setting third-party checksum URL to $YB_THIRDPARTY_CHECKSUM_URL"
      save_var_to_file_in_build_dir "$YB_THIRDPARTY_CHECKSUM_URL" thirdparty_checksum_url.txt

      if [[ -f $llvm_url_file_path ]]; then
        YB_LLVM_TOOLCHAIN_URL=$(<"$llvm_url_file_path")
        export YB_LLVM_TOOLCHAIN_URL
        yb_llvm_toolchain_url_origin="determined automatically based on the OS and compiler type"
        log "Setting LLVM toolchain URL to $YB_LLVM_TOOLCHAIN_URL"
        save_var_to_file_in_build_dir "$YB_LLVM_TOOLCHAIN_URL" llvm_url.txt
      fi

    else
      log "YB_THIRDPARTY_URL is already set to '$YB_THIRDPARTY_URL', not trying to set it" \
          "automatically."
    fi
  fi
}

check_arc_wrapper() {
  if is_jenkins || [[ ${YB_SKIP_ARC_WRAPPER_CHECK:-0} == "1" ]]; then
    return
  fi
  if [[ -f $HOME/.yb_build_env_bashrc ]]; then
    # This is a Yugabyte workstation or dev server.
    local arc_path
    set +e
    arc_path=$( which arc )
    set -e
    if [[ ! -f $arc_path ]]; then
      # OK if arc is not found. Then people cannot "arc land" changes that do not pass tests.
      return
    fi
    local expected_arc_path=$HOME/tools/bin/arc
    if [[ $arc_path == "$expected_arc_path" ]]; then
      # OK, this is where we install the arc wrapper.
      return
    fi

    if grep -Eq "Wrapper for arcanist arc" "$arc_path"; then
      # This seems to be a valid arc wrapper installed elsewhere.
      return
    fi

    fatal "Not a valid arc wrapper: $arc_path (required for internal Yugabyte hosts)"
  fi
}

# Re-executes the current script with the correct macOS architecture. The parameters are the
# TODO: this currently partially duplicates a function from
# https://github.com/yugabyte/yugabyte-db-thirdparty/blob/master/yb-thirdparty-common.sh
# called ensure_correct_mac_architecture. We need to move both to
# https://github.com/yugabyte/yugabyte-bash-common.
rerun_script_with_arch_if_necessary() {
  # The caller might be using "set +u", so turn undefined variable checking back on.
  set -u

  if [[ $OSTYPE != darwin* ]]; then
    return
  fi
  local uname_m_output
  uname_m_output=$( uname -m )
  if [[ -z "${uname_m_output}" ]]; then
    fatal "Empty output from 'uname -m'."
  fi
  if [[ -z ${YB_TARGET_ARCH:-} ]]; then
    YB_TARGET_ARCH=${uname_m_output}
  fi
  if [[ $YB_TARGET_ARCH != "x86_64" && $YB_TARGET_ARCH != "arm64" ]]; then
    fatal "Invalid value of YB_TARGET_ARCH on macOS (expected x86_64 or arm64): $YB_TARGET_ARCH." \
          "Output of 'uname -m': ${uname_m_output}."
  fi
  if [[ "${uname_m_output}" != "$YB_TARGET_ARCH" ]]; then
    if [[ -n ${YB_SWITCHED_ARCHITECTURE_FROM:-} &&
          $YB_SWITCHED_ARCHITECTURE_FROM == "${uname_m_output}" ]]; then
      fatal "Infinite architecture-switching loop detected: already switched from" \
            "'${uname_m_output}' but could not switch to '${YB_TARGET_ARCH}'."
    fi

    echo "Switching architecture from '${uname_m_output}' to '$YB_TARGET_ARCH'"
    local cmd_line
    if [[ ${YB_TARGET_ARCH} == "arm64" ]]; then
      # Use the arm64 specific version of Bash for added reliability.
      cmd_line=( arch -arm64 /opt/homebrew/bin/bash "$@" )
    else
      cmd_line=( arch "-$YB_TARGET_ARCH" "$@" )
    fi
    export YB_SWITCHED_ARCHITECTURE_FROM=$uname_m_output
    set -x
    exec "${cmd_line[@]}"
  fi
}

# Sets YB_TARGET_ARCH if they are not set.
detect_architecture() {
  if [[ -z ${YB_TARGET_ARCH:-} ]]; then
    if is_apple_silicon; then
      YB_TARGET_ARCH=arm64
    else
      YB_TARGET_ARCH=$( uname -m )
    fi
  fi
  export YB_TARGET_ARCH
}

is_apple_silicon() {
  if [[ -n "$is_apple_silicon" ]]; then
    if [[ "$is_apple_silicon" == "true" ]]; then
      return 0
    fi
    return 1
  fi

  is_apple_silicon=false
  if ! is_mac; then
    return 1
  fi

  if [[ $( uname -v ) == *ARM64* ]]; then
    is_apple_silicon=true
    return 0
  fi

  return 1
}

validate_clangd_index_format() {
  expect_num_args 1 "$@"
  local format=$1
  if [[ ! ${format} =~ ^(binary|yaml)$ ]]; then
    fatal "Invalid Clangd index format specified: ${format} (expected 'binary' or 'yaml')"
  fi
}

build_clangd_index() {
  expect_num_args 1 "$@"
  local format=$1
  validate_clangd_index_format "${format}"
  local clangd_index_path=${BUILD_ROOT}/clangd_index.${format}
  log "Building Clangd index at ${clangd_index_path}"
  (
    set -x
    # The location of the final compilation database file needs to be consistent with that in the
    # compile_commands.py module.
    time "${YB_LLVM_TOOLCHAIN_DIR}/bin/clangd-indexer" \
        --executor=all-TUs \
        "--format=${format}" \
        "${BUILD_ROOT}/compile_commands/combined_postprocessed/compile_commands.json" \
        >"${clangd_index_path}"
  )
}

adjust_compiler_type_on_mac() {
  # A workaround for old macOS build workers where the default Clang version is 13 or older.
  if is_mac &&
    ! is_apple_silicon &&
    [[ ${YB_COMPILER_TYPE:-clang} == "clang" &&
       -f /usr/bin/clang &&
       $(clang --version) =~ clang\ version\ ([0-9]+) ]]
  then
    clang_major_version=${BASH_REMATCH[1]}
    if [[ ${clang_major_version} -lt 14 ]]; then
      export YB_COMPILER_TYPE=clang14
      # Used in common-build-env-test.sh to avoid failing when the compiler type is adjusted.
      export YB_COMPILER_TYPE_WAS_ADJUSTED=true
    fi
  fi
}

# -------------------------------------------------------------------------------------------------
# Initialization
# -------------------------------------------------------------------------------------------------

# http://man7.org/linux/man-pages/man7/signal.7.html
if is_mac; then
  declare -i -r SIGUSR1_EXIT_CODE=158  # 128 + 30
else
  # Linux
  # SIGUSR1_EXIT_CODE is used in common-test-env.sh
  # shellcheck disable=SC2034
  declare -i -r SIGUSR1_EXIT_CODE=138  # 128 + 10
fi

# Parent directory for build directories of all build types.
YB_BUILD_INTERNAL_PARENT_DIR=$YB_SRC_ROOT/build
YB_BUILD_EXTERNAL_PARENT_DIR=${YB_SRC_ROOT}__build
if [[ ${YB_USE_EXTERNAL_BUILD_ROOT:-} == "1" ]]; then
  YB_BUILD_PARENT_DIR=$YB_BUILD_EXTERNAL_PARENT_DIR
else
  YB_BUILD_PARENT_DIR=$YB_BUILD_INTERNAL_PARENT_DIR
fi

if [[ ! -d $YB_BUILD_SUPPORT_DIR ]]; then
  fatal "Could not determine YB source directory from '${BASH_SOURCE[0]}':" \
        "$YB_BUILD_SUPPORT_DIR does not exist."
fi

readonly -a YB_DEFAULT_CMAKE_OPTS=(
  "-DCMAKE_C_COMPILER=$YB_COMPILER_WRAPPER_CC"
  "-DCMAKE_CXX_COMPILER=$YB_COMPILER_WRAPPER_CXX"
)

if [[ ${YB_COMMON_BUILD_ENV_DEBUG:-0} == "1" ]]; then
  echo >&2 \
    "Turning debugging on in common-build-env.sh because YB_COMMON_BUILD_ENV_DEBUG is set to 1"
  set -x
fi
