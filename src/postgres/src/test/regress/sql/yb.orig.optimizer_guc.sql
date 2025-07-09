--
-- test yb_enable_cbo and legacy counterpart value sync
--

reset yb_enable_cbo;
reset yb_enable_base_scans_cost_model;
reset yb_enable_optimizer_statistics;

show yb_enable_cbo;
show yb_enable_base_scans_cost_model;
show yb_enable_optimizer_statistics;

-- change yb_enable_cbo

set yb_enable_cbo = on;
show yb_enable_cbo;
show yb_enable_base_scans_cost_model;
show yb_enable_optimizer_statistics;

set yb_enable_cbo = off;
show yb_enable_cbo;
show yb_enable_base_scans_cost_model;
show yb_enable_optimizer_statistics;

set yb_enable_cbo = legacy_mode;
show yb_enable_cbo;
show yb_enable_base_scans_cost_model;
show yb_enable_optimizer_statistics;

set yb_enable_cbo = legacy_stats_mode;
show yb_enable_cbo;
show yb_enable_base_scans_cost_model;
show yb_enable_optimizer_statistics;

set yb_enable_cbo = off;
show yb_enable_cbo;
show yb_enable_base_scans_cost_model;
show yb_enable_optimizer_statistics;


-- turn on/off old parameters

set yb_enable_base_scans_cost_model = off;
show yb_enable_cbo;
show yb_enable_base_scans_cost_model;
show yb_enable_optimizer_statistics;
set yb_enable_optimizer_statistics = off;
show yb_enable_cbo;
show yb_enable_base_scans_cost_model;
show yb_enable_optimizer_statistics;
set yb_enable_optimizer_statistics = on;
show yb_enable_cbo;
show yb_enable_base_scans_cost_model;
show yb_enable_optimizer_statistics;
set yb_enable_base_scans_cost_model = on;
show yb_enable_cbo;
show yb_enable_base_scans_cost_model;
show yb_enable_optimizer_statistics;
set yb_enable_base_scans_cost_model = off;
show yb_enable_cbo;
show yb_enable_base_scans_cost_model;
show yb_enable_optimizer_statistics;
set yb_enable_optimizer_statistics = off;
show yb_enable_cbo;
show yb_enable_base_scans_cost_model;
show yb_enable_optimizer_statistics;


-- boolean aliases

set yb_enable_cbo = true;
show yb_enable_cbo;
set yb_enable_cbo = false;
show yb_enable_cbo;
set yb_enable_cbo = yes;
show yb_enable_cbo;
set yb_enable_cbo = no;
show yb_enable_cbo;
set yb_enable_cbo = 1;
show yb_enable_cbo;
set yb_enable_cbo = 0;
show yb_enable_cbo;


-- error

set yb_enable_cbo = oui;


--
-- test GUC_EXPLAIN flag
--

reset all;

explain (settings, costs off)
select * from pg_class;

set yb_enable_cbo = on;

explain (settings, costs off)
select * from pg_class;

set yb_enable_cbo = legacy_stats_mode;

explain (settings, costs off)
select * from pg_class;

set yb_bnl_optimize_first_batch = off;
set yb_prefer_bnl = off;
set yb_enable_batchednl = off;
set yb_enable_geolocation_costing = off;
set yb_pushdown_strict_inequality = off;
set yb_pushdown_is_not_null = off;
set yb_enable_distinct_pushdown = off;
set yb_bypass_cond_recheck = off;
set yb_enable_parallel_append = on;
set yb_enable_bitmapscan = on;
set yb_enable_base_scans_cost_model = on;
set yb_enable_optimizer_statistics = on;
set yb_bnl_batch_size = 1;
set yb_fetch_row_limit = 0;
set yb_fetch_size_limit = '4GB';
set yb_parallel_range_rows = 10;
set yb_parallel_range_size = '1kB';
set yb_network_fetch_cost = 100;

explain (settings, costs off)
select * from pg_class;


-- reset all then try a few yb_ parameters that won't affect plans

reset all;

explain (settings, costs off)
select * from pg_class;

set yb_enable_memory_tracking = off;
set yb_enable_planner_trace = on;

explain (settings, costs off)
select * from pg_class;
