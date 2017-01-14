#!/usr/bin/env bash

# Copyright (c) YugaByte, Inc.

# A wrapper script that pretends to be a C/C++ compiler and does some pre-processing of arguments
# and error checking on the output. Invokes GCC or Clang internally.

set -euo pipefail

# We'll sometimes generate scripts in this directory that could be re-run when debugging build
# issues.
GENERATED_BUILD_DEBUG_SCRIPT_DIR=$HOME/.yb-build-debug-scripts

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

determine_compiler_cmdline() {
  local compiler_cmdline
  if [[ -f ${stderr_path:-} ]]; then
    compiler_cmdline=$( head -1 "$stderr_path" | sed 's/^[+] //; s/\n$//' )
  else
    echo "echo 'Failed to determine compiler command line: file $stderr_path does not exist.'"
    return
  fi
  # Create a command line that can be copied and pasted.
  # As part of that, replace the ccache invocation with the actual compiler executable.
  compiler_cmdline="&& $compiler_cmdline"
  compiler_cmdline=${compiler_cmdline//&& ccache compiler/&& $compiler_executable}
  echo "cd \"$PWD\" $compiler_cmdline"
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

  local compiler_cmdline=$( determine_compiler_cmdline )
  local prefix=$1
  local suffix=${2:-}

  local command_line_filter=cat
  if [[ -n ${YB_SPLIT_LONG_COMPILER_CMD_LINES:-} ]]; then
    command_line_filter=$YB_SRC_DIR/build-support/split_long_command_line.py
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

SCRIPT_NAME="compiler-wrapper.sh"

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

should_skip_error_checking_by_input_file_pattern() {
  expect_num_args 1 "$@"
  local pattern=$1
  if [[ ${#input_files[@]} -eq 0 ]]; then
    return 1  # false
  fi
  local input_file
  for input_file in "${input_files[@]}"; do
    local input_file_abs_path=$input_file
    if [[ $input_file_abs_path =~ / ]]; then
      input_file_abs_path=$( cd "${input_file_abs_path%/*}" && pwd )/${input_file_abs_path##*/}
    else
      input_file_abs_path=$PWD/$input_file_abs_path
    fi
    if [[ ! $input_file_abs_path =~ $pattern ]]; then
      return 1  # false
    fi
  done
  # All files we looked at match the given file name pattern, return "true", meaning we'll skip
  # error checking.
  return 0
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
set +u
# The same as one string. We allow undefined variables for this line because an empty array is
# treated as such.
compiler_args_str="${compiler_args[*]}"
set -u

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
if which ccache >/dev/null && [[ -z ${YB_NO_CCACHE:-} ]]; then
  using_ccache=true
  export CCACHE_CC="$compiler_executable"
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
  fi
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
      if [[ ${#input_files[@]} -gt 0 ]]; then
        echo "Input files:" >&2
        for input_file in "${input_files[@]}"; do
          echo "  $input_file" >&2
        done
      fi
      echo "Output file (from -o): $output_file" >&2
      log_empty_line
    ) >&2
  fi
  rm -f "${stderr_path:-}"
  exit "$exit_code"
}

set_build_env_vars

if [[ -n ${YB_IS_THIRDPARTY_BUILD:-} ]]; then
  # This is the third-party build. Don't do any extra error checking/reporting, just pass through to
  # the compiler (which is potentially fronted with ccache).
  "${cmd[@]}"
  exit
fi

trap exit_handler EXIT

set +e

( set -x; "${cmd[@]}" ) 2>"$stderr_path"
compiler_exit_code=$?

set -e

# Skip printing some command lines commonly used by CMake for detecting compiler/linker version.
# Extra output might break the version detection.
if [[ -n ${YB_SHOW_COMPILER_COMMAND_LINE:-} &&
      "$compiler_args_str" != "-v" &&
      "$compiler_args_str" != "-Wl,--version" &&
      "$compiler_args_str" != "-fuse-ld=gold -Wl,--version" ]]; then
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
   grep " has been modified since the precompiled header " "$stderr_path" >/dev/null || \
   grep "PCH file built from a different branch " "$stderr_path" >/dev/null
then
  PCH_PATH=$PWD/$PCH_NAME
  echo -e "${RED_COLOR}Removing '$PCH_PATH' so that further builds have a chance to" \
          "succeed.${NO_COLOR}"
  ( rm -f "$PCH_PATH" )
fi

if [[ $compiler_exit_code -ne 0 ]]; then
  if egrep 'error: linker command failed with exit code [0-9]+ \(use -v to see invocation\)' \
       "$stderr_path" || \
     egrep 'error: ld returned' "$stderr_path"; then
    generate_build_debug_script rerun_failed_link_step "$( determine_compiler_cmdline ) -v"
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
    "will be initialized after .*Wreorder"
do
  treat_warning_pattern_as_error "$pattern"
done
