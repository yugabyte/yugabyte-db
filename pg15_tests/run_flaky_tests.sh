#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

found_failure=false
# Change IFS to preserve tabs.
IFS=$'\n'
while read -r line; do
  test_program=$(cut -f 1 <<<"$line")
  test_descriptor=$(cut -f 2 <<<"$line")
  test_output_path="$test_result_dir"/"${test_descriptor//\//_}"

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

  # Clean up from any old runs.
  rm -rf "$test_output_path"
  mkdir "$test_output_path"

  # Conditions for overall pass:
  # - Fails <= 3 times.
  # - Fail rate is <= 30%.
  # Examples:
  # - P
  # - F P P P
  # - F P P F P F P P P P
  max_fail_rate=0.3
  max_fails=3
  fails=0
  iter=0
  while [ "$fails" -le "$max_fails" ] \
        && { [ "$iter" -eq 0 ] \
             || [ "$(bc -l <<<"$fails / $iter > $max_fail_rate")" -eq 1 ]; }; do
    # Run test, capturing out/err to file.
    set +e
    "${test_cmdline[@]}" \
      |& tee "$test_output_path"/"$iter".txt
    result=$?
    set -e

    if [ "$result" -ne 0 ]; then
      fails=$((fails + 1))
    fi
    iter=$((iter + 1))
  done
  # Overall result.
  if [ "$fails" -le "$max_fails" ] \
     && [ "$(bc -l <<<"$fails / $iter <= $max_fail_rate")" -eq 1 ]; then
    result=0
  else
    result=1
  fi
  fail_rate=$(bc -l <<<"scale=2; $fails / $iter")

  handle_test_result "$test_output_path" "$test_descriptor" "$result" flaky_results.tsv \
    "$fail_rate"
  if [ "$result" -ne 0 ]; then
    found_failure=true
  fi
done <pg15_tests/flaky_tests.tsv

if "$found_failure"; then
  exit 1
fi
