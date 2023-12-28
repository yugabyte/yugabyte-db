#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

# Run all test_*.sh.  This is useful to make sure there are no regressions.

found_failure=false
while read -r test_name; do
  test_output_path="$test_result_dir"/"$test_name".txt

  # Run test, capturing out/err to file.
  [ -x pg15_tests/"$test_name".sh ]
  set +e
  pg15_tests/"$test_name".sh "$@" |& tee "$test_output_path"
  result=$?
  set -e

  handle_test_result "$test_output_path" "$test_name" "$result" results.tsv
  if [ $result -ne 0 ]; then
    found_failure=true
  fi
done < <(cd pg15_tests && find . -name 'test_*.sh' | grep -oE 'test_[^.]+')

if "$found_failure"; then
  exit 1
fi
