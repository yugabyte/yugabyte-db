#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test 'TestPgRegressMisc#testPgRegressMiscSerial4'
grep_in_java_test \
  "failed tests: [yb_explicit_row_lock_batching]" \
  'TestPgRegressMisc#testPgRegressMiscSerial4'
diff <(diff build/latest/postgres_build/src/test/regress/{expected,results}/yb_explicit_row_lock_batching.out) - <<EOT
520c520
<  Storage Read Requests: 7
---
>  Storage Read Requests: 6
EOT
