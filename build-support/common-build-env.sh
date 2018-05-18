#@IgnoreInspection BashAddShebang

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

# This is common between build and test scripts.

set -euo pipefail

if [[ $BASH_SOURCE == $0 ]]; then
  echo "$BASH_SOURCE must be sourced, not executed" >&2
  exit 1
fi

# Guard against multiple inclusions.
if [[ -n ${YB_COMMON_BUILD_ENV_SOURCED:-} ]]; then
  # Return to the executing script.
  return
fi

YB_COMMON_BUILD_ENV_SOURCED=1

declare -i MAX_JAVA_BUILD_ATTEMPTS=5

# What matches these expressions will be filtered out of Maven output.
MVN_OUTPUT_FILTER_REGEX='\[INFO\] (Download(ing|ed): '
MVN_OUTPUT_FILTER_REGEX+='|[^ ]+ already added, skipping$)'
MVN_OUTPUT_FILTER_REGEX+='|^Generating .*[.]html[.][.][.]$'
MVN_OUTPUT_FILTER_REGEX+='|^\[INFO\] Copying .*[.]jar to .*[.]jar$'

readonly YB_JENKINS_NFS_HOME_DIR=/n/jenkins

# In our NFS environment, we keep Linuxbrew builds in this directory.
readonly SHARED_LINUXBREW_BUILDS_DIR="$YB_JENKINS_NFS_HOME_DIR/linuxbrew"

# We look for the list of distributed build worker nodes in this file. This gets populated by
# a cronjob on buildmaster running under the jenkins user (as of 06/20/2017).
readonly YB_BUILD_WORKERS_FILE=$YB_JENKINS_NFS_HOME_DIR/run/build-workers

# The assumed number of cores per build worker. This is used in the default make parallelism level
# calculation in yb_build.sh. This does not have to be the exact number of cores per worker, but
# will affect whether or not we force the auto-scaling group of workers to expand.
readonly YB_NUM_CORES_PER_BUILD_WORKER=8

# The "number of build workers" that we'll end up using to compute the parallelism (by multiplying
# it by YB_NUM_CORES_PER_BUILD_WORKER) will be first brought into this range.
readonly MIN_EFFECTIVE_NUM_BUILD_WORKERS=5
readonly MAX_EFFECTIVE_NUM_BUILD_WORKERS=10

readonly MVN_OUTPUT_FILTER_REGEX

# An even faster alternative to downloading a pre-built third-party dependency tarball from S3
# or Google Storage: just use a pre-existing third-party build from NFS. This has to be maintained
# outside of main (non-thirdparty) YB codebase's build pipeline.
readonly NFS_PARENT_DIR_FOR_SHARED_THIRDPARTY=/n/jenkins/thirdparty

# This node is the NFS server and is also used to run the non-distributed part of distributed builds
# (e.g. "cmake" or "make" commands) in a way such that it would have access to the build directory
# as a local filesystem.
#
# This must be something that could be compared with $HOSTNAME, i.e. this can't be
# "buildmaster.c.yugabyte.internal", only "buildmaster".
readonly DISTRIBUTED_BUILD_MASTER_HOST=buildmaster

# We create a Python Virtual Environment inside this directory in the build directory.
readonly YB_VIRTUALENV_BASENAME=python_virtual_env

# -------------------------------------------------------------------------------------------------
# Functions used in initializing some constants
# -------------------------------------------------------------------------------------------------

print_stack_trace() {
  local -i i=${1:-1}  # Allow the caller to set the line number to start from.
  echo "Stack trace:" >&2
  while [[ $i -lt "${#FUNCNAME[@]}" ]]; do
    echo "  ${BASH_SOURCE[$i]}:${BASH_LINENO[$((i - 1))]} ${FUNCNAME[$i]}" >&2
    let i+=1
  done
}

fatal() {
  if [[ -n "${yb_fatal_quiet:-}" ]]; then
    yb_log_quiet=$yb_fatal_quiet
  else
    yb_log_quiet=false
  fi
  yb_log_skip_top_frames=1
  log "$@"
  if ! "$yb_log_quiet"; then
    print_stack_trace 2  # Exclude this line itself from the stack trace (start from 2nd line).
  fi
  exit 1
}

get_timestamp() {
  date +%Y-%m-%dT%H:%M:%S
}

get_timestamp_for_filenames() {
  date +%Y-%m-%dT%H_%M_%S
}

log_empty_line() {
  echo >&2
}

log_separator() {
  log_empty_line
  echo >&2 "--------------------------------------------------------------------------------------"
  log_empty_line
}

heading() {
  log_empty_line
  echo >&2 "--------------------------------------------------------------------------------------"
  echo >&2 "$1"
  echo >&2 "--------------------------------------------------------------------------------------"
  log_empty_line
}

log() {
  if [[ "${yb_log_quiet:-}" != "true" ]]; then
    # Weirdly, when we put $* inside double quotes, that has an effect of making the following log
    # statement produce multi-line output:
    #
    #   log "Some long log statement" \
    #       "continued on the other line."
    #
    # We want that to produce a single line the same way the echo command would. Putting $* by
    # itself achieves that effect. That has a side effect of passing echo-specific arguments
    # (e.g. -n or -e) directly to the final echo command.
    #
    # On why the index for BASH_LINENO is one lower than that for BASH_SOURECE and FUNCNAME:
    # This is different from the manual says at
    # https://www.gnu.org/software/bash/manual/html_node/Bash-Variables.html:
    #
    #   An array variable whose members are the line numbers in source files where each
    #   corresponding member of FUNCNAME was invoked. ${BASH_LINENO[$i]} is the line number in the
    #   source file (${BASH_SOURCE[$i+1]}) where ${FUNCNAME[$i]} was called (or ${BASH_LINENO[$i-1]}
    #   if referenced within another shell function). Use LINENO to obtain the current line number.
    #
    # Our experience is that FUNCNAME indexes exactly match those of BASH_SOURCE.
    local stack_idx0=${yb_log_skip_top_frames:-0}
    local stack_idx1=$(( $stack_idx0 + 1 ))

    echo "[$( get_timestamp )" \
         "${BASH_SOURCE[$stack_idx1]##*/}:${BASH_LINENO[$stack_idx0]}" \
         "${FUNCNAME[$stack_idx1]}]" $* >&2
  fi
}

log_with_color() {
  local log_color=$1
  shift
  log "$log_color$*$NO_COLOR"
}

horizontal_line() {
  echo "------------------------------------------------------------------------------------------"
}

thick_horizontal_line() {
  echo "=========================================================================================="
}

header() {
  echo
  horizontal_line
  echo "$@"
  horizontal_line
  echo
}

