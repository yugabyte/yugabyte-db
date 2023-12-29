#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test TestPgPrefetchControl#testBnlWithSizeLimit
grep_in_java_test \
  "'Nested Loop' is not == 'YB Batched Nested Loop'" \
  TestPgPrefetchControl#testBnlWithSizeLimit
failing_java_test TestPgRegressYbFetchLimits
grep_in_java_test \
  "failed tests: [yb_fetch_limits_joins]" \
  TestPgRegressYbFetchLimits
