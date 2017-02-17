#@IgnoreInspection BashAddShebang

# Copyright (c) YugaByte, Inc.

# This is common between build and test scripts.

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
regex_from_list() {
  expect_some_args "$@"
  local regex=""
  # no quotes around $@ on purpose: we want to break arguments containing spaces.
  for item in $@; do
    if [[ -z $item ]]; then
      continue
    fi
    if [[ -n $regex ]]; then
      regex+="|"
    fi
    regex+="$item"
  done
  echo "^($regex)$"
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
readonly VALID_BUILD_TYPES_RE=$( regex_from_list "${VALID_BUILD_TYPES[@]}" )

# Valid values of CMAKE_BUILD_TYPE passed to the top-level CMake build. This is the same as the
# above with the exclusion of ASAN/TSAN.
readonly VALID_CMAKE_BUILD_TYPES=(
  debug
  fastdebug
  profile_build
  profile_gen
  release
)
readonly VALID_CMAKE_BUILD_TYPES_RE=$( regex_from_list "${VALID_CMAKE_BUILD_TYPES[@]}" )

readonly VALID_COMPILER_TYPES=( gcc clang )
readonly VALID_COMPILER_TYPES_RE=$( regex_from_list "${VALID_COMPILER_TYPES[@]}" )

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
    log "$calling_func_name expects $caller_expected_num_args arguments, got $#."
    if [[ $# -gt 0 ]]; then
      log "Actual arguments:"
      local arg
      for arg in "$@"; do
        log "  - $arg"
      done
    fi
    exit 1
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
  BUILD_ROOT=$YB_SRC_ROOT/build/$build_type-$YB_COMPILER_TYPE-$YB_LINK

  normalize_build_root

  if "$make_build_root_readonly"; then
    readonly BUILD_ROOT
  fi
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
  if [[ -z $BUILD_ROOT ]]; then
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

determine_linking_type() {
  if [[ -z "${YB_LINK:-}" ]]; then
    YB_LINK=dynamic
  fi
  if [[ ! "${YB_LINK:-}" =~ ^(static|dynamic)$ ]]; then
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
    local _build_type_regex="-$_build_type-"
    if [[ "-$jenkins_job_name-" =~ $_build_type_regex ]]; then
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
    $YB_THIRDPARTY_DIR/build-thirdparty.sh llvm
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
      local compiler_type_regex="\\b$compiler_type\\b"
      if [[ "$jenkins_job_name" =~ $compiler_type_regex ]]; then
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

  expect_vars_to_be_set build_type
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
    if [[ -z "${YB_COMPILER_TYPE:-}" ]]; then
      YB_COMPILER_TYPE=clang
    elif [[ "$YB_COMPILER_TYPE" != "clang" ]]; then
      fatal "YB_COMPILER_TYPE can only be 'clang' on Mac OS X," \
            "found YB_COMPILER_TYPE=$YB_COMPILER_TYPE."
    fi
  elif [[ -z "${YB_COMPILER_TYPE:-}" ]]; then
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
}

# utility function called by both 'build_yb_java_code' and 'build_yb_java_code_with_retries'
build_yb_java_code_filter_save_output() {
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
  set +e -x  # do not fail on grep failure; print the command to stderr.
  if mvn "$@" --batch-mode 2>&1 | \
      egrep -v '\[INFO\] (Download(ing|ed): |[^ ]+ already added, skipping$)' | \
      egrep -v '^Generating .*[.]html[.][.][.]$' | \
      tee "$java_build_output_path"; then
    set +x # stop printing commands
    # We are testing for mvn build failure with grep
    #   since we run mvn with '--fail-never' which always returns success
    #   '--fail-at-end' is another possibility but that skips dependent modules
    #   so most tests are often not run -- therefore we resort to grep
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
  log "ERROR: Java build finished but build command failed so log cannot be processed"
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
      echo "Java build attempt $attempt failed due to temporary connectivity issues, re-trying."
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
  egrep -v "^(\[ *[0-9]{1,2}%\] +)*(Building C(XX)? object |\
Running C[+][+] protocol buffer compiler (with YRPC plugin )?on |\
Linking CXX ((static|shared) library|executable) |\
Scanning dependencies of target |\
Built target |[[:space:]]+CC(LD)?[[:space:]]+)[[:graph:]]+$"
}

# Removes the ccache wrapper directory from PATH so we can find the real path to a compiler, e.g.
# /usr/bin/gcc instead of /usr/lib64/ccache/gcc.  This is expected to run in a subshell so that we
# don't make any unexpected changes to the script's PATH.
# TODO: how to do this properly on Mac OS X?
remove_ccache_dir_from_path() {
  PATH=:$PATH:
  export PATH=${PATH//:\/usr\/lib64\/ccache:/:}
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
          "$YB_THIRDPARTY_DIR/installed/bin/clang"
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
pushd () {
  local dir_name=$1
  if [[ ! -d $dir_name ]]; then
    fatal "Directory '$dir_name' does not exist"
  fi
  command pushd "$@" > /dev/null
}

popd () {
  command popd "$@" > /dev/null
}

detect_linuxbrew() {
  YB_USING_LINUXBREW=false
  unset YB_LINUXBREW_DIR
  unset YB_LINUXBREW_LIB_DIR
  if ! is_linux; then
    return
  fi
  local d=$HOME/.linuxbrew-yb-build
  if [[ -d "$d" &&
        -d "$d/bin" &&
        -d "$d/lib" &&
        -d "$d/include" ]]; then
    export YB_LINUXBREW_DIR=$d
    YB_USING_LINUXBREW=true
    YB_LINUXBREW_LIB_DIR=$YB_LINUXBREW_DIR/lib
  fi
}

using_linuxbrew() {
  if [[ $YB_USING_LINUXBREW == true ]]; then
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
  if [[ ${YB_NUM_CPUS:-} =~ ^[0-9]+$ ]]; then
    # Already detected.
    return
  fi

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
}

run_sha256sum_on_mac() {
  shasum --portable --algorithm 256 "$@"
}

verify_sha256sum() {
  if [[ $OSTYPE =~ darwin ]]; then
    run_sha256sum_on_mac "$@"
  else
    sha256sum --quiet "$@"
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

# -------------------------------------------------------------------------------------------------
# Initialization
# -------------------------------------------------------------------------------------------------

# This script is expected to be in build-support.
readonly YB_SRC_ROOT=$( cd "$( dirname "$BASH_SOURCE" )"/.. && pwd )

if [[ ! -d $YB_SRC_ROOT/build-support ]]; then
  fatal "Could not determine YB source directory from '$BASH_SOURCE':" \
        "$YB_SRC_ROOT/build-support does not exist."
fi

readonly YB_THIRDPARTY_DIR=$YB_SRC_ROOT/thirdparty
readonly YB_COMPILER_WRAPPER_CC=$YB_SRC_ROOT/build-support/compiler-wrappers/cc
readonly YB_COMPILER_WRAPPER_CXX=$YB_SRC_ROOT/build-support/compiler-wrappers/c++

readonly YB_DEFAULT_CMAKE_OPTS=(
  "-DCMAKE_C_COMPILER=$YB_COMPILER_WRAPPER_CC"
  "-DCMAKE_CXX_COMPILER=$YB_COMPILER_WRAPPER_CXX"
)

detect_linuxbrew