# Usage: expect_some_args "$@"
# Fatals if there are no arguments.
expect_some_args() {
  local calling_func_name=${FUNCNAME[1]}
  if [[ $# -eq 0 ]]; then
    fatal "$calling_func_name expects at least one argument"
  fi
}

# Make a regular expression from a list of possible values. This function takes any non-zero number
# of arguments, but each argument is further broken down into components separated by whitespace,
# and those components are treated as separate possible values. Empty values are ignored.
make_regex_from_list() {
  local list_var_name=$1
  expect_some_args "$@"
  local regex=""
  local list_var_name_full="$list_var_name[@]"
  for item in "${!list_var_name_full}"; do
    if [[ -z $item ]]; then
      continue
    fi
    if [[ -n $regex ]]; then
      regex+="|"
    fi
    regex+="$item"
  done
  eval "${list_var_name}_RE=\"^($regex)$\""
  eval "${list_var_name}_RAW_RE=\"$regex\""
}

make_regexes_from_lists() {
  local list_var_name
  for list_var_name in "$@"; do
    make_regex_from_list "$list_var_name"
  done
}

# -------------------------------------------------------------------------------------------------
# Constants
# -------------------------------------------------------------------------------------------------

readonly VALID_BUILD_TYPES=(
  asan
  debug
  fastdebug
  profile_build
  profile_gen
  release
  tsan
  tsan_slow
)

# Valid values of CMAKE_BUILD_TYPE passed to the top-level CMake build. This is the same as the
# above with the exclusion of ASAN/TSAN.
readonly VALID_CMAKE_BUILD_TYPES=(
  debug
  fastdebug
  profile_build
  profile_gen
  release
)

readonly VALID_COMPILER_TYPES=( gcc clang zapcc )

readonly VALID_LINKING_TYPES=( static dynamic )

readonly VALID_EDITIONS=( community enterprise )

make_regexes_from_lists \
  VALID_BUILD_TYPES \
  VALID_CMAKE_BUILD_TYPES \
  VALID_COMPILER_TYPES \
  VALID_LINKING_TYPES \
  VALID_EDITIONS

readonly BUILD_ROOT_BASENAME_RE=\
"^($VALID_BUILD_TYPES_RAW_RE)-\
($VALID_COMPILER_TYPES_RAW_RE)-\
($VALID_LINKING_TYPES_RAW_RE)-\
($VALID_EDITIONS_RAW_RE)\
(-ninja)?$"

readonly YELLOW_COLOR="\033[0;33m"
readonly RED_COLOR="\033[0;31m"
readonly CYAN_COLOR="\033[0;36m"
readonly NO_COLOR="\033[0m"

# We first use this to find ephemeral drives.
readonly EPHEMERAL_DRIVES_GLOB="/mnt/ephemeral* /mnt/d*"

# We then filter the drives found using this.
# The way we use this regex we expect it NOT to be anchored in the end.
readonly EPHEMERAL_DRIVES_FILTER_REGEX="^/mnt/(ephemeral|d)[0-9]+"  # No "$" in the end.

# http://stackoverflow.com/questions/5349718/how-can-i-repeat-a-character-in-bash
readonly HORIZONTAL_LINE=$( printf '=%.0s' {1..80} )

# -------------------------------------------------------------------------------------------------
# Functions
# -------------------------------------------------------------------------------------------------

yellow_color() {
  echo -ne "$YELLOW_COLOR"
}

red_color() {
  echo -ne "$RED_COLOR"
}

no_color() {
  echo -ne "$NO_COLOR"
}

to_lowercase() {
  tr A-Z a-z
}

is_mac() {
  [[ "$OSTYPE" =~ ^darwin ]]
}

is_linux() {
  [[ "$OSTYPE" =~ ^linux ]]
}

expect_vars_to_be_set() {
  local calling_func_name=${FUNCNAME[1]}
  local var_name
  for var_name in "$@"; do
    if [[ -z ${!var_name:-} ]]; then
      fatal "The '$var_name' variable must be set by the caller of $calling_func_name." \
            "$calling_func_name expects the following variables to be set: $@."
    fi
  done
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
  if [[ $# -ne $caller_expected_num_args ]]; then
    yb_log_quiet=false
    local error_msg="$calling_func_name expects $caller_expected_num_args arguments, got $#."
    if [[ $# -eq 0 ]]; then
      error_msg+=" Check if \"\$@\" was included in the call to expect_num_args."
    fi
    if [[ $# -gt 0 ]]; then
      log "Logging actual arguments to '$calling_func_name' before a fatal error:"
      local arg
      for arg in "$@"; do
        log "  - $arg"
      done
    fi
    fatal "$error_msg"
  fi
}

normalize_build_type() {
  validate_build_type "$build_type"
  local lowercase_build_type=$( echo "$build_type" | to_lowercase )
  if [[ "$build_type" != "$lowercase_build_type" ]]; then
    # Only assign if we actually need to, because the build_type variable may already be read-only.
    build_type=$lowercase_build_type
  fi
}

# Sets the build directory based on the given build type (the build_type variable) and the value of
# the YB_COMPILER_TYPE environment variable.
set_build_root() {
  if [[ ${1:-} == "--no-readonly" ]]; then
    local -r make_build_root_readonly=false
    shift
  else
    local -r make_build_root_readonly=true
  fi

  expect_num_args 0 "$@"
  normalize_build_type
  readonly build_type

  validate_compiler_type "$YB_COMPILER_TYPE"
  determine_linking_type

  BUILD_ROOT=$YB_BUILD_PARENT_DIR/$build_type-$YB_COMPILER_TYPE-$YB_LINK

  detect_edition
  BUILD_ROOT+="-$YB_EDITION"

  if using_ninja; then
    BUILD_ROOT+="-ninja"
  fi

  normalize_build_root

  if "$make_build_root_readonly"; then
    readonly BUILD_ROOT
  fi

  if [[ -n ${predefined_build_root:-} && $predefined_build_root != $BUILD_ROOT ]]; then
    fatal "An inconsistency between predefined BUILD_ROOT ('$predefined_build_root') and" \
          "computed BUILD_ROOT ('$BUILD_ROOT')."
  fi

  export BUILD_ROOT
}

# Resolve the BUILD_ROOT symlink and save the result to the real_build_root_path variable.
set_real_build_root_path() {
  if [[ -h $BUILD_ROOT ]]; then
    real_build_root_path=$( readlink "$BUILD_ROOT" )
  else
    real_build_root_path="$BUILD_ROOT"
  fi

  readonly real_build_root_path=$( cd "$real_build_root_path" && pwd )
}

ensure_build_root_is_set() {
  if [[ -z ${BUILD_ROOT:-} ]]; then
    fatal "The BUILD_ROOT environment variable is not set. This must point to the absolute path" \
          "of the build root directory, e.g. '<yugabyte_src_dir>/build/debug'."
  fi
}

ensure_directory_exists() {
  expect_num_args 1 "$@"
  local directory_path=$1
  if [[ ! -d $directory_path ]]; then
    fatal "Directory '$directory_path' does not exist or is not a directory"
  fi
}

ensure_file_exists() {
  expect_num_args 1 "$@"
  local file_name=$1
  if [[ ! -f $file_name ]]; then
    fatal "File '$file_name' does not exist or is not a file"
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

determine_linking_type() {
  if [[ -z "${YB_LINK:-}" ]]; then
    YB_LINK=dynamic
  fi
  if [[ ! "${YB_LINK:-}" =~ ^$VALID_LINKING_TYPES_RE$ ]]; then
    fatal "Expected YB_LINK to be set to \"static\" or \"dynamic\", got \"${YB_LINK:-}\""
  fi
  export YB_LINK
  readonly YB_LINK
}

validate_build_type() {
  expect_num_args 1 "$@"
  # Local variable named _build_type to avoid a collision with the global build_type variable.
  local _build_type=$1
  if ! is_valid_build_type "$_build_type"; then
    fatal "Invalid build type: '$_build_type'. Valid build types are: ${VALID_BUILD_TYPES[@]}" \
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
  local jenkins_job_name=$( echo "$JOB_NAME" | to_lowercase )
  for _build_type in "${VALID_BUILD_TYPES[@]}"; do
    if [[ "-$jenkins_job_name-" =~ [-_]$_build_type[-_] ]]; then
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
  if [[ -z "${YB_COMPILER_TYPE:-}" ]]; then
    if [[ "$OSTYPE" =~ ^darwin ]]; then
      YB_COMPILER_TYPE=clang
    else
      YB_COMPILER_TYPE=gcc
    fi
    export YB_COMPILER_TYPE
    readonly YB_COMPILER_TYPE
  fi
}

is_clang() {
  if [[ $YB_COMPILER_TYPE == "clang" ]]; then
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

build_compiler_if_necessary() {
  # Sometimes we have to build the compiler before we can run CMake.
  if is_clang && is_linux; then
    log "Building clang before we can run CMake with compiler pointing to clang"
    "$YB_THIRDPARTY_DIR/build_thirdparty.py" llvm
  fi
}

set_compiler_type_based_on_jenkins_job_name() {
  if [[ -n "${YB_COMPILER_TYPE:-}" ]]; then
    if [[ -n "${JOB_NAME:-}" ]]; then
      log "The YB_COMPILER_TYPE variable is already set to '${YB_COMPILER_TYPE}', not setting it" \
          "based on the Jenkins job name."
    fi
  else
    local compiler_type
    local jenkins_job_name=$( echo "$JOB_NAME" | to_lowercase )
    YB_COMPILER_TYPE=""
    for compiler_type in "${VALID_COMPILER_TYPES[@]}"; do
      if [[ "-$jenkins_job_name-" =~ [-_]$compiler_type[-_] ]]; then
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
  validate_compiler_type
  readonly YB_COMPILER_TYPE
  export YB_COMPILER_TYPE
}

validate_compiler_type() {
  local compiler_type
  if [[ $# -eq 0 ]]; then
    if [[ -z ${YB_COMPILER_TYPE:-} ]]; then
      fatal "$FUNCNAME is called with no arguments but YB_COMPILER_TYPE is not set or is empty"
    fi
    compiler_type=$YB_COMPILER_TYPE
  elif [[ $# -eq 1 ]]; then
    compiler_type=$1
  else
    fatal "$FUNCNAME can only be called with 0 or 1 argument, got $# arguments: $*"
  fi

  if [[ ! $compiler_type =~ $VALID_COMPILER_TYPES_RE ]]; then
    fatal "Invalid compiler type: YB_COMPILER_TYPE='$compiler_type'" \
          "(expected one of: ${VALID_COMPILER_TYPES[@]})."
  fi
}

validate_cmake_build_type() {
  expect_num_args 1 "$@"
  local _cmake_build_type=$1
  _cmake_build_type=$( echo "$_cmake_build_type" | tr A-Z a-z )
  if [[ ! "$_cmake_build_type" =~ $VALID_CMAKE_BUILD_TYPES_RE ]]; then
    fatal "Invalid CMake build type (what we're about to pass to our CMake build as" \
          "_cmake_build_type): '$_cmake_build_type'." \
          "Valid CMake build types are: ${VALID_CMAKE_BUILD_TYPES[@]}."
  fi
}

ensure_using_clang() {
  if [[ -n ${YB_COMPILER_TYPE:-} && $YB_COMPILER_TYPE != "clang" ]]; then
    fatal "ASAN/TSAN builds require clang," \
          "but YB_COMPILER_TYPE is already set to '$YB_COMPILER_TYPE'"
  fi
  YB_COMPILER_TYPE="clang"
}

enable_tsan() {
  cmake_opts+=( -DYB_USE_TSAN=1 )
  ensure_using_clang
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
    log "Setting build type to 'debug' by default"
    build_type=debug
  fi

  normalize_build_type
  # We're relying on build_type to set more variables, so make sure it does not change later.
  readonly build_type

  case "$build_type" in
    asan)
      cmake_opts+=( -DYB_USE_ASAN=1 -DYB_USE_UBSAN=1 )
      cmake_build_type=fastdebug
      ensure_using_clang
    ;;
    tsan)
      enable_tsan
      cmake_build_type=fastdebug
    ;;
    tsan_slow)
      enable_tsan
      cmake_build_type=debug
    ;;
    *)
      cmake_build_type=$build_type
  esac
  validate_cmake_build_type "$cmake_build_type"
  readonly cmake_build_type

  if is_mac; then
    if [[ -z ${YB_COMPILER_TYPE:-} ]]; then
      YB_COMPILER_TYPE=clang
    elif [[ $YB_COMPILER_TYPE != "clang" ]]; then
      fatal "YB_COMPILER_TYPE can only be 'clang' on Mac OS X," \
            "found YB_COMPILER_TYPE=$YB_COMPILER_TYPE."
    fi
  elif [[ -z ${YB_COMPILER_TYPE:-} ]]; then
    # The default on Linux.
    YB_COMPILER_TYPE=gcc
  fi

  validate_compiler_type
  readonly YB_COMPILER_TYPE
  export YB_COMPILER_TYPE

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

  if using_ninja; then
    cmake_opts+=( -G Ninja )
    make_program=ninja
    if ! which ninja &>/dev/null; then
      if using_linuxbrew; then
        make_program=$YB_LINUXBREW_DIR/bin/ninja
      elif is_mac; then
        log "Did not find the 'ninja' executable, auto-installing ninja using Homebrew"
        brew install ninja
      fi
    fi
    make_file=build.ninja
  else
    make_program=make
    make_file=Makefile
  fi

  cmake_opts+=( -DCMAKE_MAKE_PROGRAM=$make_program )
}

set_mvn_parameters() {
  if [[ -z ${YB_MVN_LOCAL_REPO:-} ]]; then
    if is_jenkins && is_src_root_on_nfs; then
      YB_MVN_LOCAL_REPO=/n/jenkins/m2_repository
    else
      YB_MVN_LOCAL_REPO=$HOME/.m2/repository
    fi
  fi
  export YB_MVN_LOCAL_REPO

  if [[ -z ${YB_MVN_SETTINGS_PATH:-} ]]; then
    if is_jenkins && is_src_root_on_nfs; then
      YB_MVN_SETTINGS_PATH=/n/jenkins/m2_settings.xml
    else
      YB_MVN_SETTINGS_PATH=$HOME/.m2/settings.xml
    fi
  fi
  export MVN_SETTINGS_PATH

  mvn_common_options=(
    --batch-mode
    -Dmaven.repo.local="$YB_MVN_LOCAL_REPO"
    -Dyb.thirdparty.dir="$YB_THIRDPARTY_DIR"
    -DbinDir="$BUILD_ROOT/bin"
  )
}

# A utility function called by both 'build_yb_java_code' and 'build_yb_java_code_with_retries'.
build_yb_java_code_filter_save_output() {
  set_mvn_parameters

  # --batch-mode hides download progress.
  # We are filtering out some patterns from Maven output, e.g.:
  # [INFO] META-INF/NOTICE already added, skipping
  # [INFO] Downloaded: https://repo.maven.apache.org/maven2/org/codehaus/plexus/plexus-classworlds/2.4/plexus-classworlds-2.4.jar (46 KB at 148.2 KB/sec)
  # [INFO] Downloading: https://repo.maven.apache.org/maven2/org/apache/maven/doxia/doxia-logging-api/1.1.2/doxia-logging-api-1.1.2.jar
  local has_local_output=false # default is output path variable is set by calling function
  if [[ -z ${java_build_output_path:-} ]]; then
    local java_build_output_path=/tmp/yb-java-build-$( get_timestamp ).$$.tmp
    has_local_output=true
  fi
  local mvn_opts=( "${mvn_common_options[@]}" )
  if [[ -f $YB_MVN_SETTINGS_PATH  ]]; then
    mvn_opts+=(
      --settings "$YB_MVN_SETTINGS_PATH"
    )
  elif [[ $YB_MVN_SETTINGS_PATH != $HOME/.m2/settings.xml ]]; then
    log "Maven user settings file specified by YB_MVN_SETTINGS_PATH does not exist:" \
        "'$YB_MVN_SETTINGS_PATH'"
  fi
  if ! is_jenkins; then
    mvn_opts+=( -Dmaven.javadoc.skip )
  fi
  set +e -x  # +e: do not fail on grep failure, -x: print the command to stderr.
  if mvn "${mvn_opts[@]}" "$@" 2>&1 | \
      egrep -v --line-buffered "$MVN_OUTPUT_FILTER_REGEX" | \
      tee "$java_build_output_path"; then
    set +x # stop printing commands
    # We are testing for mvn build failure with grep, since we run mvn with '--fail-never' which
    # always returns success. '--fail-at-end' could have been another possibility, but that mode
    # skips dependent modules so most tests are often not run. Therefore, we resort to grep.
    egrep "BUILD SUCCESS" "$java_build_output_path" &>/dev/null
    local mvn_exit_code=$?
    set -e
    if [[ $has_local_output == "true" ]]; then
      rm -f "$java_build_output_path" # cleaning up
    fi
    log "Java build finished with exit code $mvn_exit_code" # useful for searching in console output
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
  local java_build_output_path=/tmp/yb-java-build-$( get_timestamp ).$$.tmp
  build_yb_java_code_filter_save_output "$@"
  local mvn_exit_code=$?
  rm -f "$java_build_output_path"
  return $mvn_exit_code
}

build_yb_java_code_with_retries() {
  local java_build_output_path=/tmp/yb-java-build-$( get_timestamp ).$$.tmp
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

    let attempt+=1
  done
  return 1
}

# Create a directory on an ephemeral drive and link it into the given target location. If there are
# no ephemeral drives, create the directory in place.
# Parameters:
#   target_path - The target path to create the directory or symlink at.
#   directory_identifier - A unique identifier that will be used in naming the new directory
#                          created on an ephemeral drive.
create_dir_on_ephemeral_drive() {
  expect_num_args 2 "$@"
  local target_path=$1
  local directory_identifier=$2

  if [[ -z ${num_ephemeral_drives:-} ]]; then
    # Collect available ephemeral drives. This is only done once.
    local ephemeral_mountpoint
    # EPHEMERAL_DRIVES_FILTER_REGEX is not supposed to be anchored in the end, so we need to add
    # a "$" to filter ephemeral mountpoints correctly.
    ephemeral_drives=()
    for ephemeral_mountpoint in $EPHEMERAL_DRIVES_GLOB; do
      if [[ -d $ephemeral_mountpoint &&
            $ephemeral_mountpoint =~ $EPHEMERAL_DRIVES_FILTER_REGEX$ ]]; then
        ephemeral_drives+=( "$ephemeral_mountpoint" )
      fi
    done

    declare -r -i num_ephemeral_drives=${#ephemeral_drives[@]}  # "-r -i" means readonly integer.
  fi

  if [[ $num_ephemeral_drives -eq 0 ]]; then
    if [[ -n ${YB_VERBOSE:-} && ! -d $target_path ]]; then
      log "No ephemeral drives found, creating directory '$target_path' in place."
    fi
    mkdir_safe "$target_path"
  else
    local random_drive=${ephemeral_drives[$RANDOM % $num_ephemeral_drives]}
    local actual_dir=$random_drive/${USER}__$jenkins_job_and_build/$directory_identifier
    mkdir_safe "$actual_dir"

    # Create the parent directory that we'll be creating a link in, if necessary.
    if [[ ! -d ${target_path%/*} ]]; then
      log "Directory $target_path does not exist, creating it before creating a symlink inside."
      mkdir_safe "${target_path%/*}"
    fi

    ln -s "$actual_dir" "$target_path"
    log "Created '$target_path' as a symlink to an ephemeral drive location '$actual_dir'."
  fi
}

mkdir_safe() {
  expect_num_args 1 "$@"
  local dir_path=$1
  # Check if this is a broken link.
  if [[ -h $dir_path && ! -d $dir_path ]]; then
    unlink "$dir_path"
  fi
  mkdir -p "$dir_path"
}

# Skip the most part of the normal C++ build output. Still keep the "100%" lines so we can see
# if the build runs to completion. This only filters stdin, so it is expected that stderr is
# redirected to stdout when invoking the C++ build.
filter_boring_cpp_build_output() {
  egrep -v --line-buffered "\
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
ssh: Could not resolve hostname build-workers-.*: Name or service not known"
}

remove_path_entry() {
  expect_num_args 1 "$@"
  local path_entry=$1
  local prev_path=""
  # Remove all occurrences of the given entry.
  while  [[ $PATH != $prev_path ]]; do
    prev_path=$PATH
    PATH=:$PATH:
    PATH=${PATH//:$path_entry:/:}
    PATH=${PATH#:}
    PATH=${PATH%:}
  done
  export PATH
}

# Removes the ccache wrapper directory from PATH so we can find the real path to a compiler, e.g.
# /usr/bin/gcc instead of /usr/lib64/ccache/gcc.  This is expected to run in a subshell so that we
# don't make any unexpected changes to the script's PATH.
# TODO: how to do this properly on Mac OS X?
remove_ccache_dir_from_path() {
  remove_path_entry /usr/lib64/ccache
}

# Given a compiler type, e.g. gcc or clang, find the actual compiler executable (not a wrapper
# provided by ccache).  Takes into account YB_GCC_PREFIX and YB_CLANG_PREFIX variables that allow to
# use custom gcc and clang installations. Sets cc_executable and cxx_executable variables. This is
# used in compiler-wrapper.sh.
find_compiler_by_type() {
  compiler_type=$1
  validate_compiler_type "$1"
  local compiler_type=$1
  unset cc_executable
  unset cxx_executable
  case "$compiler_type" in
    gcc)
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
    ;;
    clang)
      if [[ -n ${YB_CLANG_PREFIX:-} ]]; then
        if [[ ! -d $YB_CLANG_PREFIX/bin ]]; then
          fatal "Directory YB_CLANG_PREFIX/bin ($YB_CLANG_PREFIX/bin) does not exist"
        fi
        cc_executable=$YB_CLANG_PREFIX/bin/clang
      elif [[ $OSTYPE =~ ^darwin ]]; then
        cc_executable=/usr/bin/clang
      else
        local clang_path
        local clang_found=false
        local clang_paths_to_try=(
          "$YB_THIRDPARTY_DIR/clang-toolchain/bin/clang"
          # clang is present in this location in pre-built third-party archives built before
          # the transition to Linuxbrew (https://phabricator.dev.yugabyte.com/D982). This can be
          # removed when the transition is complete.
          "$YB_THIRDPARTY_DIR/installed/common/bin/clang"
        )
        for clang_path in "${clang_paths_to_try[@]}"; do
          if [[ -f $clang_path ]]; then
            cc_executable=$clang_path
            clang_found=true
            break
          fi
        done
        if ! "$clang_found"; then
          fatal "Failed to find clang at the following locations: ${clang_paths_to_try[@]}"
        fi
      fi
      if [[ -z ${cxx_executable:-} ]]; then
        cxx_executable=$cc_executable++  # clang -> clang++
      fi
    ;;
    zapcc)
      if [[ -n ${YB_ZAPCC_INSTALL_PATH:-} ]]; then
        cc_executable=$YB_ZAPCC_INSTALL_PATH/bin/zapcc
        cxx_executable=$YB_ZAPCC_INSTALL_PATH/bin/zapcc++
      else
        cc_executable=zapcc
        cxx_executable=zapcc++
      fi
    ;;
    *)
      fatal "Unknown compiler type '$compiler_type'"
  esac
  local compiler_var_name
  for compiler_var_name in cc_executable cxx_executable; do
    if [[ -n ${!compiler_var_name:-} ]]; then
      local compiler_path=${!compiler_var_name}
      if [[ ! -x $compiler_path && $compiler_path =~ ^[a-z+]+$ ]]; then
        # This is a plain "gcc/g++/clang/clang++" compiler command name. Try to find the exact
        # compiler path using the "which" command.
        set +e
        compiler_path=$( remove_ccache_dir_from_path && which "${!compiler_var_name}" )
        if [[ $? -ne 0 ]]; then
          # "which" did not work, revert to the old value.
          compiler_path=${!compiler_var_name}
        fi
        set -e
      fi

      if [[ ! -x $compiler_path ]]; then
        fatal "Compiler executable does not exist at the path we set $compiler_var_name to" \
              "(possibly applying 'which' expansion): $compiler_path" \
              "(trying to use compiler type '$compiler_type')."
      fi
      eval $compiler_var_name=\"$compiler_path\"
    fi
  done
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

popd() {
  command popd "$@" > /dev/null
}

detect_linuxbrew() {
  YB_USING_LINUXBREW=false
  local user_specified_linuxbrew_dir=${YB_LINUXBREW_DIR:-}
  unset YB_LINUXBREW_DIR
  unset YB_LINUXBREW_LIB_DIR
  if ! is_linux; then
    return
  fi
  local candidates=(
    "$HOME/.linuxbrew-yb-build"
  )

  local version_for_jenkins_file=$YB_SRC_ROOT/thirdparty/linuxbrew_version_for_jenkins.txt
  if [[ -f $version_for_jenkins_file ]]; then
    local version_for_jenkins=$( read_file_and_trim "$version_for_jenkins_file" )
    preferred_linuxbrew_dir="$SHARED_LINUXBREW_BUILDS_DIR/linuxbrew_$version_for_jenkins"
    if [[ -d $preferred_linuxbrew_dir ]]; then
      if is_jenkins_user; then
        # If we're running on Jenkins (or building something for consumption by Jenkins under the
        # "jenkins" user), then the "Linuxbrew for Jenkins" directory takes precedence.
        candidates=( "$preferred_linuxbrew_dir" "${candidates[@]}" )
      else
        # Otherwise, the user's local Linuxbrew build takes precedence.
        candidates=( "${candidates[@]}" "$preferred_linuxbrew_dir" )
      fi
    elif is_jenkins; then
      log "Warning: Linuxbrew directory referenced by '$version_for_jenkins_file' does not" \
          "exist: '$preferred_linuxbrew_dir', will attempt to use other location."
    fi
  elif is_jenkins; then
    log "Warning: '$version_for_jenkins_file' does not exist"
  fi

  if [[ -n $user_specified_linuxbrew_dir ]]; then
    candidates=( "$user_specified_linuxbrew_dir" "${candidates[@]}" )
  fi

  local linuxbrew_dir
  for linuxbrew_dir in "${candidates[@]}"; do
    if [[ -d "$linuxbrew_dir" &&
          -d "$linuxbrew_dir/bin" &&
          -d "$linuxbrew_dir/lib" &&
          -d "$linuxbrew_dir/include" ]]; then
      export YB_LINUXBREW_DIR=$linuxbrew_dir
      YB_USING_LINUXBREW=true
      YB_LINUXBREW_LIB_DIR=$YB_LINUXBREW_DIR/lib
      break
    fi
  done
}

using_linuxbrew() {
  if [[ $YB_USING_LINUXBREW == true ]]; then
    return 0
  else
    return 1
  fi
}

using_ninja() {
  if [[ ${YB_USE_NINJA:-} == "1" ]]; then
    return 0
  else
    return 1
  fi
}

set_build_env_vars() {
  if using_linuxbrew; then
    # We need to add Linuxbrew's bin directory to PATH so that we can find the right compiler and
    # linker.
    export PATH=$YB_LINUXBREW_DIR/bin:$PATH
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

detect_num_cpus_and_set_make_parallelism() {
  detect_num_cpus
  if [[ -z ${YB_MAKE_PARALLELISM:-} ]]; then
    if [[ ${YB_REMOTE_BUILD:-} == "1" ]]; then
      declare -i num_build_workers=$( wc -l "$YB_BUILD_WORKERS_FILE" | awk '{print $1}' )
      # Add one to the number of workers so that we cause the auto-scaling group to scale up a bit
      # by stressing the CPU on each worker a bit more.
      declare -i effective_num_build_workers=$(( $num_build_workers + 1 ))

      # However, make sure this number is within a reasonable range.
      if [[ $effective_num_build_workers -lt $MIN_EFFECTIVE_NUM_BUILD_WORKERS ]]; then
        effective_num_build_workers=$MIN_EFFECTIVE_NUM_BUILD_WORKERS
      fi
      if [[ $effective_num_build_workers -gt $MAX_EFFECTIVE_NUM_BUILD_WORKERS ]]; then
        effective_num_build_workers=$MAX_EFFECTIVE_NUM_BUILD_WORKERS
      fi

      YB_MAKE_PARALLELISM=$(( $effective_num_build_workers * $YB_NUM_CORES_PER_BUILD_WORKER ))
    else
      YB_MAKE_PARALLELISM=$YB_NUM_CPUS
    fi
  fi
  export YB_MAKE_PARALLELISM
}

run_sha256sum_on_mac() {
  shasum --portable --algorithm 256 "$@"
}

verify_sha256sum() {
  local common_args="--check"
  if [[ $OSTYPE =~ darwin ]]; then
    run_sha256sum_on_mac $common_args "$@"
  else
    sha256sum --quiet $common_args "$@"
  fi
}

compute_sha256sum() {
  (
    if [[ $OSTYPE =~ darwin ]]; then
      run_sha256sum_on_mac "$@"
    else
      sha256sum "$@"
    fi
  ) | awk '{print $1}'
}

validate_thirdparty_dir() {
  ensure_directory_exists "$YB_THIRDPARTY_DIR/build_definitions"
  ensure_directory_exists "$YB_THIRDPARTY_DIR/patches"
  ensure_file_exists "$YB_THIRDPARTY_DIR/build_thirdparty.py"
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
  if [[ -n ${BUILD_ID:-} && -n ${JOB_NAME:-} ]] && is_jenkins_user; then
    return 0  # Yes, we're running on Jenkins.
  fi
  return 1  # Probably running locally.
}

# Check if we're in a Jenkins master build (as opposed to a Phabricator build).
is_jenkins_master_build() {
  if [[ -n ${JOB_NAME:-} && $JOB_NAME = *-master-* ]]; then
    return 0
  fi
  return 1
}

# Check if we're using an NFS partition in YugaByte's build environment.
is_src_root_on_nfs() {
  if [[ $YB_SRC_ROOT =~ ^/(n|z)/ ]]; then
    return 0
  fi
  return 1
}

is_remote_build() {
  if [[ ${YB_REMOTE_BUILD:-} == "1" ]]; then
    return 0  # "true" return value
  fi
  return 1  # "false" return value
}

# This is used for escaping command lines for remote execution.
# From StackOverflow: https://goo.gl/sTKReB
# Using this approach: "Put the whole string in single quotes. This works for all chars except
# single quote itself. To escape the single quote, close the quoting before it, insert the single
# quote, and re-open the quoting."
#
escape_cmd_line() {
  escape_cmd_line_rv=""
  for arg in "$@"; do
    escape_cmd_line_rv+=" '"${arg/\'/\'\\\'\'}"'"
    # This should be equivalent to the sed command below.  The quadruple backslash encodes one
    # backslash in the replacement string. We don't need that in the pure-bash implementation above.
    # sed -e "s/'/'\\\\''/g; 1s/^/'/; \$s/\$/'/"
  done
  # Remove the leading space if necessary.
  escape_cmd_line_rv=${escape_cmd_line_rv# }
}

run_remote_cmd() {
  local build_host=$1
  local executable=$2
  shift 2
  local escape_cmd_line_rv
  escape_cmd_line "$@"
  ssh "$build_host" \
      "'$YB_BUILD_SUPPORT_DIR/remote_cmd.sh' '$PWD' '$PATH' '$executable' $escape_cmd_line_rv"
}

# Run the build command (cmake / make) on the appropriate host. This is localhost in most cases.
# However, in a remote build, we ensure we run this command on the "distributed build master host"
# machine, as there are some issues with running cmake or make over NFS (e.g. stale file handles).
run_build_cmd() {
  if is_remote_build && [[ $HOSTNAME != $DISTRIBUTED_BUILD_MASTER_HOST ]]; then
    run_remote_cmd "$DISTRIBUTED_BUILD_MASTER_HOST" "$@"
  else
    "$@"
  fi
}

configure_remote_build() {
  # Automatically set YB_REMOTE_BUILD in an NFS GCP environment.
  if [[ -z ${YB_NO_REMOTE_BUILD:-} ]] && is_running_on_gcp && is_src_root_on_nfs; then
    if [[ -z ${YB_REMOTE_BUILD:-} ]]; then
      log "Automatically enabling distributed build (running in an NFS GCP environment). " \
          "Use YB_NO_REMOTE_BUILD (or the --no-remote ybd option) to disable this behavior."
      export YB_REMOTE_BUILD=1
    else
      log "YB_REMOTE_BUILD already defined: '$YB_REMOTE_BUILD', not enabling it automatically," \
          "even though we would in this case."
    fi
  elif is_jenkins; then
    # Make it easier to diagnose why we're not using the distributed build. Only enable this on
    # Jenkins to avoid confusing output during development.
    log "Not using remote / distributed build:" \
        "YB_NO_REMOTE_BUILD=${YB_NO_REMOTE_BUILD:-undefined}. See additional diagnostics below."
    is_running_on_gcp && log "Running on GCP." || log "This is not GCP."
    if is_src_root_on_nfs; then
      log "YB_SRC_ROOT ($YB_SRC_ROOT) appears to be on NFS in YugaByte's distributed build setup."
    fi
  fi
}

yb_edition_detected=false

validate_edition() {
  if [[ ! $YB_EDITION =~ ^(community|enterprise)$ ]]; then
    fatal "The YB_EDITION environment variable has an invalid value: '$YB_EDITION'" \
          "(must be either 'community' or 'enterprise')."
  fi
}

detect_edition() {
  if "$yb_edition_detected"; then
    return
  fi
  yb_edition_detected=true

  # If we haven't detected edition based on BUILD_ROOT, let's do that based on existence of the
  # enterprise source directory.
  if [[ -z ${YB_EDITION:-} ]]; then
    if is_jenkins && [[ $JOB_NAME =~ -community(-|$) ]]; then
      YB_EDITION=community
      log "Detecting YB_EDITION: $YB_EDITION based on Jenkins job name: $JOB_NAME"
    elif is_jenkins && [[ $JOB_NAME =~ -enterprise(-|$) ]]; then
      YB_EDITION=enterprise
      log "Detecting YB_EDITION: $YB_EDITION based on Jenkins job name: $JOB_NAME"
    elif [[ -d $YB_ENTERPRISE_ROOT ]]; then
      YB_EDITION=enterprise
      log "Detected YB_EDITION: $YB_EDITION based on existence of '$YB_ENTERPRISE_ROOT'"
    else
      YB_EDITION=community
      log "Detected YB_EDITION: $YB_EDITION"
    fi
  fi

  if [[ $YB_EDITION == "enterprise" && ! -d $YB_ENTERPRISE_ROOT ]]; then
    fatal "YB_EDITION is set to '$YB_EDITION' but the directory '$YB_ENTERPRISE_ROOT'" \
          "does not exist"
  fi

  readonly YB_EDITION
  export YB_EDITION
}

set_yb_src_root() {
  YB_SRC_ROOT=$1
  YB_BUILD_SUPPORT_DIR=$YB_SRC_ROOT/build-support
  if [[ ! -d $YB_SRC_ROOT ]]; then
    fatal "YB_SRC_ROOT directory '$YB_SRC_ROOT' does not exist"
  fi
  YB_ENTERPRISE_ROOT=$YB_SRC_ROOT/ent
  YB_COMPILER_WRAPPER_CC=$YB_BUILD_SUPPORT_DIR/compiler-wrappers/cc
  YB_COMPILER_WRAPPER_CXX=$YB_BUILD_SUPPORT_DIR/compiler-wrappers/c++
}

read_file_and_trim() {
  expect_num_args 1 "$@"
  local file_name=$1
  if [[ -f $file_name ]]; then
    cat "$file_name" | sed -e 's/^[[:space:]]*//; s/[[:space:]]*$//'
  else
    log "File '$file_name' does not exist"
    return 1
  fi
}

# In our internal environment we build third-party dependencies in separate directories on NFS
# so that we can use them across many builds.
find_thirdparty_dir() {
  found_shared_thirdparty_dir=false
  local parent_dir_for_shared_thirdparty=$NFS_PARENT_DIR_FOR_SHARED_THIRDPARTY
  if [[ ! -d $parent_dir_for_shared_thirdparty ]]; then
    log "Parent directory for shared third-party directories" \
        "('$NFS_PARENT_DIR_FOR_SHARED_THIRDPARTY') does not exist, cannot use pre-built" \
        "third-party directory from there."
    return
  fi

  local version=$(
    read_file_and_trim "$YB_SRC_ROOT/thirdparty/version_for_jenkins_${short_os_name}.txt"
  )
  local thirdparty_dir_suffix="yugabyte-thirdparty-${version}/thirdparty"
  local existing_thirdparty_dir="${parent_dir_for_shared_thirdparty}/${thirdparty_dir_suffix}"
  if [[ -d $existing_thirdparty_dir ]]; then
    log "Using existing third-party dependencies from $existing_thirdparty_dir"
    if is_jenkins; then
      log "Cleaning the old dedicated third-party dependency build in '$YB_SRC_ROOT/thirdparty'"
      unset YB_THIRDPARTY_DIR
      "$YB_SRC_ROOT/thirdparty/clean_thirdparty.sh" --all
    fi
    export YB_THIRDPARTY_DIR=$existing_thirdparty_dir
    found_shared_thirdparty_dir=true
    export NO_REBUILD_THIRDPARTY=1
    return
  fi

  log "Even though the top-level directory '$parent_dir_for_shared_thirdparty'" \
      "exists, we could not find a prebuilt shared third-party directory there that exists. " \
      "Falling back to building our own third-party dependencies."
}

handle_predefined_build_root_quietly=false

handle_predefined_build_root() {
  expect_num_args 0 "$@"
  if [[ -z ${predefined_build_root:-} ]]; then
    return
  fi

  if [[ -d $predefined_build_root ]]; then
    predefined_build_root=$( cd "$predefined_build_root" && pwd )
  fi

  if [[ $predefined_build_root != $YB_BUILD_INTERNAL_PARENT_DIR/* && \
        $predefined_build_root != $YB_BUILD_EXTERNAL_PARENT_DIR/* ]]; then
    # Sometimes $predefined_build_root contains symlinks on its path.
    $YB_SRC_ROOT/build-support/validate_build_root.py \
      "$predefined_build_root" \
      "$YB_BUILD_INTERNAL_PARENT_DIR" \
      "$YB_BUILD_EXTERNAL_PARENT_DIR"
  fi

  local basename=${predefined_build_root##*/}

  if [[ $basename =~ $BUILD_ROOT_BASENAME_RE ]]; then
    local _build_type=${BASH_REMATCH[1]}
    local _compiler_type=${BASH_REMATCH[2]}
    local _linking_type=${BASH_REMATCH[3]}
    local _edition=${BASH_REMATCH[4]}
    local _dash_ninja=${BASH_REMATCH[5]}
  else
    fatal "Could not parse build root directory name '$basename'" \
          "(full path: '$predefined_build_root'). Expected to match '$BUILD_ROOT_BASENAME_RE'."
  fi

  if [[ -z ${build_type:-} ]]; then
    if ! "$handle_predefined_build_root_quietly"; then
      log "Setting build type to '$build_type' based on predefined build root ('$basename')"
    fi
    build_type=$_build_type
    validate_build_type "$build_type"
  elif [[ $build_type != $_build_type ]]; then
    fatal "Build type from the build root ('$_build_type' from '$predefined_build_root') does " \
          "not match current build type ('$build_type')."
  fi

  if [[ -z ${YB_COMPILER_TYPE:-} ]]; then
    export YB_COMPILER_TYPE=$_compiler_type
    if ! "$handle_predefined_build_root_quietly"; then
      log "Automatically setting compiler type to '$YB_COMPILER_TYPE' based on predefined build" \
          "root ('$basename')"
    fi
  elif [[ $YB_COMPILER_TYPE != $_compiler_type ]]; then
    fatal "Compiler type from the build root ('$_compiler_type' from '$predefined_build_root') " \
          "does not match YB_COMPILER_TYPE ('$YB_COMPILER_TYPE')."
  fi

  export YB_USE_NINJA=${YB_USE_NINJA:-}
  if [[ $_dash_ninja == "-ninja" && -z ${YB_USE_NINJA:-} ]]; then
    if ! "$handle_predefined_build_root_quietly"; then
      log "Setting YB_USE_NINJA to 1 based on predefined build root ('$basename')"
    fi
    export YB_USE_NINJA=1
  elif [[ $_dash_ninja == "-ninja" && $YB_USE_NINJA != "1" || \
          $_dash_ninja != "-ninja" && $YB_USE_NINJA == "1" ]]; then
    fatal "The use of ninja from build root ('$predefined_build_root') does not match that" \
          "of the YB_USE_NINJA env var ('$YB_USE_NINJA')"
  fi

  if [[ -z ${YB_EDITION:-} ]]; then
    export YB_EDITION=$_edition
    if ! "$handle_predefined_build_root_quietly"; then
      log "Detected YB_EDITION: '$YB_EDITION' based on predefined build root ('$basename')"
    fi
  elif [[ $YB_EDITION != $_edition ]]; then
    fatal "Edition from the build root ('$_edition' from '$predefined_build_root') " \
          "does not match YB_EDITION ('$YB_EDITION')."
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

detect_os() {
  short_os_name="unknown_os"
  if is_mac; then
    short_os_name="mac"
  elif is_linux; then
    short_os_name="linux"
  fi
}

# Assigns a random "test invocation id" that allows to kill stuck processes corresponding to this
# instance of a particular test or the whole test suite.
set_test_invocation_id() {
  local timestamp=$( get_timestamp_for_filenames )
  export YB_TEST_INVOCATION_ID=test_invocation_${timestamp}_${RANDOM}_${RANDOM}_$$
}

# Kills any processes that have YB_TEST_INVOCATION_ID in their command line. Sets
# killed_stuck_processes=true in case that happens.
kill_stuck_processes() {
  expect_num_args 0 "$@"
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
      killed_stuck_processes=true
      log "Killed process $pid with SIGKILL."
    fi
  done
}

handle_build_root_from_current_dir() {
  if [[ ${YB_IS_THIRDPARTY_BUILD:-} == "1" ]]; then
    return
  fi
  local handle_predefined_build_root_quietly=true
  local d=$PWD
  while [[ $d != "/" && $d != "" ]]; do
    basename=${d##*/}
    if [[ $basename =~ $BUILD_ROOT_BASENAME_RE ]]; then
      predefined_build_root=$d
      handle_predefined_build_root
      return
    fi
    d=${d%/*}
  done

  fatal "Working directory of the compiler '$PWD' is not within a valid YugaByte build root."
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
  git ls-files '*.py' | xargs -P 8 -n 1 "$YB_BUILD_SUPPORT_DIR/check_python_syntax.py"
  popd
}

add_python_wrappers_dir_to_path() {
  # Make sure the Python wrappers directory is the first on PATH
  remove_path_entry "$YB_PYTHON_WRAPPERS_DIR"
  export PATH=$YB_PYTHON_WRAPPERS_DIR:$PATH
}

activate_virtualenv() {
  local virtualenv_parent_dir=$YB_BUILD_PARENT_DIR
  local virtualenv_dir=$virtualenv_parent_dir/$YB_VIRTUALENV_BASENAME
  if [[ ! $virtualenv_dir = */$YB_VIRTUALENV_BASENAME ]]; then
    fatal "Internal error: virtualenv_dir ('$virtualenv_dir') must end" \
          "with YB_VIRTUALENV_BASENAME ('$YB_VIRTUALENV_BASENAME')"
  fi
  if [[ ${YB_RECREATE_VIRTUALENV:-} == "1" && -d $virtualenv_dir ]]; then
    log "YB_RECREATE_VIRTUALENV is set, deleting virtualenv at '$virtualenv_dir'"
    rm -rf "$virtualenv_dir"
    unset YB_RECREATE_VIRTUALENV
  fi
  if [[ ! -d $virtualenv_dir ]]; then
    if [[ -n ${VIRTUAL_ENV:-} && -f $VIRTUAL_ENV/bin/activate ]]; then
      local old_virtual_env=$VIRTUAL_ENV
      # Re-activate and deactivate the other virtualenv we're in. Otherwise the deactivate
      # function might not even be present in our current shell. This is necessary because otherwise
      # the --user installation below will fail.
      set +eu
      . "$VIRTUAL_ENV/bin/activate"
      deactivate
      set -eu
      # Not clear why deactivate does not do this.
      remove_path_entry "$old_virtual_env/bin"
    fi
    # We need to be using system python to install the virtualenv module or create a new virtualenv.
    pip2 install virtualenv --user
    (
      set -x
      mkdir -p "$virtualenv_parent_dir"
      cd "$virtualenv_parent_dir"
      python2 -m virtualenv "$YB_VIRTUALENV_BASENAME"
    )
  fi
  set +u
  . "$virtualenv_dir"/bin/activate
  set -u
  pip2 install -r "$YB_SRC_ROOT/requirements.txt"
  add_python_wrappers_dir_to_path
}

check_python_interpreter_version() {
  expect_num_args 3 "$@"
  local python_interpreter=$1
  local expected_major_version=$2
  local minor_version_lower_bound=$3
  # Get the Python interpreter version. Filter out debug output we may be adding if
  # YB_PYTHON_WRAPPER_DEBUG is set.
  local version_str=$( "$python_interpreter" --version 2>&1 >/dev/null | grep -v "Invoking Python" )
  version_str=${version_str#Python }
  local actual_major_version=${version_str%%.*}
  local version_str_without_major=${version_str#*.}
  local actual_minor_version=${version_str_without_major%%.*}
  if [[ $actual_major_version -ne $expected_major_version ]]; then
    fatal "Expected major version for Python interpreter '$python_interpreter' to be" \
          "'$expected_major_version', found '$actual_major_version'. Full Python version:" \
          "'$version_str'."
  fi
  if [[ $actual_minor_version -lt $minor_version_lower_bound ]]; then
    fatal "Expected minor version for Python interpreter '$python_interpreter' to be at least " \
          "'$minor_version_lower_bound', found '$actual_minor_version'. Full Python version:" \
          "'$version_str'."
  fi
}

check_python_interpreter_versions() {
  check_python_interpreter_version python2 2 7
  if is_mac; then
    local python_interpreter_basename
    for python_interpreter_basename in python python2 python 2.7 python3; do
      local homebrew_interpreter_path=/usr/local/bin/$python_interpreter_basename
      if [[ -e $homebrew_interpreter_path ]]; then
        if [[ ! -L $homebrew_interpreter_path ]]; then
          fatal "$homebrew_interpreter_path exists but is not a symlink." \
                "Broken Homebrew installation?"
        fi
        local link_target=$( readlink "$homebrew_interpreter_path" )
        if [[ $link_target == /usr/bin/* ]]; then
          fatal "Found symlink  $homebrew_interpreter_path -> $link_target." \
                "Broken Homebrew installation?"
        fi
      fi
    done
  fi
}

# -------------------------------------------------------------------------------------------------
# Initialization
# -------------------------------------------------------------------------------------------------

detect_os

# This script is expected to be in build-support, a subdirectory of the repository root directory.
set_yb_src_root "$( cd "$( dirname "$BASH_SOURCE" )"/.. && pwd )"

if [[ $YB_SRC_ROOT == */ ]]; then
  fatal "YB_SRC_ROOT ends with '/' (not allowed): '$YB_SRC_ROOT'"
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
  fatal "Could not determine YB source directory from '$BASH_SOURCE':" \
        "$YB_BUILD_SUPPORT_DIR does not exist."
fi

using_default_thirdparty_dir=false
if [[ -z ${YB_THIRDPARTY_DIR:-} ]]; then
  YB_THIRDPARTY_DIR=$YB_SRC_ROOT/thirdparty
  using_default_thirdparty_dir=true
fi

readonly YB_DEFAULT_CMAKE_OPTS=(
  "-DCMAKE_C_COMPILER=$YB_COMPILER_WRAPPER_CC"
  "-DCMAKE_CXX_COMPILER=$YB_COMPILER_WRAPPER_CXX"
)

YB_PYTHON_WRAPPERS_DIR=$YB_BUILD_SUPPORT_DIR/python-wrappers

if ! "${yb_is_python_wrapper_script:-false}"; then
  detect_linuxbrew
  add_python_wrappers_dir_to_path
fi

# End of initialization.
# -------------------------------------------------------------------------------------------------
