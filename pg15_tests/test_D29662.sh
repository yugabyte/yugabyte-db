#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test 'TestPgRegressMisc#testPgRegressMiscSerial'
grep_in_java_test \
  'failed tests: [yb_create_language, yb_depend, yb_guc, yb_query_consistent_snapshot, yb_select]' \
  'TestPgRegressMisc#testPgRegressMiscSerial'
