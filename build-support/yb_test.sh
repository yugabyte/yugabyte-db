#!/usr/bin/env bash

set -euo pipefail

NON_GTEST_TESTS=(
  bin/merge-test
  rocksdb-build/bloom_test
  rocksdb-build/compact_on_deletion_collector_test
  rocksdb-build/c_test
  rocksdb-build/cuckoo_table_reader_test
  rocksdb-build/db_sanity_test
  rocksdb-build/dynamic_bloom_test
  rocksdb-build/merge_test
  rocksdb-build/prefix_test
  rocksdb-build/stringappend_test
)

show_help() {
  cat <<-EOT
Usage: ${0##*/} [<options>] <command>
This is our own wrapper around Google Test that acts as a replacement for ctest, especially in cases
when we want to run tests in parallel on a Hadoop cluster.

Options:
  -h, --help
  --build-type
    one of debug (default), fastdebug, release, profile_gen, profile_build
Commands:
  run   Runs the tests one at a time
  list  List individual tests. This is suitable for producing input for a Hadoop Streaming job.
EOT
}

project_dir=$( cd "$( dirname $0 )"/.. && pwd )

build_type="debug"
cmd=""
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

build_dir="$project_dir/build/$build_type"

if [ ! -d "$build_dir" ]; then
  echo "Build directory '$build_dir' not found" >&2
  exit 1
fi

IFS=$'\n'  # so that we can iterate through lines even if they contain spaces
test_binaries=()
num_tests=0
for test_binary in $( find "$build_dir/bin" -type f -name "*test" -executable ); do
  test_binaries+=( "$test_binary" )
  let num_tests+=1
done
for test_binary in $( find "$build_dir/rocksdb-build" -type f -name "*test" -executable ); do
  test_binaries+=( "$test_binary" )
  let num_tests+=1
done

echo "Found $num_tests test binaries"
if [ "$num_tests" -eq 0 ]; then
  echo "No tests binaries found" >&2
  exit 1
fi

non_gtest_tests_re=""
for non_gtest_test in "${NON_GTEST_TESTS[@]}"; do
  if [ -n "$non_gtest_tests_re" ]; then
    non_gtest_tests_re+="|"
  fi
  non_gtest_tests_re+="$non_gtest_test"
done

tests=()
num_test_cases=0

echo "Collecting test cases and tests from gtest binaries"
for test_binary in "${test_binaries[@]}"; do
  if [[ "$test_binary" =~ $non_gtest_tests_re ]]; then
    echo "Known non-gtest test: $test_binary, skipping"
    continue
  fi
  rel_test_binary="${test_binary#$build_dir/}"

  set +e
  gtest_list_tests_result=$( "$test_binary" --gtest_list_tests )
  exit_code=$?
  set -e
  if [ "$exit_code" -ne 0 ]; then
    echo "'$test_binary' does not seem to be a gtest test (--gtest_list_tests failed)" >&2
    # TODO: handle these tests.
  else
    for test_list_item in $gtest_list_tests_result; do
      if [[ "$test_list_item" =~ ^\ \  ]]; then
        test=${test_list_item#  }  # Remove two leading spaces
        test=${test%%#*}  # Remove everything after a "#" comment
        test=${test%  }  # Remove two trailing spaces
        tests+=( "${rel_test_binary}:::$test_case$test" )
      else
        test_case=${test_list_item%%#*}  # Remove everything after a "#" comment
        test_case=${test_case%  }  # Remove two trailing spaces
        let num_test_cases+=1
      fi
    done
  fi
done

num_tests=${#tests[@]}
echo "Found $num_tests GTest tests in $num_test_cases test cases"

test_log_dir="$build_dir/yb-test-logs"
rm -rf "$test_log_dir"
mkdir -p "$test_log_dir"
test_index=1

for t in "${tests[@]}"; do
  test_binary=${t%:::*}
  test_filter=${t#*:::}
  test_binary_sanitized=$( echo "$test_binary" | sed 's/\//__/g; s/[:.]/_/g;' )
  test_filter_sanitized=$( echo "$test_filter" | sed 's/\//__/g; s/[:.]/_/g;' )
  test_log_dir_for_binary="$test_log_dir/$test_binary_sanitized"
  mkdir -p "$test_log_dir_for_binary"
  test_log_path_prefix="$test_log_dir_for_binary/$test_filter_sanitized"
  export TEST_TMPDIR="$test_log_path_prefix.tmp"
  mkdir -p "$TEST_TMPDIR"
  echo "[$test_index/$num_tests] $test_binary, test $test_filter," \
       "logging to $test_log_path_prefix.log"

  set +e
  "$build_dir/$test_binary" \
    "--gtest_filter=$test_filter" \
    "--gtest_output=xml:$test_log_path_prefix.xml" \
    >"$test_log_path_prefix.log" 2>&1
  echo
  if [ $? -ne 0 ]; then
    echo "Test failed"
    # TODO: make sure the xml file gets created.
  fi
  set -e

  let test_index+=1
done
