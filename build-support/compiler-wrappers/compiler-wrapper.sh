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

# A wrapper script that pretends to be a C/C++ compiler and does some pre-processing of arguments
# and error checking on the output. Invokes GCC or Clang internally.  This script is invoked through
# symlinks called "cc" or "c++".

# To run shellcheck: shellcheck -x build-support/compiler-wrappers/compiler-wrapper.sh

# shellcheck source=build-support/common-build-env.sh
. "${BASH_SOURCE[0]%/*}/../common-build-env.sh"

if [[ ${YB_COMMON_BUILD_ENV_SOURCED:-} != "1" ]]; then
  echo >&2 "Failed to source common-build-env.sh"
  exit 1
fi

set -euo pipefail

# -------------------------------------------------------------------------------------------------
# Constants

# We'll generate scripts in this directory on request. Those scripts could be re-run when debugging
# build issues.
readonly GENERATED_BUILD_DEBUG_SCRIPT_DIR=$HOME/.yb-build-debug-scripts
readonly SCRIPT_NAME="compiler-wrapper.sh"

# Note: it is very important that every line in the following pattern ends with a backslash, except
# the last line that only contains a closing double quote. Otherwise, unexpected grep behavior is
# possible, such as matching arbitrary input.
readonly COMPILATION_FAILURE_STDERR_PATTERNS="\
: Stale file handle\
|file not recognized: file truncated\
|/usr/bin/env: bash: Input/output error\
|: No such file or directory\
|[.]Po[:][0-9]+:.*missing separator[.] *Stop[.]\
|Compiler does not exist or is not executable at the path\
|EOFError: EOF read where object expected\
|ValueError: bad marshal data\
"

declare -i -r MAX_INPUT_FILES_TO_SHOW=20

# User is allowed to set YB_REMOTE_COMPILATION_MAX_ATTEMPTS.
YB_REMOTE_COMPILATION_MAX_ATTEMPTS=${YB_REMOTE_COMPILATION_MAX_ATTEMPTS:-10}
if [[ ! $YB_REMOTE_COMPILATION_MAX_ATTEMPTS =~ ^[0-9]+$ ||
      $YB_REMOTE_COMPILATION_MAX_ATTEMPTS -le 0 ]]; then
  fatal "Invalid value of YB_REMOTE_COMPILATION_MAX_ATTEMPTS (must be an integer greater than 0):" \
        "$YB_REMOTE_COMPILATION_MAX_ATTEMPTS"
fi
declare -i -r YB_REMOTE_COMPILATION_MAX_ATTEMPTS=$YB_REMOTE_COMPILATION_MAX_ATTEMPTS

compilation_step_name="COMPILATION"
delete_stderr_file=true
delete_output_file_on_failure=false
stderr_path=""

# Files such as numeric.c, int.c, int8.c, and float.c get compiled with UBSAN (Undefined Behavior
# Sanitizer) turned off due to large number of overflow cases in them (by design).

# numeric.c is compiled without UBSAN as a fix for a problem observed with Clang 5.0 and 6.0:
# undefined reference to `__muloti4'
# (full log at http://bit.ly/2lYdYnp).
# Related to the problem reported at http://bit.ly/2NvS6MR.

# int.c and int8.c have a lot of assumptions regarding the exact resulting values of overflowing
# operations, and these assumptions are not true in UBSAN mode.

# float.c compiled in UBSAN mode causes the following error on the yb_float4 test (server process
# crashes): http://bit.ly/2AW9oye

readonly NO_UBSAN_RE='(numeric|int|int8|float)'

# -------------------------------------------------------------------------------------------------
# Common functions

fatal_error() {
  echo -e "${RED_COLOR}[FATAL] $SCRIPT_NAME: $*$NO_COLOR"
  exit 1
}

