#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test TestPgRegressMisc#testPgRegressMiscSerial4
grep_in_java_test \
  "failed tests: [yb_explicit_row_lock_planning]" \
  TestPgRegressMisc#testPgRegressMiscSerial4
