#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test TestPgRegressYbBitmapScans
grep_in_java_test \
  "failed tests: [yb_bitmap_scans]" \
  TestPgRegressYbBitmapScans
diff <(diff build/latest/postgres_build/src/test/regress/{expected,results}/yb_bitmap_scans.out | head -13) - <<EOT
335,336c335,336
<  Update on tenk3  (cost=6.91..11.21 rows=0 width=544) (actual rows=0 loops=1)
<    ->  YB Bitmap Table Scan on tenk3  (cost=6.91..11.21 rows=10 width=544) (actual rows=110 loops=1)
---
>  Update on tenk3  (cost=6.91..11.21 rows=0 width=0) (actual rows=0 loops=1)
>    ->  YB Bitmap Table Scan on tenk3  (cost=6.91..11.21 rows=10 width=312) (actual rows=110 loops=1)
376,377c376,377
<  Delete on tenk3  (cost=6.75..11.00 rows=0 width=300) (actual rows=0 loops=1)
<    ->  YB Bitmap Table Scan on tenk3  (cost=6.75..11.00 rows=10 width=300) (actual rows=1089 loops=1)
---
>  Delete on tenk3  (cost=6.75..11.00 rows=0 width=0) (actual rows=0 loops=1)
>    ->  YB Bitmap Table Scan on tenk3  (cost=6.75..11.00 rows=10 width=308) (actual rows=1089 loops=1)
786,788c786,788
EOT
