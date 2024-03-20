#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test 'TestPgRegressMisc#testPgRegressMiscSerial'
grep_in_java_test \
  'failed tests: [yb_create_language, yb_depend, yb_guc, yb_query_consistent_snapshot]' \
  'TestPgRegressMisc#testPgRegressMiscSerial'
# yb_create_language should only have a small failure.
diff <(diff build/latest/postgres_build/src/test/regress/{expected,results}/yb_create_language.out | head -1) - <<EOT
13c13
EOT
