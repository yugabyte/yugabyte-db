#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test 'TestPgRegressTypesUDT'
grep_in_java_test \
  'failed tests: [yb_base_type, yb_create_type]' \
  'TestPgRegressTypesUDT'
