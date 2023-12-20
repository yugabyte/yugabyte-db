#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test TestPgPrefetchControl
grep_in_java_test "'Seq Scan' is not == 'YB Seq Scan'" TestPgPrefetchControl
failing_java_test TestPgRegressYbFetchLimits
grep_in_java_test \
  "failed tests: [yb_fetch_limits, yb_fetch_limits_joins]" \
  TestPgRegressYbFetchLimits
