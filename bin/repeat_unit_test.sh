#!/usr/bin/env bash

# Copyright (c) YugaByte, Inc.

show_usage() {
  cat <<-EOT
Usage: ${0##*/} <build_type> <test_binary_name> <test_filter> [<options>]
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
  -k, --keep-all-logs
    Keep all logs, not just failing tests' logs.
  --skip-address-already-in-use
    Skip "address already in use" errors that frequently cause false positive test failures.
    The current approach is somewhat simplistic in that we attribute the test failure to the
    "address already in use" issue, even if other issues may be present, but that's OK as long as
    we believe "address already in use" only happens a small percentage of the time.
  --test-args "<arguments>"
    Pass additional arguments to the test.
EOT
}

delete_tmp_files() {
  # Be extra careful before we nuke a directory.
  if [[ -n ${TEST_TMPDIR:-} && $TEST_TMPDIR =~ ^/tmp/ ]]; then
    rm -rf "$TEST_TMPDIR"
  fi
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
original_args=( "$@" )

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
      more_test_args+=" $1=$2"
      shift
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
    --skip-address-already-in-use)
      skip_address_already_in_use=true
    ;;
    --test-args)
      more_test_args+=" $2"
      shift
    ;;
    *)
      positional_args+=( "$1" )
    ;;
  esac
  shift
done

set_cmake_build_type_and_compiler_type
set_build_root
set_asan_tsan_options

declare -i -r num_pos_args=${#positional_args[@]}
if [[ $num_pos_args -ne 2 ]]; then
  show_usage >&2
  fatal "Expected exactly two positional arguments other than build type:" \
        "<test_binary_name> <test_filter>"
fi

test_binary_name=${positional_args[0]}
test_filter=${positional_args[1]}

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
  core_dir=$TEST_TMPDIR
  (
    cd "$TEST_TMPDIR"
    set -x
    "$abs_test_binary_path" --gtest_filter="$test_filter" $more_test_args &>"$raw_test_log_path"
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
      gzip "$test_log_path"
      echo "FAILED: iteration $iteration (log: $test_log_path.gz)"
    fi
  elif "$keep_all_logs"; then
    gzip "$test_log_path"
    echo "PASSED: iteration $iteration (log: $test_log_path.gz)"
  else
    echo "PASSED: iteration $iteration"
    rm -f "$test_log_path"
  fi
else
  # Parallel execution of many iterations
  seq 1 $num_iter | \
    xargs -P $parallelism -n 1 "$0" "${original_args[@]}" --log_dir "$log_dir" --iteration
fi
