#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test 'org.yb.pgsql.TestPgRegressJoin'
grep_in_java_test \
  "failed tests: [yb_join_batching, yb_join_batching_plans]" \
  TestPgRegressJoin
