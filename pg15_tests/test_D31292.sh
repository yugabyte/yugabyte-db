#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test 'TestPgRegressTypesUDT'
grep_in_java_test \
  'failed tests: [yb_base_type, yb_create_type, yb_pg_rowtypes]' \
  'TestPgRegressTypesUDT'

failing_java_test 'TestPgRegressPgMiscIndependent'
grep_in_java_test \
  'failed tests: [yb_pg_dbsize, yb_pg_identity, yb_pg_index_including, yb_pg_misc, yb_pg_sequence]' \
  'TestPgRegressPgMiscIndependent'
