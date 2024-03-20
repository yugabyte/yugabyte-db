#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test 'org.yb.pgsql.TestPgRegressTrigger'
grep_in_java_test \
  "failed tests: [yb_pg_triggers]" \
  TestPgRegressTrigger
