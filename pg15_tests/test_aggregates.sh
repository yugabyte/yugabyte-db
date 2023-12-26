#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test TestPgRegressAggregates
grep_in_java_test \
  "failed tests: [yb_aggregates, yb_pg_aggregates]" \
  TestPgRegressAggregates
