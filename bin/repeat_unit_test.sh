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

show_usage() {
  cat <<-EOT
Usage: ${0##*/} <build_type> <test_binary_name> [<test_filter>] [<options>]
Runs the given test in a loop locally, collects statistics about successes/failures, and saves
logs.

Options:
  -h, --help
    Show usage.
  -p, --parallelism
    Run this many instances of the test in parallel.
  -n, --num-iter
    Run this many iterations of the test.
  -v <verbosity>
    Verbosity option passed to the test.
  --verbose
    Output extra debug information from this script. Does not affect the test.
  -k, --keep-all-logs
    Keep all logs, not just failing tests' logs.
  --skip-address-already-in-use
    Skip "address already in use" errors that frequently cause false positive test failures.
    The current approach is somewhat simplistic in that we attribute the test failure to the
    "address already in use" issue, even if other issues may be present, but that's OK as long as
    we believe "address already in use" only happens a small percentage of the time.
  --test-args "<arguments>"
    Pass additional arguments to the test.
  --clang
    Use binaries built with Clang (e.g. to distinguish between debug/release builds made with
    gcc vs. clang). If YB_COMPILER_TYPE is set, it will take precedence, and this option will not
    be allowed.
  --skip-log-compression
    Don't compress kept logs.
EOT
}

delete_tmp_files() {
  # Be extra careful before we nuke a directory.
  if [[ -n ${TEST_TMPDIR:-} && $TEST_TMPDIR =~ ^/tmp/ ]]; then
    rm -rf "$TEST_TMPDIR"
  fi
}

store_log_file_and_report_result() {
  if "$skip_log_compression"; then
    test_log_stored_path="$test_log_path"
  else
    gzip "$test_log_path"
    test_log_stored_path="$test_log_path.gz"
  fi
  echo "$1: iteration $iteration (log: $test_log_stored_path)"
}

set -euo pipefail

. "${0%/*}"/../build-support/common-test-env.sh

unset TEST_TMPDIR

trap delete_tmp_files EXIT

script_name=${0##*/}
script_name_no_ext=${script_name%.sh}
skip_address_already_in_use=false

if [[ $# -eq 0 ]]; then
  show_usage >&2
  exit 1
fi
positional_args=()
more_test_args=""
log_dir=""
declare -i parallelism=4
declare -i iteration=0
declare -i num_iter=1000
keep_all_logs=false
skip_log_compression=false
original_args=( "$@" )
yb_compiler_type_arg=""
verbose=false

while [[ $# -gt 0 ]]; do
  if [[ ${#positional_args[@]} -eq 0 ]] && is_valid_build_type "$1"; then
    build_type=$1
    shift
    continue
  fi
  case "$1" in
    -h|--help)
      show_usage >&2
      exit 1
    ;;
    -v)
      more_test_args+=" -v=$2"
      shift
    ;;
    --verbose)
      verbose=true
    ;;
    -p|--parallelism)
      parallelism=$2
      shift
    ;;
    --log_dir)
      # Used internally for parallel execution
      log_dir="$2"
      if [[ ! -d $log_dir ]]; then
        echo "Specified log directory '$log_dir' does not exist" >&2
        exit 1
      fi
      shift
    ;;
    -n|--num-iter)
      num_iter=$2
      shift
    ;;
    --iteration)
      # Used internally for parallel execution
      iteration=$2
      shift
    ;;
    -k|--keep-all-logs)
      keep_all_logs=true
    ;;
    --skip-log-compression)
      skip_log_compression=true
    ;;
    --skip-address-already-in-use)
      skip_address_already_in_use=true
    ;;
    --test-args)
      more_test_args+=" $2"
      shift
    ;;
    --clang)
      if [[ -n ${YB_COMPILER_TYPE:-} && ${YB_COMPILER_TYPE:-} != "clang" ]]; then
        fatal "YB_COMPILER_TYPE is set to '$YB_COMPILER_TYPE'," \
              "but --clang is specified on the command line"
      fi
      yb_compiler_type_arg="clang"
    ;;
    *)
      positional_args+=( "$1" )
    ;;
  esac
  shift
done

yb_compiler_type_from_env=${YB_COMPILER_TYPE:-}
if [[ -n $yb_compiler_type_arg ]]; then
  YB_COMPILER_TYPE=$yb_compiler_type_arg
fi

set_cmake_build_type_and_compiler_type
set_build_root
set_asan_tsan_options

