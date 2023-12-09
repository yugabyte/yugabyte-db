#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

java_test 'TestPgRegressMisc#testPgRegressMiscSerial' false
grep_in_java_test \
  'failed tests: [yb_create_language, yb_depend, yb_guc, yb_query_consistent_snapshot, yb_select]' \
  'TestPgRegressMisc#testPgRegressMiscSerial'
# yb_get_range_split_clause should pass.
java_test 'TestPgRegressMisc#testPgRegressMiscSerial2' true
grep_in_java_test \
  'test: yb_get_range_split_clause' \
  'TestPgRegressMisc#testPgRegressMiscSerial2'
