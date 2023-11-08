#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

# yb_get_range_split_clause should pass.
java_test 'TestPgRegressMisc#testPgRegressMiscSerial' false
grep_in_java_test \
  'failed tests: [yb_create_language, yb_create_table_like, yb_depend, yb_explicit_row_lock_planning, yb_guc, yb_query_consistent_snapshot, yb_select]' \
  'TestPgRegressMisc#testPgRegressMiscSerial'