declare -i -r num_pos_args=${#positional_args[@]}
if [[ $num_pos_args -lt 1 || $num_pos_args -gt 2 ]]; then
  show_usage >&2
  fatal "Expected two to three positional arguments:" \
        "<build_type> <test_binary_name> [<test_filter>]"
fi

test_binary_name=${positional_args[0]}

gtest_filter_arg=""
if [[ $num_pos_args -eq 2 ]]; then
  test_filter=${positional_args[1]}
  gtest_filter_info="gtest_filter is $test_filter (from the command line)"
elif [[ -n ${YB_GTEST_FILTER:-} ]]; then
  test_filter=$YB_GTEST_FILTER
  gtest_filter_info="gtest_filter is $test_filter (from the YB_GTEST_FILTER env var)"
else
  test_filter="all_tests"
  gtest_filter_info="gtest_filter is not set, running all tests in the test program"
fi

if [[ $test_filter != "all_tests" ]]; then
  gtest_filter_arg="--gtest_filter=$test_filter"
fi

abs_test_binary_path=$( find_test_binary "$test_binary_name" )
rel_test_binary=${abs_test_binary_path#$BUILD_ROOT}

if [[ $rel_test_binary == $abs_test_binary_path ]]; then
  fatal "Expected absolute test binary path ('$abs_test_binary_path') to start with" \
        "BUILD_ROOT ('$BUILD_ROOT')"
fi

if [[ -z $log_dir ]]; then
  log_dir=$HOME/logs/$script_name_no_ext/$test_binary_name/$test_filter/$(
    get_timestamp_for_filenames
  )
  mkdir -p "$log_dir"
fi

if [[ $iteration -gt 0 ]]; then
  # One iteration with a specific "id" ($iteration).
  test_log_path_prefix=$log_dir/$iteration
  raw_test_log_path=${test_log_path_prefix}__raw.log
  test_log_path=$test_log_path_prefix.log

  set +e
  export TEST_TMPDIR=/tmp/yb__${0##*/}__$RANDOM.$RANDOM.$RANDOM.$$
  mkdir -p "$TEST_TMPDIR"
  set_expected_core_dir "$TEST_TMPDIR"
  determine_test_timeout

  # TODO: deduplicate the setup here against run_one_test() in common-test-env.sh.
  test_cmd_line=( "$abs_test_binary_path" "$gtest_filter_arg" $more_test_args
                  ${YB_EXTRA_GTEST_FLAGS:-} )
  test_wrapper_cmd_line=(
    "$BUILD_ROOT"/bin/run-with-timeout $(( $timeout_sec + 1 )) "${test_cmd_line[@]}"
  )

  (
    cd "$TEST_TMPDIR"
    if "$verbose"; then
      log "Iteration $iteration logging to $raw_test_log_path"
    fi
    ulimit -c unlimited
    ( set -x; "${test_wrapper_cmd_line[@]}" ) &>"$raw_test_log_path"
  )
  exit_code=$?
  set -e
  if ! did_test_succeed "$exit_code" "$raw_test_log_path"; then
    postprocess_test_log
    process_core_file
    if "$skip_address_already_in_use" && \
       ( egrep '\bAddress already in use\b' "$test_log_path" >/dev/null ||
         egrep '\bWebserver: Could not start on address\b' "$test_log_path" >/dev/null ); then
      # TODO: perhaps we should not skip some types of errors that did_test_succeed finds in the
      # logs (ASAN/TSAN, check failures, etc.), even if we see "address already in use".
      echo "PASSED: iteration $iteration (assuming \"Address already in use\" is a false positive)"
      rm -f "$test_log_path"
    else
      store_log_file_and_report_result "FAILED"
    fi
  elif "$keep_all_logs"; then
    postprocess_test_log
    store_log_file_and_report_result "PASSED"
  else
    echo "PASSED: iteration $iteration"
    rm -f "$raw_test_log_path"
  fi
else
  if [[ -n $yb_compiler_type_from_env ]]; then
    log "YB_COMPILER_TYPE env variable was set to '$yb_compiler_type_from_env' by the caller."
  fi
  log "$gtest_filter_info"
  if [[ -n ${YB_EXTRA_GTEST_FLAGS:-} ]]; then
    log "Extra test flags from YB_EXTRA_GTEST_FLAGS: $YB_EXTRA_GTEST_FLAGS"
  elif "$verbose"; then
    log "YB_EXTRA_GTEST_FLAGS is not set"
  fi
  # Parallel execution of many iterations
  seq 1 $num_iter | \
    xargs -P $parallelism -n 1 "$0" "${original_args[@]}" --log_dir "$log_dir" --iteration
fi
