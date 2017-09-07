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

. "${0%/*}/../common-build-env.sh"

set -euo pipefail

# -------------------------------------------------------------------------------------------------
# Constants

# We'll generate scripts in this directory on request. Those scripts could be re-run when debugging
# build issues.
readonly GENERATED_BUILD_DEBUG_SCRIPT_DIR=$HOME/.yb-build-debug-scripts
readonly SCRIPT_NAME="compiler-wrapper.sh"

# -------------------------------------------------------------------------------------------------
# Common functions

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

# The return value is assigned to the determine_compiler_cmdline_rv variable, which should be made
# local by the caller.
determine_compiler_cmdline() {
  local compiler_cmdline
  determine_compiler_cmdline_rv=""
  if [[ -f ${stderr_path:-} ]]; then
    compiler_cmdline=$( head -1 "$stderr_path" | sed 's/^[+] //; s/\n$//' )
  else
    log "Failed to determine compiler command line: file '$stderr_path' does not exist."
    return 1
  fi
  # Create a command line that can be copied and pasted.
  # As part of that, replace the ccache invocation with the actual compiler executable.
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
  if [[ $compiler_args_str == "-v" ||
        $compiler_args_str == "-Wl,--version" ||
        $compiler_args_str == "-fuse-ld=gold -Wl,--version" ||
        $compiler_args_str == "-dumpversion" ]]; then
    return
  fi

  local determine_compiler_cmdline_rv
  determine_compiler_cmdline
  local compiler_cmdline=$determine_compiler_cmdline_rv

  local prefix=$1
  local suffix=${2:-}

  local command_line_filter=cat
  if [[ -n ${YB_SPLIT_LONG_COMPILER_CMD_LINES:-} ]]; then
    command_line_filter=$YB_SRC_ROOT/build-support/split_long_command_line.py
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
    local script_name="$GENERATED_BUILD_DEBUG_SCRIPT_DIR/${script_name_prefix}__$(
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

flush_stderr_file() {
  if [[ -f ${stderr_path:-} ]]; then
    cat "$stderr_path" >&2
    rm -f "$stderr_path"
  fi
}

remote_build_exit_handler() {
  local exit_code=$?
  flush_stderr_file
  exit "$exit_code"
}

# -------------------------------------------------------------------------------------------------
# Common setup for remote and local build

cc_or_cxx=${0##*/}

stderr_path=/tmp/yb-$cc_or_cxx.$RANDOM-$RANDOM-$RANDOM.$$.stderr

# -------------------------------------------------------------------------------------------------
# Remote build

cmd_line_str="$*"

output_file=""
nonexistent_file_args=()
local_build_only=false
for arg in "$@"; do
  if [[ $arg =~ *CMakeTmp* || $arg == "-" ]]; then
    local_build_only=true
  fi
done

is_build_worker=false
if [[ $HOSTNAME == build-worker* ]]; then
  is_build_worker=true
fi

if [[ $local_build_only == "false" &&
      ${YB_REMOTE_BUILD:-} == "1" &&
      -z ${YB_NO_REMOTE_BUILD:-} &&
      $is_build_worker == "false" ]]; then

  trap remote_build_exit_handler EXIT

  current_dir=$PWD
  declare -i attempt=1
  declare -i no_worker_count=0
  sleep_deciseconds=1  # a decisecond is one-tenth of a second
  while [[ $attempt -lt 100 ]]; do
    build_worker_name=$( shuf -n 1 "$YB_BUILD_WORKERS_FILE" )
    if [[ -z $build_worker_name ]]; then
      let no_worker_count+=1
      if [[ $no_worker_count -ge 100 ]]; then
        fatal "Found no live workers in "$YB_BUILD_WORKERS_FILE" in $no_worker_count attempts"
      fi
      log "Waiting for one second while no live workers are present in $YB_BUILD_WORKERS_FILE"
      sleep 1
      continue
    fi
    build_host=$build_worker_name.c.yugabyte.internal
    set +e
    run_remote_cmd "$build_host" "$0" "$@" 2>"$stderr_path"
    exit_code=$?
    set -e
    # Saw an exit code 254 after this error: "write: Connection reset by peer".
    if [[ $exit_code -eq 255 || $exit_code -eq 254 ]] || \
        egrep "\
ccache: error: Failed to open .*: No such file or directory|\
Fatal error: can't create .*: Stale file handle\
" "$stderr_path" >&2; then
      flush_stderr_file
      # TODO: distinguish between problems that can be retried on the same host, and problems
      # indicating that the host is down.
      # TODO: maintain a blacklist of hosts that are down and don't retry on the same host from
      # that blacklist list too soon.
      log "Host $build_host is experiencing problems, retrying on a different host" \
          "(this was attempt $attempt) after a 0.$sleep_deciseconds second delay"
      sleep 0.$sleep_deciseconds
      if [[ $sleep_deciseconds -lt 9 ]]; then
        let sleep_deciseconds+=1
      fi
      let attempt+=1
      continue
    fi
    flush_stderr_file
    break
  done
  if [[ $exit_code -ne 0 ]]; then
    log_empty_line
    # Not using the log function here, because as of 07/23/2017 it does not correctly handle
    # multi-line log messages (it concatenates them into one line).
    echo >&2 "
---------------------------------------------------------------------------------------------------
REMOTE COMPILER INVOCATION FAILED
---------------------------------------------------------------------------------------------------
Build host: $build_host
Directory:  $PWD
PATH:       $PATH
Command:    $0 $*
Exit code:  $exit_code
---------------------------------------------------------------------------------------------------

"
  fi
  exit $exit_code
elif [[ ${YB_DEBUG_REMOTE_BUILD:-} == "1" ]] && ! $is_build_worker; then
  log "Not doing remote build: local_build_only=$local_build_only," \
    "YB_REMOTE_BUILD=$YB_REMOTE_BUILD," \
    "HOSTNAME=$HOSTNAME"
fi

# -------------------------------------------------------------------------------------------------
# Functions for local build

local_build_exit_handler() {
  local exit_code=$?
  if [[ $exit_code -eq 0 ]]; then
    if [[ -f ${stderr_path:-} ]]; then
      tail -n +2 "$stderr_path" >&2
    fi
  elif [[ -n ${YB_IS_THIRDPARTY_BUILD:-} ]]; then
    # Do not add any fancy output if we're running as part of the third-party build.
    if [[ -f ${stderr_path:-} ]]; then
      cat "$stderr_path" >&2
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
            (
              tail -n +2 "$stderr_path"
              echo
              if [[ ${#input_files[@]} -gt 0 ]]; then
                echo "Input files:"
                for input_file in "${input_files[@]}"; do
                  # Only resolve paths for files that exists (and therefore are more likely to
                  # actually be files).
                  if [[ -f "/usr/bin/realpath" && -e "$input_file" ]]; then
                    input_file=$( realpath "$input_file" )
                  fi
                  echo "  $input_file"
                done
                echo
                echo "Output file (from -o): $output_file"
              fi

            ) | "$YB_SRC_ROOT/build-support/fix_paths_in_compile_errors.py"

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
  rm -f "${stderr_path:-}"
  exit "$exit_code"
}

# -------------------------------------------------------------------------------------------------
# Local build

compiler_args=( "$@" )

YB_SRC="$YB_SRC_ROOT/src"

set +u
# The same as one string. We allow undefined variables for this line because an empty array is
# treated as such.
compiler_args_str="${compiler_args[*]}"
set -u

output_file=""
input_files=()
library_files=()
compiling_pch=false

# Determine if we're building the precompiled header (not whether we're using one).
is_precompiled_header=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    -o)
      output_file=${2:-}
      if [[ $# -gt 1 ]]; then
        shift
      fi
    ;;
    *.cc|*.h|*.o|*.a|*.so|*.dylib)
      # Do not include arguments that look like compiler options into the list of input files,
      # even if they have plausible extensions.
      if [[ ! $1 =~ ^[-] ]]; then
        input_files+=( "$1" )
      fi
    ;;
    c++-header)
      compiling_pch=true
    ;;
    *)
    ;;
  esac
  shift
done

if [[ ! $output_file = *.o && ${#library_files[@]} -gt 0 ]]; then
  input_files+=( "${library_files[@]}" )
  library_files=()
fi

set_default_compiler_type
find_compiler_by_type "$YB_COMPILER_TYPE"

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
if which ccache >/dev/null && ! "$compiling_pch" && [[ -z ${YB_NO_CCACHE:-} ]]; then
  using_ccache=true
  export CCACHE_CC="$compiler_executable"
  export CCACHE_SLOPPINESS="pch_defines,time_macros"
  export CCACHE_BASEDIR=$YB_SRC_ROOT
  jenkins_ccache_dir=/n/jenkins/ccache
  if [[ $USER == "jenkins" && -d $jenkins_ccache_dir ]] && is_src_root_on_nfs && is_running_on_gcp
  then
    if ! is_jenkins; then
      log "is_jenkins (based on BUILD_ID and JOB_NAME) is false for some reason, even though" \
          "the user is 'jenkins'. Setting CCACHE_DIR to '$jenkins_ccache_dir' anyway." \
          "This is host $HOSTNAME, and current directory is $PWD."
    fi
    export CCACHE_DIR=$jenkins_ccache_dir
  fi
  cmd=( ccache compiler )
else
  cmd=( "$compiler_executable" )
  if [[ -n ${YB_EXPLAIN_WHY_NOT_USING_CCACHE:-} ]]; then
    if ! which ccache >/dev/null; then
      log "Could not find ccache in PATH ( $PATH )"
    fi
    if [[ -n ${YB_NO_CCACHE:-} ]]; then
      log "YB_NO_CCACHE is set"
    fi
    if "$compiling_pch"; then
      log "Not using ccache for precompiled headers."
    fi
  fi
fi

if [[ ${#compiler_args[@]} -gt 0 ]]; then
  cmd+=( "${compiler_args[@]}" )
fi

set_build_env_vars

compiler_exit_code=UNKNOWN
trap local_build_exit_handler EXIT

set +e

( set -x; "${cmd[@]}" ) 2>"$stderr_path"
compiler_exit_code=$?

set -e

# Skip printing some command lines commonly used by CMake for detecting compiler/linker version.
# Extra output might break the version detection.
if [[ -n ${YB_SHOW_COMPILER_COMMAND_LINE:-} &&
      $compiler_args_str != "-v" &&
      $compiler_args_str != "-Wl,--version" &&
      $compiler_args_str != "-fuse-ld=gold -Wl,--version" &&
      $compiler_args_str != "-print-prog-name=ld" ]]; then
  show_compiler_command_line "$CYAN_COLOR"
fi

if [[ -n ${YB_IS_THIRDPARTY_BUILD:-} ]]; then
  # This is the third-party build. Don't do any extra error checking/reporting, just pass the
  # compiler output back to the caller. The compiler's standard error will be passed to the calling
  # process by the exit handler.
  exit "$compiler_exit_code"
fi

# Deal with failures when trying to use precompiled headers. Our current approach is to delete the
# precompiled header.
if grep ".h.gch: created by a different GCC executable" "$stderr_path" >/dev/null || \
   grep ".h.gch: not used because " "$stderr_path" >/dev/null || \
   grep "fatal error: malformed or corrupted AST file:" "$stderr_path" >/dev/null || \
   grep "new operators was enabled in PCH file but is currently disabled" "$stderr_path" \
     >/dev/null || \
   egrep "definition of macro '.*' differs between the precompiled header .* and the command line" \
         "$stderr_path" >/dev/null || \
   grep " has been modified since the precompiled header " "$stderr_path" >/dev/null || \
   grep "PCH file built from a different branch " "$stderr_path" >/dev/null
then
  # Extract path to generated file from error message.
  # Example: "note: please rebuild precompiled header 'PCH_PATH'"
  PCH_PATH=$( grep rebuild "$stderr_path" | awk '{ print $NF }' ) || echo -n
  if [[ -z $PCH_PATH ]]; then
    echo -e "${RED_COLOR}Failed to obtain PCH_PATH from $stderr_path${NO_COLOR}"
    # Fallback to a specific location of the precompiled header file (used in the RocksDB codebase).
    # If we add more precompiled header files, we will need to change related error handling here.
    PCH_NAME=rocksdb_precompiled/precompiled_header.h.gch
    PCH_PATH=$PWD/$PCH_NAME
  else
    # Strip quotes
    PCH_PATH=${PCH_PATH:1:-1}
  fi
  # Dump stats for debugging
  stat -x "$PCH_PATH"

  # Extract source for precompiled header.
  # Example: "fatal error: file 'SOURCE_FILE' has been modified since the precompiled header
  # 'PCH_PATH' was built"
  SOURCE_FILE=$( grep "fatal error" "$stderr_path" | awk '{ print $4 }' )
  if [[ -n ${SOURCE_FILE} ]]; then
    SOURCE_FILE=${SOURCE_FILE:1:-1}
    # Dump stats for debugging
    stat -x ${SOURCE_FILE}
  fi

  echo -e "${RED_COLOR}Removing '$PCH_PATH' so that further builds have a chance to" \
          "succeed.${NO_COLOR}"
  ( rm -rf "$PCH_PATH" )
fi

if [[ $compiler_exit_code -ne 0 ]]; then
  if egrep 'error: linker command failed with exit code [0-9]+ \(use -v to see invocation\)' \
       "$stderr_path" >/dev/null || \
     egrep 'error: ld returned' "$stderr_path" >/dev/null; then
    determine_compiler_cmdline
    generate_build_debug_script rerun_failed_link_step "$determine_compiler_cmdline_rv -v"
  fi

  if egrep ': undefined reference to ' "$stderr_path" >/dev/null; then
    for library_path in "${input_files[@]}"; do
      nm -gC "$library_path" | grep ParseGet
    done
  fi

  exit "$compiler_exit_code"
fi

# Selectively treat some warnings as errors. This is not very easily done using compiler options,
# because even though there is a -Wno-error that prevents a warning from being an error even if
# -Werror is in effect, the opposite does not seem to exist.

# We don't treat unannotated fall-through in switch statement as an error for files generated by
# Flex.
if ! should_skip_error_checking_by_input_file_pattern '[.]l[.]cc$'; then
  for pattern in "warning: unannotated fall-through between switch labels" \
                 "warning: fallthrough annotation does not directly precede switch label"; do
    treat_warning_pattern_as_error "$pattern"
  done
fi

# Do not catch any additional errors in certain Kudu-specific files that will go away anyway.
# As we remove these Kudu files, we should remove these patterns as well.
# We are also ignoring some of these errors in a couple of tests that currently have a lot of
# ignored Status return values (tablet_server-test, tablet-test). We should clean those up pretty
# soon.
#
# Note: forward slash simply means forward slash in the file name regex, it does not indicate regex
# beginning/end.
if ! should_skip_error_checking_by_input_file_pattern \
   "/(diskrowset|\
cfile_set-test|\
compaction|\
compaction-test|\
deltamemstore|\
deltamemstore-test|\
diskrowset|\
diskrowset-test|\
major_delta_compaction-test|\
mt-diskrowset-test|\
mt-rowset_delta_compaction-test|\
remote_bootstrap_kudu_client-test|\
remote_bootstrap_kudu_session-test|\
rowset_info|\
tablet/(delta_)?compaction|\
tablet_server-test|\
tablet-test\
)[.]cc\$"
then
  treat_warning_pattern_as_error \
    "warning: ignoring return value of function declared with '?warn_unused_result'?"
fi

for pattern in \
    "no return statement in function returning non-void" \
    "control may reach end of non-void function" \
    "control reaches end of non-void function" \
    "warning: reference to local variable .* returned" \
    "warning: enumeration value .* not handled in switch" \
    "warning: comparison between .* and .* .*-Wenum-compare" \
    "will be initialized after .*Wreorder" \
    "has virtual functions and accessible non-virtual destructor .*Wnon-virtual-dtor"
do
  treat_warning_pattern_as_error "$pattern"
done
