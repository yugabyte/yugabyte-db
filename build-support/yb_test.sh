#!/usr/bin/env bash

set -euo pipefail

show_help() {
  cat <<-EOT
Usage: ${0##*/} [<options>] <command>
This is our own wrapper around Google Test that acts as a replacement for ctest, especially in cases
when we want to run tests in parallel on a Hadoop cluster.

Options:
  -h, --help
  --build-type
    one of debug (default), fastdebug, release, profile_gen, profile_build
  --test-binary-re <test_binary_regex>
    A regular expression for filtering test binary (executable) names.
  --exit-on-failure
    Stop running tests after the first failure

Commands:
  run   Runs the tests one at a time
  list  List individual tests. This is suitable for producing input for a Hadoop Streaming job.
EOT
}

# Sanitize the given string so we can make it a path component.
sanitize_for_path() {
  echo "$1" | sed 's/\//__/g; s/[:.]/_/g;'
}

add_test_binary() {
  local test_binary=$1
  # Only add executable files.
  if [ -x "$test_binary" ]; then
    test_binaries+=( "$test_binary" )
    let num_tests+=1
  fi
}

project_dir=$( cd "$( dirname $0 )"/.. && pwd )

build_type="debug"
test_binary_re=""
cmd=""
exit_on_failure=false

while [ $# -ne 0 ]; do
  case "$1" in
    -h|--help)
      show_help >&2
      exit 1
    ;;
    --build_type)
      build_type="$2"
      shift
    ;;
    --test-binary-re)
      test_binary_re="$2"
      shift
    ;;
    --exit-on-failure)
      exit_on_failure=true
    ;;
    run|list)
      if [ -n "$cmd" ] && [ "$cmd" != "$1" ]; then
        echo "Only one command can be specified, found '$cmd' and '$1'" >&2
        exit 1
      fi
      cmd="$1"
    ;;
    *)
      echo "Invalid option: $1" >&2
      exit 1
  esac
  shift
done

if [ -z "$cmd" ]; then
  echo "No command specified" >&2
  echo >&2
  show_help >&2
  exit 1
fi

if [[ ! "$build_type" =~ ^(debug|fastdebug|release|profile_gen|profile_build)$ ]]; then
  echo "Unexpected build type: $build_type" >&2
  exit 1
fi

BUILD_ROOT="$project_dir/build/$build_type"

. "$( dirname "$BASH_SOURCE" )/common-test-env.sh"

TEST_BINARIES_TO_RUN_AT_ONCE=(
  bin/tablet_server-test
  rocksdb-build/thread_local_test
  rocksdb-build/backupable_db_test
)
TEST_BINARIES_TO_RUN_AT_ONCE_RE=$( make_regex "${TEST_BINARIES_TO_RUN_AT_ONCE[@]}" )


IFS=$'\n'  # so that we can iterate through lines even if they contain spaces
test_binaries=()
num_tests=0
for test_binary in $( find "$BUILD_ROOT/bin" -type f -name "*test" ); do
  add_test_binary "$test_binary"
done
for test_binary in $( find "$BUILD_ROOT/rocksdb-build" -type f -name "*test" ); do
  add_test_binary "$test_binary"
done

echo "Found $num_tests test binaries"
if [ "$num_tests" -eq 0 ]; then
  echo "No tests binaries found" >&2
  exit 1
fi

tests=()
num_tests=0
num_test_cases=0
num_binaries_to_run_at_once=0

