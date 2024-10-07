#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test TestPgRegressYbBitmapScans
grep_in_java_test \
  "failed tests: [yb_bitmap_scans_system]" \
  TestPgRegressYbBitmapScans
diff <(diff build/latest/postgres_build/src/test/regress/{expected,results}/yb_bitmap_scans_system.out | tail -n +2) - <<EOT
<                                              QUERY PLAN
< ----------------------------------------------------------------------------------------------------
<  Sort (actual rows=11 loops=1)
---
>                                                               QUERY PLAN
> --------------------------------------------------------------------------------------------------------------------------------------
>  Sort (actual rows=15 loops=1)
14c14
<    ->  YB Bitmap Table Scan on pg_authid (actual rows=11 loops=1)
---
>    ->  YB Bitmap Table Scan on pg_authid (actual rows=15 loops=1)
16,18c16,19
<          ->  BitmapOr (actual rows=11 loops=1)
<                ->  Bitmap Index Scan on pg_authid_rolname_index (actual rows=8 loops=1)
<                      Index Cond: ((rolname >= 'pg'::name) AND (rolname < 'ph'::name))
---
>          Recheck Cond: (((rolname >= 'pg'::text) AND (rolname < 'ph'::text)) OR ((rolname >= 'yb'::text) AND (rolname < 'yc'::text)))
>          ->  BitmapOr (actual rows=15 loops=1)
>                ->  Bitmap Index Scan on pg_authid_rolname_index (actual rows=15 loops=1)
>                      Index Cond: ((rolname >= 'pg'::text) AND (rolname < 'ph'::text))
20,21c21,22
<                ->  Bitmap Index Scan on pg_authid_rolname_index (actual rows=3 loops=1)
<                      Index Cond: ((rolname >= 'yb'::name) AND (rolname < 'yc'::name))
---
>                ->  Bitmap Index Scan on pg_authid_rolname_index (actual rows=15 loops=1)
>                      Index Cond: ((rolname >= 'yb'::text) AND (rolname < 'yc'::text))
23c24
< (12 rows)
---
> (13 rows)
27,40c28,45
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
>  oid  |          rolname          | rolsuper | rolinherit | rolcreaterole | rolcreatedb | rolcanlogin | rolreplication | rolbypassrls | rolconnlimit | rolpassword | rolvaliduntil
> ------+---------------------------+----------+------------+---------------+-------------+-------------+----------------+--------------+--------------+-------------+---------------
>  4544 | pg_checkpoint             | f        | t          | f             | f           | f           | f              | f            |           -1 |             |
>  6171 | pg_database_owner         | f        | t          | f             | f           | f           | f              | f            |           -1 |             |
>  4571 | pg_execute_server_program | f        | t          | f             | f           | f           | f              | f            |           -1 |             |
>  3373 | pg_monitor                | f        | t          | f             | f           | f           | f              | f            |           -1 |             |
>  6181 | pg_read_all_data          | f        | t          | f             | f           | f           | f              | f            |           -1 |             |
>  3374 | pg_read_all_settings      | f        | t          | f             | f           | f           | f              | f            |           -1 |             |
>  3375 | pg_read_all_stats         | f        | t          | f             | f           | f           | f              | f            |           -1 |             |
>  4569 | pg_read_server_files      | f        | t          | f             | f           | f           | f              | f            |           -1 |             |
>  4200 | pg_signal_backend         | f        | t          | f             | f           | f           | f              | f            |           -1 |             |
>  3377 | pg_stat_scan_tables       | f        | t          | f             | f           | f           | f              | f            |           -1 |             |
>  6182 | pg_write_all_data         | f        | t          | f             | f           | f           | f              | f            |           -1 |             |
>  4570 | pg_write_server_files     | f        | t          | f             | f           | f           | f              | f            |           -1 |             |
>  8039 | yb_db_admin               | f        | f          | f             | f           | f           | f              | f            |           -1 |             |
>  8030 | yb_extension              | f        | t          | f             | f           | f           | f              | f            |           -1 |             |
>  8032 | yb_fdw                    | f        | t          | f             | f           | f           | f              | f            |           -1 |             |
> (15 rows)
44,45c49,50
<                                                               QUERY PLAN
< --------------------------------------------------------------------------------------------------------------------------------------
---
>                                                                                QUERY PLAN
> ------------------------------------------------------------------------------------------------------------------------------------------------------------------------
50c55
<      ->  YB Bitmap Table Scan on pg_authid (actual rows=12 loops=1)
---
>      ->  YB Bitmap Table Scan on pg_authid (actual rows=16 loops=1)
52c57,58
<            ->  BitmapOr (actual rows=12 loops=1)
---
>            Recheck Cond: ((rolname = 'postgres'::name) OR ((rolname >= 'pg'::text) AND (rolname < 'ph'::text)) OR ((rolname >= 'yb'::text) AND (rolname < 'yc'::text)))
>            ->  BitmapOr (actual rows=16 loops=1)
55,61c61,67
<                  ->  Bitmap Index Scan on pg_authid_rolname_index (actual rows=8 loops=1)
<                        Index Cond: ((rolname >= 'pg'::name) AND (rolname < 'ph'::name))
<                        Storage Index Filter: ((rolname = 'postgres'::name) OR (rolname ~~ 'pg_%'::text) OR (rolname ~~ 'yb_%'::text))
<                  ->  Bitmap Index Scan on pg_authid_rolname_index (actual rows=3 loops=1)
<                        Index Cond: ((rolname >= 'yb'::name) AND (rolname < 'yc'::name))
<                        Storage Index Filter: ((rolname = 'postgres'::name) OR (rolname ~~ 'pg_%'::text) OR (rolname ~~ 'yb_%'::text))
< (15 rows)
---
>                  ->  Bitmap Index Scan on pg_authid_rolname_index (actual rows=16 loops=1)
>                        Index Cond: ((rolname >= 'pg'::text) AND (rolname < 'ph'::text))
>                        Storage Index Filter: ((rolsuper = 'postgres'::name) OR (rolsuper ~~ 'pg_%'::text) OR (rolsuper ~~ 'yb_%'::text))
>                  ->  Bitmap Index Scan on pg_authid_rolname_index (actual rows=16 loops=1)
>                        Index Cond: ((rolname >= 'yb'::text) AND (rolname < 'yc'::text))
>                        Storage Index Filter: ((rolsuper = 'postgres'::name) OR (rolsuper ~~ 'pg_%'::text) OR (rolsuper ~~ 'yb_%'::text))
> (16 rows)
72,73c78,79
<                                                  QUERY PLAN
< ------------------------------------------------------------------------------------------------------------
---
>                                                                                QUERY PLAN
> ------------------------------------------------------------------------------------------------------------------------------------------------------------------------
78c84,86
<      ->  YB Bitmap Table Scan on pg_authid (actual rows=12 loops=1)
---
>      ->  YB Bitmap Table Scan on pg_authid (actual rows=16 loops=1)
>            Recheck Cond: ((rolname = 'postgres'::name) OR ((rolname >= 'pg'::text) AND (rolname < 'ph'::text)) OR ((rolname >= 'yb'::text) AND (rolname < 'yc'::text)))
>            Rows Removed by Index Recheck: 2
80c88
<            ->  BitmapOr (actual rows=12 loops=1)
---
>            ->  BitmapOr (actual rows=18 loops=1)
83,87c91,95
<                  ->  Bitmap Index Scan on pg_authid_rolname_index (actual rows=8 loops=1)
<                        Index Cond: ((rolname >= 'pg'::name) AND (rolname < 'ph'::name))
<                  ->  Bitmap Index Scan on pg_authid_rolname_index (actual rows=3 loops=1)
<                        Index Cond: ((rolname >= 'yb'::name) AND (rolname < 'yc'::name))
< (13 rows)
---
>                  ->  Bitmap Index Scan on pg_authid_rolname_index (actual rows=18 loops=1)
>                        Index Cond: ((rolname >= 'pg'::text) AND (rolname < 'ph'::text))
>                  ->  Bitmap Index Scan on pg_authid_rolname_index (actual rows=18 loops=1)
>                        Index Cond: ((rolname >= 'yb'::text) AND (rolname < 'yc'::text))
> (15 rows)
EOT
