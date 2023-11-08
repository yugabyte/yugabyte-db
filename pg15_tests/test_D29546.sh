#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

java_test TestPgPrefetchControl false
grep_in_java_test "'Seq Scan' is not == 'YB Seq Scan'" TestPgPrefetchControl
java_test TestPgRegressYbFetchLimits false
grep_in_java_test \
  "failed tests: [yb_fetch_limits, yb_fetch_limits_joins]" \
  TestPgRegressYbFetchLimits