echo "Collecting test cases and tests from gtest binaries"
for test_binary in "${test_binaries[@]}"; do
  rel_test_binary="${test_binary#$BUILD_ROOT/}"
  if [ -n "$test_binary_re" ] && [[ ! "$rel_test_binary" =~ $test_binary_re ]]; then
    continue
  fi
  if [ "$rel_test_binary" == "rocksdb-build/db_sanity_test" ]; then
    # db_sanity_check is not a test, but a command-line tool.
    continue
  fi
  if is_known_non_gtest_test_by_rel_path "$rel_test_binary" || \
     [[ "$rel_test_binary" =~ $TEST_BINARIES_TO_RUN_AT_ONCE_RE ]]; then
    tests+=( "$rel_test_binary" )
    let num_binaries_to_run_at_once+=1
    continue
  fi

  gtest_list_stderr_path=$( mktemp -t "gtest_list_tests.stderr.XXXXXXXXXX" )

  set +e
  gtest_list_tests_result=$( "$test_binary" --gtest_list_tests 2>"$gtest_list_stderr_path" )
  exit_code=$?
  set -e

  if [ -s "$gtest_list_stderr_path" ]; then
    (
      echo
      echo "'$test_binary' produced stderr output when run with --gtest_list_tests:"
      echo
      cat "$gtest_list_stderr_path"
      echo
      echo "Please add the test '$rel_test_binary' to the appropriate list in common-test-env.sh"
      echo "or fix the underlying issue in the code."
    ) >&2

    rm -f "$gtest_list_stderr_path"
    exit 1
  fi

  rm -f "$gtest_list_stderr_path"

  if [ "$exit_code" -ne 0 ]; then
    echo "'$test_binary' does not seem to be a gtest test (--gtest_list_tests failed)" >&2
    echo "Please add this test to the appropriate list in common-test-env.sh" >&2
    exit 1
  fi

  for test_list_item in $gtest_list_tests_result; do
    if [[ "$test_list_item" =~ ^\ \  ]]; then
      test=${test_list_item#  }  # Remove two leading spaces
      test=${test%%#*}  # Remove everything after a "#" comment
      test=${test%  }  # Remove two trailing spaces
      tests+=( "${rel_test_binary}:::$test_case$test" )
      let num_tests+=1
    else
      test_case=${test_list_item%%#*}  # Remove everything after a "#" comment
      test_case=${test_case%  }  # Remove two trailing spaces
      let num_test_cases+=1
    fi
  done
done

echo "Found $num_tests GTest tests in $num_test_cases test cases, and" \
     "$num_binaries_to_run_at_once test executables to be run at once"

test_log_dir="$BUILD_ROOT/yb-test-logs"
rm -rf "$test_log_dir"
mkdir -p "$test_log_dir"
test_index=1

global_exit_code=0

echo "Starting tests at $(date)"

set +u
if [ ${#tests[@]} -eq 0 ]; then
  echo "No tests found" >&2
  exit 1
fi
set -u

# Given IFS=$'\n', this is a generic way to sort an array, even if some lines contain spaces
# (which should not be the case for our "<test_binary>:::<test>" strings).
tests=(
  $( for test_str in "${tests[@]}"; do echo "$test_str"; done | sort )
)

test_log_url_prefix=""
if [ -n "${BUILD_URL:-}" ]; then
  build_url_no_trailing_slash=${BUILD_URL%/}
  test_log_url_prefix="${build_url_no_trailing_slash}/artifact/build/$build_type/yb-test-logs"
fi


for t in "${tests[@]}"; do
  if [[ "$t" =~ ::: ]]; then
    test_binary=${t%:::*}
    test_name=${t#*:::}
    run_at_once=false
    what_test_str="test $test_name"
  else
    # Run all test cases in the given test binary
    test_binary=$t
    test_name=""
    run_at_once=true
    what_test_str="all tests"
  fi

  test_binary_sanitized=$( sanitize_for_path "$test_binary" )
  test_name_sanitized=$( sanitize_for_path "$test_name" )

  rel_test_log_path="$test_binary_sanitized"
  if $run_at_once; then
    # Make this similar to the case when we run tests separately. Pretend that the test binary name
    # is the test name.
    rel_test_log_path+="/${test_binary##*/}"
  else
    rel_test_log_path+="/$test_name_sanitized"
  fi
  # At this point $rel_test_log_path contains the common prefix of the log (.log), the temporary
  # directory (.tmp), and the XML report (.xml) for this test relative to the yb-test-logs
  # directory.

  test_log_path_prefix="$test_log_dir/$rel_test_log_path"
  export TEST_TMPDIR="$test_log_path_prefix.tmp"
  mkdir -p "$TEST_TMPDIR"
  test_log_path="$test_log_path_prefix.log"
  echo "[$test_index/${#tests[@]}] $test_binary ($what_test_str), logging to $test_log_path"

  xml_output_file="$test_log_path_prefix.xml"
  abs_test_binary_path="$BUILD_ROOT/$test_binary"
  test_cmd_line=( "$abs_test_binary_path" )

  if is_known_non_gtest_test_by_rel_path "$test_binary"; then
    is_gtest_test=false
  else
    test_cmd_line+=( "--gtest_output=xml:$xml_output_file" )
    is_gtest_test=true
  fi

  if ! "$run_at_once" && "$is_gtest_test"; then
    test_cmd_line+=( "--gtest_filter=$test_name" )
  fi

  test_log_path="$test_log_path_prefix.log"

  pushd "$TEST_TMPDIR" >/dev/null
  ulimit -c unlimited
  set +e
  "${test_cmd_line[@]}" >"$test_log_path" 2>&1
  test_exit_code=$?
  set -e
  popd >/dev/null

  test_failed=false
  if [ "$test_exit_code" -ne 0 ]; then
    echo "$test_binary returned exit code $test_exit_code"
    test_failed=true
  fi
  if [ ! -f "$xml_output_file" ]; then
    if is_known_non_gtest_test_by_rel_path "$test_binary"; then
      echo "$test_binary is a known non-gtest executable, OK that it did not produce XML output"
    else
      echo "$test_binary failed to produce an XML output file at $xml_output_file" >&2
      test_failed=true
    fi
    echo "Generating an XML output file using parse_test_failure.py: $xml_output_file"
    "$project_dir"/build-support/parse_test_failure.py -x <"$test_log_path" >"$xml_output_file"
  fi

  if "$test_failed"; then
    global_exit_code=1
    echo "Test command line: ${test_cmd_line[@]}" >&2
    echo "Log path: $test_log_path" >&2
    if [ -n "${BUILD_URL:-}" ]; then
      # Produce a URL like
      # https://jenkins.dev.yugabyte.com/job/yugabyte-with-custom-test-script/47/artifact/build/debug/yb-test-logs/bin__raft_consensus-itest/RaftConsensusITest_TestChurnyElections.log
      echo "Log URL: $test_log_url_prefix/$rel_test_log_path.log" >&2
    fi
    if [ -f "$TEST_TMPDIR/core" ]; then
      echo "Found a core file at '$TEST_TMPDIR/core', backtrace:" >&2
      set +e
      # gdb command from http://www.commandlinefu.com/commands/view/6004/print-stack-trace-of-a-core-file-without-needing-to-enter-gdb-interactively
      ( set -x; gdb -q -n -ex bt -batch "$abs_test_binary_path" "$TEST_TMPDIR/core" )
      set -e
    fi
    echo >&2
    if "$exit_on_failure"; then
      echo "Exiting after the first failure (--exit-on-failure)" >&2
      exit "$global_exit_code"
    fi
  else
    # Mac OS X slaves are having trouble uploading artifacts, so we delete logs and temporary
    # files for successful tests.
    if [ "$IS_MAC" == "1" ]; then
      rm -rf "$test_log_path" "$TEST_TMPDIR"
    fi
  fi

  let test_index+=1

done

echo "Finished running tests at $(date)"

exit "$global_exit_code"
