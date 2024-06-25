#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test TestPgRegressYbBitmapScans
grep_in_java_test \
  "failed tests: [yb_bitmap_scans_joins, yb_bitmap_scans_system]" \
  TestPgRegressYbBitmapScans
diff <(diff build/latest/postgres_build/src/test/regress/{expected,results}/yb_bitmap_scans_joins.out | tail -n +2) - <<EOT
<                                      QUERY PLAN
< -------------------------------------------------------------------------------------
<  Nested Loop Left Join (actual rows=1 loops=1)
<    ->  Function Scan on yb_table_properties p (actual rows=1 loops=1)
<    ->  YB Bitmap Table Scan on pg_yb_tablegroup gr (actual rows=0 loops=1)
<          ->  Bitmap Index Scan on pg_yb_tablegroup_oid_index (actual rows=0 loops=1)
<                Index Cond: (oid = p.tablegroup_oid)
< (5 rows)
---
>                                         QUERY PLAN
> -------------------------------------------------------------------------------------------
>  Merge Left Join (actual rows=1 loops=1)
>    Merge Cond: (p.tablegroup_oid = gr.oid)
>    ->  Sort (actual rows=1 loops=1)
>          Sort Key: p.tablegroup_oid
>          Sort Method: quicksort
>          ->  Function Scan on yb_table_properties p (actual rows=1 loops=1)
>    ->  Index Scan using pg_yb_tablegroup_oid_index on pg_yb_tablegroup gr (never executed)
> (7 rows)
EOT
