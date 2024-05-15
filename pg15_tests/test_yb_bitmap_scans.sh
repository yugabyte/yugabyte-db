#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test TestPgRegressYbBitmapScans
grep_in_java_test \
  "failed tests: [yb_bitmap_scans]" \
  TestPgRegressYbBitmapScans
diff <(diff build/latest/postgres_build/src/test/regress/{expected,results}/yb_bitmap_scans.out) - <<EOT
288,289c288,289
<  Update on tenk3  (cost=6.91..11.21 rows=0 width=544) (actual rows=0 loops=1)
<    ->  YB Bitmap Table Scan on tenk3  (cost=6.91..11.21 rows=10 width=544) (actual rows=110 loops=1)
---
>  Update on tenk3  (cost=6.91..11.21 rows=0 width=0) (actual rows=0 loops=1)
>    ->  YB Bitmap Table Scan on tenk3  (cost=6.91..11.21 rows=10 width=312) (actual rows=110 loops=1)
329,330c329,330
<  Delete on tenk3  (cost=6.75..11.00 rows=0 width=300) (actual rows=0 loops=1)
<    ->  YB Bitmap Table Scan on tenk3  (cost=6.75..11.00 rows=10 width=300) (actual rows=1089 loops=1)
---
>  Delete on tenk3  (cost=6.75..11.00 rows=0 width=0) (actual rows=0 loops=1)
>    ->  YB Bitmap Table Scan on tenk3  (cost=6.75..11.00 rows=10 width=308) (actual rows=1089 loops=1)
649,650c649,650
< --------------------------------------------------------------------------------------------------------------------------
<  Sort  (cost=4.73..4.74 rows=1 width=115) (actual rows=11 loops=1)
---
> ---------------------------------------------------------------------------------------------------------------------------
>  Sort  (cost=4.73..4.74 rows=1 width=119) (actual rows=0 loops=1)
653c653,655
<    ->  YB Bitmap Table Scan on pg_authid  (cost=0.69..4.72 rows=1 width=115) (actual rows=11 loops=1)
---
>    ->  YB Bitmap Table Scan on pg_authid  (cost=0.69..4.72 rows=1 width=119) (actual rows=0 loops=1)
>          Recheck Cond: ((rolname ~~ 'pg_%'::text) OR (rolname ~~ 'yb_%'::text))
>          Rows Removed by Index Recheck: 3
655,660c657,663
<          ->  BitmapOr  (cost=0.69..0.69 rows=2 width=0) (actual rows=11 loops=1)
<                ->  Bitmap Index Scan on pg_authid_rolname_index  (cost=0.00..0.35 rows=1 width=0) (actual rows=8 loops=1)
<                      Index Cond: ((rolname >= 'pg'::name) AND (rolname < 'ph'::name))
<                ->  Bitmap Index Scan on pg_authid_rolname_index  (cost=0.00..0.35 rows=1 width=0) (actual rows=3 loops=1)
<                      Index Cond: ((rolname >= 'yb'::name) AND (rolname < 'yc'::name))
< (10 rows)
---
>          Rows Removed by Filter: 15
>          ->  BitmapOr  (cost=0.69..0.69 rows=2 width=0) (actual rows=18 loops=1)
>                ->  Bitmap Index Scan on pg_authid_rolname_index  (cost=0.00..0.35 rows=1 width=0) (actual rows=18 loops=1)
>                      Index Cond: ((rolname >= 'pg'::text) AND (rolname < 'ph'::text))
>                ->  Bitmap Index Scan on pg_authid_rolname_index  (cost=0.00..0.35 rows=1 width=0) (actual rows=18 loops=1)
>                      Index Cond: ((rolname >= 'yb'::text) AND (rolname < 'yc'::text))
> (13 rows)
664,677c667,669
<           rolname          | rolsuper | rolinherit | rolcreaterole | rolcreatedb | rolcanlogin | rolreplication | rolbypassrls | rolconnlimit | rolpassword | rolvaliduntil
< ---------------------------+----------+------------+---------------+-------------+-------------+----------------+--------------+--------------+-------------+---------------
<  pg_execute_server_program | f        | t          | f             | f           | f           | f              | f            |           -1 |             |
<  pg_monitor                | f        | t          | f             | f           | f           | f              | f            |           -1 |             |
<  pg_read_all_settings      | f        | t          | f             | f           | f           | f              | f            |           -1 |             |
<  pg_read_all_stats         | f        | t          | f             | f           | f           | f              | f            |           -1 |             |
<  pg_read_server_files      | f        | t          | f             | f           | f           | f              | f            |           -1 |             |
<  pg_signal_backend         | f        | t          | f             | f           | f           | f              | f            |           -1 |             |
<  pg_stat_scan_tables       | f        | t          | f             | f           | f           | f              | f            |           -1 |             |
<  pg_write_server_files     | f        | t          | f             | f           | f           | f              | f            |           -1 |             |
<  yb_db_admin               | f        | f          | f             | f           | f           | f              | f            |           -1 |             |
<  yb_extension              | f        | t          | f             | f           | f           | f              | f            |           -1 |             |
<  yb_fdw                    | f        | t          | f             | f           | f           | f              | f            |           -1 |             |
< (11 rows)
---
>  oid | rolname | rolsuper | rolinherit | rolcreaterole | rolcreatedb | rolcanlogin | rolreplication | rolbypassrls | rolconnlimit | rolpassword | rolvaliduntil
> -----+---------+----------+------------+---------------+-------------+-------------+----------------+--------------+--------------+-------------+---------------
> (0 rows)
EOT
