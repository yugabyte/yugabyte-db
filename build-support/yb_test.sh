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

add_test_binary() {
  local test_binary=$1
  # Only add executable files.
  if [ -x "$test_binary" ]; then
    test_binaries+=( "$test_binary" )
    let num_test_binaries+=1
  fi
}

print_found_tests_summary() {
  echo "Found $num_tests GTest tests in $num_test_cases test cases in $num_test_binaries" \
       "binaries, and $num_binaries_to_run_at_once test binaries to be run at once (usually due" \
       "internal dependencies between tests, or for non-gtest test binaries.)"
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

if [[ ! "$cmd" =~ ^(run|list)$ ]]; then
  echo "Invalid command: $cmd" >&2
  exit 1
fi

if [[ ! "$build_type" =~ ^(debug|fastdebug|release|profile_gen|profile_build)$ ]]; then
  echo "Unexpected build type: $build_type" >&2
  exit 1
fi

. "$( dirname "$BASH_SOURCE" )/common-test-env.sh"

set_build_root "$build_type"
set_common_test_paths

IFS=$'\n'  # so that we can iterate through lines even if they contain spaces
test_binaries=()
num_test_binaries=0
for rel_test_dir in "${VALID_TEST_BINARY_DIRS[@]}"; do
  for test_binary in $( find "$BUILD_ROOT/$rel_test_dir" -type f -name "*test" ); do
    add_test_binary "$test_binary"
  done
done

echo "Found $num_test_binaries test binaries"
if [ "$num_test_binaries" -eq 0 ]; then
  echo "No tests binaries found" >&2
  exit 1
fi

tests=()
num_tests=0
num_test_cases=0
num_binaries_to_run_at_once=0

echo "Going through all test binaries and collecting test cases (groups of test functions)" \
     "and tests (test functions) from them."

for test_binary in "${test_binaries[@]}"; do
  rel_test_binary="${test_binary#$BUILD_ROOT/}"
  if [ -n "$test_binary_re" ] && [[ ! "$rel_test_binary" =~ $test_binary_re ]]; then
    continue
  fi
  collect_gtest_tests
done

case "$cmd" in
  run)
    # proceed to run the tests
  ;;
  list)
    i=1
    for test in "${tests[@]}"; do
      echo "$i. $test"
      let i+=1
    done
    echo
    print_found_tests_summary
    exit 0
  ;;
  *)
    echo "Invalid command: $cmd" >&2
    exit 1
esac

# Run the tests.

print_found_tests_summary

# Sanity check before we delete the test log directory.
if [ -z "${YB_TEST_LOG_ROOT_DIR:-}" ]; then
  echo "YB_TEST_LOG_ROOT_DIR is not set" >&2
  exit 1
fi

rm -rf "$YB_TEST_LOG_ROOT_DIR"
mkdir -p "$YB_TEST_LOG_ROOT_DIR"
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

set_test_log_url_prefix

for test_descriptor in "${tests[@]}"; do
  prepare_for_running_test

  echo "[$test_index/$num_tests] $test_binary ($what_test_str)"

  run_test_and_process_results

  if "$test_failed" && "$exit_on_failure"; then
    echo "Exiting after the first failure (--exit-on-failure)" >&2
    exit "$global_exit_code"
  fi

  let test_index+=1

done

echo "Finished running tests at $(date)"

exit "$global_exit_code"
