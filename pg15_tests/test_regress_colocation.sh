#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test 'TestPgRegressColocation'
grep_in_java_test \
  'failed tests: [yb_feature_colocation]' \
  'TestPgRegressColocation'
# yb_feature_colocation should only have small failures.
diff <(diff build/latest/postgres_build/src/test/regress/{expected,results}/yb_feature_colocation.out) - <<EOT
846c846
<          Distinct Prefix: 1
---
>          Distinct Prefix: 2
863c863
<          Distinct Prefix: 1
---
>          Distinct Prefix: 2
EOT
