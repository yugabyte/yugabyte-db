#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

found_failure=false
# Change IFS to preserve tabs.
IFS=$'\n'
while read -r line; do
  test_program=$(cut -f 1 <<<"$line")
  test_descriptor=$(cut -f 2 <<<"$line")
  test_output_path="$test_result_dir"/"${test_descriptor//\//_}".txt

  if [ -n "$test_program" ]; then
    test_cmdline=(
      cxx_test
      "$test_program"
      "$test_descriptor"
    )
  else
    test_cmdline=(
      java_test
      "$test_descriptor"
    )
  fi

  # Run test, capturing out/err to file.
  set +e
  "${test_cmdline[@]}" \
    |& tee "$test_output_path"
  result=$?
  set -e

  handle_test_result "$test_output_path" "$test_descriptor" "$result" passing_results.tsv
  if [ $result -ne 0 ]; then
    found_failure=true
  fi
done <pg15_tests/passing_tests.tsv

if "$found_failure"; then
  exit 1
fi
