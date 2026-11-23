--
-- test YB optimizer GUCs
--


----------------------------------------------------------------
-- yb_enable_parallel_append - enable_parallel_append mapping --
----------------------------------------------------------------
show yb_enable_parallel_append;
show enable_parallel_append;
set yb_enable_parallel_append = off;
show enable_parallel_append;
set enable_parallel_append = on;
show yb_enable_parallel_append;


---------------------------
-- test GUC_EXPLAIN flag --
---------------------------

reset all;

explain (settings, costs off)
select * from pg_class;

set yb_enable_cbo = on;

explain (settings, costs off)
select * from pg_class;

set yb_bnl_optimize_first_batch = off;
set yb_prefer_bnl = off;
set yb_enable_batchednl = off;
set yb_enable_geolocation_costing = off;
set yb_enable_distinct_pushdown = off;
set yb_test_skip_binding_scan_keys = on;
set yb_enable_bitmapscan = on;
set yb_bnl_batch_size = 1;
set yb_fetch_row_limit = 0;
set yb_fetch_size_limit = '2kB';
set yb_parallel_range_rows = 10;
set yb_parallel_range_size = '1kB';
set yb_network_fetch_cost = 100;

explain (settings, costs off)
select * from pg_class;


---------------------------------------------------------------------
-- reset all then try a few yb_ parameters that won't affect plans --
---------------------------------------------------------------------

reset all;

explain (settings, costs off)
select * from pg_class;

set yb_enable_memory_tracking = off;
set yb_enable_planner_trace = on;

explain (settings, costs off)
select * from pg_class;
