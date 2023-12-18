#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

# Run passing C++ tests.

found_failure=false
while read -r line; do
  test_program=$(cut -f 1 <<<"$line")
  test_descriptor=$(cut -f 2 <<<"$line")
  test_output_path="$test_result_dir"/"$test_descriptor".txt

  # Run test, capturing out/err to file.
  set +e
  "${build_cmd[@]}" \
    --cxx-test "$test_program" \
    --gtest_filter "$test_descriptor" \
    |& tee "$test_output_path"
  result=$?
  set -e

  handle_test_result "$test_output_path" "$test_descriptor" "$result" passing_cxx_results.tsv
  if [ $result -ne 0 ]; then
    found_failure=true
  fi
done <pg15_tests/passing_cxx.tsv

if "$found_failure"; then
  exit 1
fi
