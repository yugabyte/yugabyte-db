#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test TestPgRegressYbBitmapScans
grep_in_java_test \
  "failed tests: [yb_bitmap_scans]" \
  TestPgRegressYbBitmapScans
diff <(diff build/latest/postgres_build/src/test/regress/{expected,results}/yb_bitmap_scans.out | head -1) - <<EOT
870,872c870,872
EOT
