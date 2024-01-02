#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

# Run passing Java tests.

found_failure=false
while read -r test_descriptor; do
  test_output_path="$test_result_dir"/"$test_descriptor".txt

  # Run test, capturing out/err to file.
  set +e
  java_test "$test_descriptor" |& tee "$test_output_path"
  result=$?
  set -e

  handle_test_result "$test_output_path" "$test_descriptor" "$result" passing_java_results.tsv
  if [ $result -ne 0 ]; then
    found_failure=true
  fi
done <pg15_tests/passing_java.tsv

if "$found_failure"; then
  exit 1
fi
