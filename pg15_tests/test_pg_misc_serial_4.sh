#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test 'TestPgRegressMisc#testPgRegressMiscSerial4'
grep_in_java_test \
  "failed tests: [yb_explicit_row_lock_batching]" \
  'TestPgRegressMisc#testPgRegressMiscSerial4'
# YB_TODO: this is a combination of two issues:
# - former storage read requests difference assigned to Karthik
# - after D36888, it appears to need further adjustment
diff <(diff build/latest/postgres_build/src/test/regress/{expected,results}/yb_explicit_row_lock_batching.out) - <<EOT
520c520
<  Storage Read Requests: 6
---
>  Storage Read Requests: 5
562c562
<  Storage Read Requests: 8
---
>  Storage Read Requests: 7
EOT
