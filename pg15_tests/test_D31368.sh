#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

# On MacOS, yb_get_current_transaction_priority fails also - but that happens in master branch too.
failing_java_test 'TestPgRegressProc'
grep_in_java_test \
  'failed tests: [yb_hash_code]' \
  'TestPgRegressProc'
