#!/usr/bin/env bash
# Run all test_*.sh.  This is useful to make sure there are no regressions.
set -euo pipefail
cd "${BASH_SOURCE[0]%/*}"
test_result_dir=../build/latest/pg15_tests
tsv_path=${test_result_dir}/results.tsv

# This assumes latest symlink is already present/correct.
mkdir -p "$test_result_dir"

find . -name 'test_*.sh' \
  | grep -oE 'test_[^.]+' \
  | while read -r test_name; do
  test_output_path="$test_result_dir"/"$test_name".txt

  # Run test, capturing out/err to file.
  [ -x "$test_name".sh ]
  set +e
  ./"$test_name".sh "$@" |& tee "$test_output_path"
  result=$?
  set -e

  datetime=$(date -Iseconds)
    # In case of failure, persist failure output.
  if [ $result -ne 0 ]; then
    cp "$test_output_path" "$test_output_path"."$datetime"
  fi

  # Output tsv row: date, test, exit code
  echo -e "$datetime\t$test_name\t$result" | tee -a "$tsv_path"
done
