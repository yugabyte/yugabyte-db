#!/usr/bin/env bash
# Run given test n times.  This is useful to make sure newly introduced tests
# are not flaky.  Stop running upon encountering first failure.
#
# Example usage:
#     .../run_test_n_times.sh .../test_D29226.sh 10 fastdebug --gcc11
set -euo pipefail
cd "${BASH_SOURCE[0]%/*}"

test_file=${1##*/}
times=$2
shift 2

# Use trap to handle both ctrl+c and failure exits.
i=0
trap 'echo "Stopped on iteration $i"' EXIT
for i in $(seq 1 "$times"); do
  ./"$test_file" "$@"
done
trap - EXIT
