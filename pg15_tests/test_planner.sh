#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test TestPgRegressPlanner
grep_in_java_test \
  "failed tests: [yb_planner_size_estimates]" \
  TestPgRegressPlanner