# The return value is assigned to the determine_compiler_cmdline_rv variable, which should be made
# local by the caller.
determine_compiler_cmdline() {
  local compiler_cmdline
  determine_compiler_cmdline_rv=""
  if [[ -f $stderr_path ]]; then
    compiler_cmdline=$( head -1 "$stderr_path" | sed 's/^[+] //; s/\n$//' )
  else
    log "Failed to determine compiler command line: file '$stderr_path' does not exist."
    return 1
  fi
  # Create a command line that can be copied and pasted.
  # As part of that, replace the ccache invocation with the actual compiler executable.
  compiler_cmdline="YB_THIRDPARTY_DIR=$YB_THIRDPARTY_DIR $compiler_cmdline"
  if using_linuxbrew; then
    compiler_cmdline="PATH=$YB_LINUXBREW_DIR/bin:\$PATH $compiler_cmdline"
  fi
  compiler_cmdline="&& $compiler_cmdline"
  compiler_cmdline=${compiler_cmdline// ccache compiler/ $compiler_executable}
  determine_compiler_cmdline_rv="cd \"$PWD\" $compiler_cmdline"
}

show_compiler_command_line() {
  if [[ $# -lt 1 || $# -gt 2 ]]; then
    fatal "${FUNCNAME[0]} only takes one or two arguments (message prefix / suffix), got $#: $*"
  fi

  # These command lines appear during compiler/linker and Boost version detection and we don't want
  # to do output any additional information in these cases.
  if is_configure_mode_invocation; then
    return 0
  fi

  local determine_compiler_cmdline_rv
  determine_compiler_cmdline
  local compiler_cmdline=$determine_compiler_cmdline_rv

  local prefix=$1
  local suffix=${2:-}

  local command_line_filter=cat
  if [[ -n ${YB_SPLIT_LONG_COMPILER_CMD_LINES:-} ]]; then
    command_line_filter=$YB_SCRIPT_PATH_SPLIT_LONG_COMMAND_LINE
  fi

  # Split the failed compilation command over multiple lines for easier reading.
  echo -e "$prefix( $compiler_cmdline )$suffix$NO_COLOR" | \
    $command_line_filter >&2
  set -e
}

# This can be used to generate scripts that make it easy to re-run failed build commands.
generate_build_debug_script() {
  local script_name_prefix=$1
  shift
  if [[ -n ${YB_GENERATE_BUILD_DEBUG_SCRIPTS:-} ]]; then
    mkdir -p "$GENERATED_BUILD_DEBUG_SCRIPT_DIR"
    local script_name
    script_name="$GENERATED_BUILD_DEBUG_SCRIPT_DIR/${script_name_prefix}__$(
      get_timestamp_for_filenames
    )__$$_$RANDOM$RANDOM.sh"
    (
      echo "#!/usr/bin/env bash"
      echo "export PATH='$PATH'"
      # Make the script pass-through its arguments to the command it runs.
      echo "$* \"\$@\""
    ) >"$script_name"
    chmod u+x "$script_name"
    log "Generated a build debug script at $script_name"
  fi
}

should_skip_error_checking_by_input_file_pattern() {
  expect_num_args 1 "$@"
  local pattern=$1
  if [[ ${#input_files[@]} -eq 0 ]]; then
    return 1  # false
  fi
  local input_file
  for input_file in "${input_files[@]}"; do
    if [[ $input_file =~ / ]]; then
      local input_file_dir=${input_file%/*}
      local input_file_basename=${input_file##*/}
      if [[ -d $input_file_dir ]]; then
        input_file_abs_path=$( cd "$input_file_dir" && pwd )/$input_file_basename
      else
        input_file_abs_path=$input_file
      fi
    else
      input_file_abs_path=$PWD/$input_file
    fi
    if [[ ! $input_file_abs_path =~ $pattern ]]; then
      return 1  # false
    fi
  done
  # All files we looked at match the given file name pattern, return "true", meaning we'll skip
  # error checking.
  return 0
}

# -------------------------------------------------------------------------------------------------
# Functions for remote build

flush_stderr_file_helper() {
  if ! cat "$stderr_path" >&2; then
    fatal "Failed command on host $( hostname ): cat '$stderr_path'"
  fi
}

remote_build_flush_stderr_file() {
  if [[ -f $stderr_path ]]; then
    flush_stderr_file_helper
    # No need to check $delete_stderr_file here, because it is only used for local compiler
    # invocations.
    rm -f "$stderr_path"
  fi
}

remote_build_exit_handler() {
  local exit_code=$?
  remote_build_flush_stderr_file
  exit "$exit_code"
}

is_configure_mode_invocation() {
  # These command lines appear during compiler/linker and Boost version detection and we don't want
  # to do output any additional information in these cases.
  if [[ $compiler_args_str == "-v" ||
        $compiler_args_str == "-Wl,--version" ||
        $compiler_args_str == "-fuse-ld=gold -Wl,--version" ||
        $compiler_args_str == "-dumpversion" ||
        $compiler_args_str =~ .*\ -c\ test(C|CXX)Compiler[.](c|cc|cxx)$ ]]; then
    return 0  # "true" return value in bash
  fi

  return 1  # "false" return value in bash
}

check_compiler_exit_code() {
  if [[ $compiler_exit_code -ne 0 ]]; then
    if grep -Eq 'error: linker command failed with exit code [0-9]+ \(use -v to see invocation\)' \
         "$stderr_path" || \
       grep -Eq 'error: ld returned' "$stderr_path"; then
      local determine_compiler_cmdline_rv
      determine_compiler_cmdline
      generate_build_debug_script rerun_failed_link_step "$determine_compiler_cmdline_rv -v"
    fi

    if grep -E ': undefined reference to ' "$stderr_path" >/dev/null; then
      for library_path in "${input_files[@]}"; do
        nm -gC "$library_path" | grep ParseGet
      done
    fi

    exit "$compiler_exit_code"
  fi
}

remove_duplicate_rpath_args() {
  local filtered_args=()
  local rpath_args=()
  local skip_arg
  for arg in "${compiler_args[@]}"; do
    if [[ $arg == -Wl,-rpath,* ]]; then
      skip_arg=false
      if [[ ${#rpath_args[@]} -gt 0 ]]; then
        for existing_rpath_arg in "${rpath_args[@]}"; do
          if [[ $existing_rpath_arg == "$arg" ]]; then
            if [[ ${YB_FAIL_ON_DUPLICATE_RPATH:-0} == "1" ]]; then
              fatal "Duplicate RPATH argument: $arg. Compiler arguments: ${compiler_args_str}"
            fi
            skip_arg=true
            break
          fi
        done
      fi
      if [[ $skip_arg == "false" ]]; then
        filtered_args+=( "$arg" )
        rpath_args+=( "$arg" )
      fi
    else
      filtered_args+=( "$arg" )
    fi
  done
  compiler_args=( "${filtered_args[@]}" )
}

# -------------------------------------------------------------------------------------------------
# Common setup for remote and local build.
# We parse command-line arguments in both cases.
# -------------------------------------------------------------------------------------------------

cc_or_cxx=${0##*/}

stderr_path=/tmp/yb-$cc_or_cxx.$RANDOM-$RANDOM-$RANDOM.$$.stderr

compiler_args=( "$@" )
set +u
# The same as one string. We allow undefined variables for this line because an empty array is
# treated as such. When performing checks against compiler_args_str, note that that compiler_args
# can be modified after this line, and compiler_args_str might not be an exact match compared to
# the compiler command actually being executed.
compiler_args_str="${compiler_args[*]}"
set -u

if [[ -z ${BUILD_ROOT:-} ]]; then
  handle_build_root_from_current_dir
  BUILD_ROOT=$predefined_build_root
  # Not calling set_build_root here, because we don't need additional setup that it does.
else
  predefined_build_root=$BUILD_ROOT
  handle_predefined_build_root_quietly=true
  # shellcheck disable=SC2119
  handle_predefined_build_root
fi

output_file=""
input_files=()
compiling_pch=false
yb_pch=false

num_output_files_found=0
has_yb_c_files=false

compiler_args_no_output=()

linking=false
use_lld=false
lld_linking=false

num_times_tcmalloc_linked=0

while [[ $# -gt 0 ]]; do
  is_output_arg=false
  case "$1" in
    -o)
      if [[ $# -gt 1 ]]; then
        if [[ -n $output_file ]]; then
          fatal "The -o option specified twice: '$output_file' and '${2:-}'"
        fi
        output_file=${2:-}
        (( num_output_files_found+=1 ))
        shift
      fi
      is_output_arg=true
    ;;
    *.c|*.cc|*.h|*.o|*.a|*.so|*.dylib)
      if [[ $1 == */libtcmalloc.a ]]; then
        (( num_times_tcmalloc_linked+=1 ))
      fi

      # Do not include arguments that look like compiler options into the list of input files,
      # even if they have plausible extensions.
      if [[ ! $1 =~ ^[-] ]]; then
        input_files+=( "$1" )
        if [[ $1 =~ ^(.*/|)[a-zA-Z0-9_]*(yb|YB)[a-zA-Z0-9_]*[.]c$ ]]; then
          # We will use this later to add custom compilation flags to PostgreSQL source files that
          # we contributed, e.g. for stricter error checking.
          has_yb_c_files=true
        fi
      fi
    ;;
    c++-header)
      compiling_pch=true
    ;;
    -yb-pch)
      yb_pch=true
    ;;
    -DYB_COMPILER_TYPE=*)
      compiler_type_from_cmd_line=${1#-DYB_COMPILER_TYPE=}
      if [[ -n ${YB_COMPILER_TYPE:-} ]]; then
        if [[ $YB_COMPILER_TYPE != "$compiler_type_from_cmd_line" ]]; then
          fatal "Compiler command line has '$1', but YB_COMPILER_TYPE is '${YB_COMPILER_TYPE}'"
        fi
      else
        export YB_COMPILER_TYPE=$compiler_type_from_cmd_line
      fi
    ;;
    -fuse-ld=lld)
      use_lld=true
    ;;
  esac
  if [[ ${is_output_arg} == "false" ]]; then
    compiler_args_no_output+=( "$1" )
  fi
  shift
done

if [[ $output_file == *.o ]]; then
  # Compiling.
  linking=false
else
  linking=true
fi

if [[ ${linking} == "true" &&
      ${output_file} =~ .*[.](so|dylib).* &&
      ${num_times_tcmalloc_linked} -gt 0 ]]; then
  fatal "Error linking ${output_file}." \
        "The tcmalloc static library cannot be linked into shared libraries. This can result in" \
        "subtle runtime errors, because we also link libtcmalloc.a into each executable, and we" \
        "will end up with two copies of tcmalloc loaded. Command line: ${compiler_args[*]}"
fi

if [[ $linking == "true" && $use_lld == "true" ]]; then
  lld_linking=true
fi

if [[ $YB_COMPILER_TYPE == clang* && $output_file == "jsonpath_gram.o" ]]; then
  # To avoid this error:
  # https://gist.github.com/mbautin/b943fb426bfead7388dde17ddb1b0fa7
  compiler_args+=( -Wno-error=implicit-fallthrough )
fi

remove_duplicate_rpath_args

if [[ ${num_times_tcmalloc_linked} -gt 1 ]]; then
  fatal "libtcmalloc.a static library linked multiple times (${num_times_tcmalloc_linked} times)." \
        "This could lead to subtle bugs. Command line: ${compiler_args[*]}."
fi

# -------------------------------------------------------------------------------------------------
# Remote build
# -------------------------------------------------------------------------------------------------

local_build_only=false
for arg in "$@"; do
  if [[ $arg == *CMakeTmp* || $arg == "-" ]]; then
    local_build_only=true
  fi
done

is_build_worker=false
if [[ $HOSTNAME == build-worker* ]]; then
  is_build_worker=true
fi

# If linking with LLVM's lld linker, do it locally.
# See https://github.com/yugabyte/yugabyte-db/issues/11034 for more details.
if [[ $local_build_only == "false" &&
      ${YB_REMOTE_COMPILATION:-} == "1" &&
      $is_build_worker == "false" &&
      $lld_linking == "false" ]] &&
   ! is_configure_mode_invocation; then

  trap remote_build_exit_handler EXIT

  declare -i attempt=0
  sleep_deciseconds=1  # a decisecond is one-tenth of a second
  while true; do
    (( attempt+=1 ))
    if [[ $attempt -gt $YB_REMOTE_COMPILATION_MAX_ATTEMPTS ]]; then
      fatal "Failed after $YB_REMOTE_COMPILATION_MAX_ATTEMPTS attempts: $*"
    fi

    get_build_worker_list
    build_worker_name=${build_workers[ $RANDOM % ${#build_workers[@]} ]}
    build_host="$build_worker_name$YB_BUILD_WORKER_DOMAIN"

    set +e
    run_remote_cmd "$build_host" "$0" "${compiler_args[@]}" 2>"$stderr_path"
    exit_code=$?
    set -e

    # Exit code 126: "/usr/bin/env: bash: Input/output error"
    # Exit code 127: "remote_cmd.sh: No such file or directory"
    # Exit code 141: SIGPIPE
    # Exit code 254: "write: Connection reset by peer".
    # Exit code 255: ssh: connect to host ... port ...: Connection refused
    # $YB_EXIT_CODE_NO_SUCH_FILE_OR_DIRECTORY: we return this when we fail to find files or
    #   directories that are supposed to exist. We retry to work around NFS issues.
    #
    # "file not recognized: file truncated" could happen when re-running an interrupted build
    # while the old compiler process is still running.
    if [[ $exit_code -eq 126 ||
          $exit_code -eq 127 ||
          $exit_code -eq 141 ||
          $exit_code -eq 254 ||
          $exit_code -eq 255 ||
          $exit_code -eq $YB_EXIT_CODE_NO_SUCH_FILE_OR_DIRECTORY ]] ||
        ( grep -E "$COMPILATION_FAILURE_STDERR_PATTERNS" "$stderr_path" &&
          ! grep ": syntax error " "$stderr_path" ) ||
        # Always retry for non-zero exit code and empty or whitespace-only stderr output.
        ( [[ $exit_code -ne 0 ]] && ! grep -Eq '[^[:space:]]' "$stderr_path" )
    then
      remote_build_flush_stderr_file
      # TODO: distinguish between problems that can be retried on the same host, and problems
      # indicating that the host is down.
      # TODO: maintain a blacklist of hosts that are down and don't retry on the same host from
      # that blacklist list too soon.
      if [[ -f $stderr_path && $exit_code -eq 0 ]]; then
        echo "Exit code is 0, showing error output:" >&2
        cat "$stderr_path" >&2
      fi
      log "Host $build_host is experiencing problems (exit code $exit_code), retrying on another" \
          "host (this was attempt $attempt) after a 0.$sleep_deciseconds second delay"
      sleep 0.$sleep_deciseconds
      if [[ $sleep_deciseconds -lt 9 ]]; then
        (( sleep_deciseconds+=1 ))
      fi

      continue
    fi

    remote_build_flush_stderr_file

    # We have decided not to retry the build. Break now and report the error if any.
    break
  done  # End of loop that tries to run the build on different remote hosts.

  if [[ $exit_code -ne 0 ]]; then
    log_empty_line
    # Not using the log function here, because as of 07/23/2017 it does not correctly handle
    # multi-line log messages (it concatenates them into one line).
    old_ps4=$PS4
    PS4=""
    compiler_args_escaped_str=$(
      ( set -x; echo -- "${compiler_args[@]}" >/dev/null ) 2>&1 | sed 's/^echo -- //'
    )
    echo >&2 "
---------------------------------------------------------------------------------------------------
REMOTE COMPILER INVOCATION FAILED
---------------------------------------------------------------------------------------------------
Build host: $build_host
Directory:  $PWD
PATH:       $PATH
Command:    $0 $compiler_args_escaped_str
Exit code:  $exit_code
---------------------------------------------------------------------------------------------------

"
    PS4=$old_ps4
  fi
  exit "$exit_code"
elif debugging_remote_compilation && ! $is_build_worker; then
  log "Not doing remote build: local_build_only=$local_build_only," \
    "YB_REMOTE_COMPILATION=${YB_REMOTE_COMPILATION:-undefined}," \
    "HOSTNAME=$HOSTNAME"
fi

# -------------------------------------------------------------------------------------------------
# Functions for local build
# -------------------------------------------------------------------------------------------------

local_build_exit_handler() {
  local exit_code=$?
  if [[ $exit_code -eq 0 ]]; then
    if [[ -f ${stderr_path:-} ]]; then
      tail -n +2 "$stderr_path" >&2
    fi
  else
    # We output the compiler executable path because the actual command we're running will likely
    # contain ccache instead of the compiler executable.
    (
      if ! show_compiler_command_line "\n$RED_COLOR" \
               "  # Compiler exit code: $compiler_exit_code.\n"; then
        echo >&2 "Failed to show compiler command line."
      fi
      if [[ -f ${stderr_path:-} ]]; then
        if [[ -s ${stderr_path:-} ]]; then
          (
            red_color
            echo "/-------------------------------------------------------------------------------"
            echo "| $compilation_step_name FAILED"
            echo "|-------------------------------------------------------------------------------"
            IFS=$'\n'
            (
              tail -n +2 "$stderr_path"
              echo
              if [[ ${#input_files[@]} -gt 0 ]]; then
                declare -i num_files_shown=0
                declare -i num_files_skipped=0
                echo "Input files:"
                for input_file in "${input_files[@]}"; do
                  # Only resolve paths for files that exists (and therefore are more likely to
                  # actually be files).
                  if [[ -f "/usr/bin/realpath" && -e "$input_file" ]]; then
                    input_file=$( realpath "$input_file" )
                  fi
                  (( num_files_shown+=1 ))
                  if [[ $num_files_shown -lt $MAX_INPUT_FILES_TO_SHOW ]]; then
                    echo "  $input_file"
                  else
                    (( num_files_skipped+=1 ))
                  fi
                done
                if [[ $num_files_skipped -gt 0 ]]; then
                  echo "  ($num_files_skipped files skipped)"
                fi
                echo "Output file (from -o): $output_file"
              fi

            ) | "$YB_SCRIPT_PATH_FIX_PATHS_IN_COMPILE_ERRORS"

            unset IFS
            echo "\-------------------------------------------------------------------------------"
            no_color
          ) >&2
        else
          echo "Compiler standard error is empty." >&2
        fi
      fi
    ) >&2
  fi
  if [[ -n $stderr_path ]] && ( "$delete_stderr_file" || [[ -s $stderr_path ]] ); then
    rm -f "${stderr_path:-}"
  fi
  if [[ $exit_code -eq 0 && -n $output_file ]]; then
    # Successful compilation. Update the output file timestamp locally to work around any clock
    # skew issues between the node we're running this on and compilation worker nodes.
    if [[ -e $output_file ]]; then
      touch "$output_file"
    else
      log "Warning: was supposed to create an output file '$output_file', but it does not exist."
    fi
  fi
  if [[ $exit_code -ne 0 && -f $output_file ]] && "$delete_output_file_on_failure"; then
    rm -f "$output_file"
  fi
  exit "$exit_code"
}

run_compiler_and_save_stderr() {
  set +e
  ( set -x; "$@" ) 2>"$stderr_path"
  compiler_exit_code=$?
  set -e
}

# -------------------------------------------------------------------------------------------------
# Local build
# -------------------------------------------------------------------------------------------------

if [[ $output_file == libyb_pgbackend* || $output_file == postgres ]]; then
  # We record the linker command used for the libyb_pgbackend library so we can use it when
  # producing the LTO build for yb-tserver.
  echo "${compiler_args[*]}" >"link_cmd_${output_file}.txt"
fi

if [[ ${build_type:-} == "asan" &&
      $PWD == */postgres_build/src/backend/utils/adt &&
      # Turn off UBSAN instrumentation in a number of PostgreSQL source files determined by the
      # $NO_UBSAN_RE regular expression. See the definition of NO_UBSAN_RE for details.
      $compiler_args_str =~ .*\ -c\ -o\ ${NO_UBSAN_RE}[.]o\ ${NO_UBSAN_RE}[.]c\ .* ]]; then
  rewritten_args=()
  for arg in "${compiler_args[@]}"; do
    case $arg in
      -fsanitize=undefined)
        # Skip UBSAN
      ;;
      *)
        rewritten_args+=( "$arg" )
      ;;
    esac
  done
  compiler_args=( "${rewritten_args[@]}" "-fno-sanitize=undefined" )
fi

set_default_compiler_type
find_or_download_thirdparty
find_compiler_by_type

if [[ $cc_or_cxx == "compiler-wrapper.sh" && $compiler_args_str == "--version" ]]; then
  # Allow invoking this script not through a symlink but directly in one special case: when trying
  # to determine the compiler version.
  cc_or_cxx=cc
fi

case "$cc_or_cxx" in
  cc) compiler_executable=$cc_executable ;;
  c++) compiler_executable=$cxx_executable ;;
  *)
    fatal "The $SCRIPT_NAME script should be invoked through a symlink named 'cc' or 'c++', " \
          "found: $cc_or_cxx." \
          "Just in case:" \
          "cc_executable=${cc_executable:-undefined}," \
          "cxx_executable=${cxx_executable:-undefined}."
esac

if [[ -z ${compiler_executable:-} ]]; then
  fatal "[Host $(hostname)] The compiler_executable variable is not defined." \
        "Command line: $compiler_args_str"
fi

if [[ ! -x $compiler_executable ]]; then
  log_diagnostics_about_local_thirdparty
  fatal "[Host $(hostname)] Compiler executable does not exist or is not executable:" \
        "$compiler_executable. Command line: $compiler_args_str"
fi

# We use ccache if it is available and YB_NO_CCACHE is not set.
if [[ -z ${YB_NO_CCACHE:-} && ${YB_USE_PCH:-} != "1" && "${compiling_pch}" != "true" ]] &&
   command -v ccache >/dev/null; then
  export CCACHE_CC="$compiler_executable"
  export CCACHE_SLOPPINESS="file_macro,pch_defines,time_macros"
  export CCACHE_BASEDIR=$YB_SRC_ROOT

  if [ -z "${USER:-}" ]; then
    if whoami &> /dev/null; then
      USER="$(whoami)"
    else
      fatal_error "No USER variable in env and no whoami to detect it"
    fi
  fi

  # Ensure CCACHE puts temporary files on the local disk.
  export CCACHE_TEMPDIR=${CCACHE_TEMPDIR:-/tmp/ccache_tmp_$USER}
  if [[ -n ${YB_CCACHE_DIR:-} ]]; then
    export CCACHE_DIR=$YB_CCACHE_DIR
  else
    jenkins_ccache_dir=/Volumes/n/jenkins/ccache
    if [[ $USER == "jenkins" && -d $jenkins_ccache_dir ]] && is_src_root_on_nfs; then
      # Enable reusing cache entries from builds in different directories, potentially with
      # incorrect file paths in debug information. This is OK for Jenkins because we probably won't
      # be running these builds in the debugger.
      export CCACHE_NOHASHDIR=1

      if [[ ${YB_DEBUG_CCACHE:-0} == "1" ]] && ! is_jenkins; then
        log "is_jenkins (based on JOB_NAME) is false for some reason, even though" \
            "the user is 'jenkins'. Setting CCACHE_DIR to '$jenkins_ccache_dir' anyway." \
            "This is host $HOSTNAME, and current directory is $PWD."
      fi
      export CCACHE_DIR=$jenkins_ccache_dir
    fi
  fi
  if [[ ${CCACHE_DIR:-} =~ $YB_NFS_PATH_RE ]]; then
    # Do not update the stats file, because that involves locking and might be problematic/slow
    # on NFS.
    export CCACHE_NOSTATS=1
  fi
  cmd=( ccache compiler )
else
  cmd=( "$compiler_executable" )
  if [[ -n ${YB_EXPLAIN_WHY_NOT_USING_CCACHE:-} ]]; then
    if ! command -v ccache >/dev/null; then
      log "Could not find ccache in PATH ( $PATH )"
    fi
    if [[ -n ${YB_NO_CCACHE:-} ]]; then
      log "YB_NO_CCACHE is set"
    fi
    if [[ ${compiling_pch} == "true" ]]; then
      log "Not using ccache for precompiled headers."
    fi
  fi
fi

if [[ ${#compiler_args[@]} -gt 0 ]]; then
  cmd+=( "${compiler_args[@]}" )
fi

if [[ $has_yb_c_files == "true" && $PWD == $BUILD_ROOT/postgres_build/* ]]; then
  # Custom build flags for YB files inside of the PostgreSQL source tree. This re-enables some flags
  # that we had to disable by default in build_postgres.py.
  cmd+=( "-Werror=unused-function" )
fi
add_brew_bin_to_path

if [[ $PWD == $BUILD_ROOT/postgres_build ||
      $PWD == $BUILD_ROOT/postgres_build/* ]]; then
  new_cmd=()
  for arg in "${cmd[@]}"; do
    if [[ $arg == -I* ]]; then
      include_dir=${arg#-I}
      if [[ -d $include_dir ]]; then
        include_dir=$( cd "$include_dir" && pwd )
        if [[ $include_dir == $BUILD_ROOT/postgres_build/* ]]; then
          rel_include_dir=${include_dir#"${BUILD_ROOT}"/postgres_build/}
          updated_include_dir=$YB_SRC_ROOT/src/postgres/$rel_include_dir
          if [[ -d $updated_include_dir ]]; then
            new_cmd+=( -I"$updated_include_dir" )
          fi
        fi
      fi
      new_cmd+=( "$arg" )
    elif [[ -f $arg && $arg != "conftest.c" ]]; then
      file_path=$PWD/${arg#./}
      rel_file_path=${file_path#"${BUILD_ROOT}"/postgres_build/}
      updated_file_path=$YB_SRC_ROOT/src/postgres/$rel_file_path
      if [[ -f $updated_file_path ]] && cmp --quiet "$file_path" "$updated_file_path"; then
        new_cmd+=( "$updated_file_path" )
      else
        new_cmd+=( "$arg" )
      fi
    else
      new_cmd+=( "$arg" )
    fi
  done
  cmd=( "${new_cmd[@]}" )
fi

compiler_exit_code=UNKNOWN
trap local_build_exit_handler EXIT

if [[ ${YB_GENERATE_COMPILATION_CMD_FILES:-0} == "1" &&
      -n $output_file &&
      ($output_file == *.o || $output_file == *.pch) ]]; then
  IFS=$'\n'
  echo "
directory: $PWD
output_file: $output_file
compiler: $compiler_executable
arguments:
${cmd[*]}
">"${output_file%.o}.cmd.txt"
fi
  unset IFS

# When -yb-pch is specified, we use it to generate precompiled header.
if [[ "${yb_pch}" == "true" ]]; then
  if [[ -n $output_file ]]; then
    pch_cmd=( "$compiler_executable" )
    skip_next=false
    pch_file="${output_file%.cc.o}.h.pch"
    # Replace original args with args required for compilation.
    for arg in "${compiler_args[@]}"; do
      if [[ ${skip_next} == "true" ]]; then
        skip_next=false
        continue
      fi
      case "$arg" in
        -yb-pch)
          pch_cmd+=( "-Xclang" "-emit-pch" "-fpch-instantiate-templates" "-fpch-codegen"
                     "-fpch-debuginfo" "-x" "c++-header" "-c" )
        ;;
        -fpch-*|-x|c++-header)
        ;;
        -MT)
          pch_cmd+=( "$arg" "$pch_file" )
          skip_next=true
        ;;
        -o)
          pch_cmd+=( "$arg" "$pch_file" )
          skip_next=true
        ;;
        -c)
          skip_next=true
        ;;
        *)
          pch_cmd+=("$arg")
        ;;
      esac
    done
    run_compiler_and_save_stderr "${pch_cmd[@]}"
    check_compiler_exit_code

    codegen_cmd=( "$compiler_executable" )
    for arg in "${compiler_args[@]}"; do
      if [[ ${skip_next} == "true" ]]; then
        skip_next=false
        continue
      fi
      case $arg in
        -yb-pch)
          skip_next=true
        ;;
        -c)
          codegen_cmd+=( "$arg" "$pch_file" )
          skip_next=true
        ;;
        *)
          codegen_cmd+=( "$arg" )
        ;;
      esac
    done
    run_compiler_and_save_stderr "${codegen_cmd[@]}"
  else
    new_cmd=( "$compiler_executable" )
    skip_next=false
    for arg in "${compiler_args[@]}"; do
      if [[ ${skip_next} == "true" ]]; then
        skip_next=false
        continue
      fi
      case $arg in
        -yb-pch)
          skip_next=true
        ;;
        *)
          new_cmd+=( "$arg" )
        ;;
      esac
    done
    run_compiler_and_save_stderr "${new_cmd[@]}"
  fi
else
  if [[ -n $output_file ]]; then
    run_compiler_and_save_stderr "${cmd[@]}"
  else
    # Have to patch compiler command line to make CLion happy while loading CMake project.
    new_cmd=( "$compiler_executable" )
    skip_next=false
    for arg in "${compiler_args[@]}"; do
      if [[ ${skip_next} == "true" ]]; then
        if [[ "$arg" == "-Xclang" ]]; then
          continue
        fi

        skip_next=false
        if [[ "$arg" == -* ]]; then
          new_cmd+=( "$arg" )
        else
          new_cmd+=( "-include" "-Xclang" "$arg" )
        fi
        continue
      fi
      case $arg in
        -include)
          skip_next=true
        ;;
        *)
          new_cmd+=( "$arg" )
        ;;
      esac
    done
    run_compiler_and_save_stderr "${new_cmd[@]}"
  fi
fi

# Skip printing some command lines commonly used by CMake for detecting compiler/linker version.
# Extra output might break the version detection.
if [[ -n ${YB_SHOW_COMPILER_COMMAND_LINE:-} ]] &&
   ! is_configure_mode_invocation; then
  show_compiler_command_line "$CYAN_COLOR"
fi

check_compiler_exit_code

if grep -Eq 'ld: warning: directory not found for option' "$stderr_path"; then
  log "Linker failed to find a directory (probably a library directory) that should exist."
  exit 1
fi
