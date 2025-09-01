--
-- test yb_enable_cbo and legacy counterpart value sync
--

set client_min_messages = warning;
drop function if exists check_optimizer_guc;

create function check_optimizer_guc() returns text
language plpgsql as
$$
declare
    cbo text;
    bscm text;
    stats text;
    vals text;
    valid_vals text[] := '{"(on, on, on)"}'::text[] ||
               		 '{"(on, on, off)"}'::text[] ||
                         '{"(legacy_mode, off, off)"}'::text[] ||
                         '{"(off, off, off)"}'::text[] || /* legacy ignore stats */
                         '{"(legacy_stats_mode, off, on)"}'::text[] ||
                         '{"(legacy_bnl_mode, off, off)"}'::text[] ||
                         '{"(legacy_stats_bnl_mode, off, on)"}'::text[];
begin
    execute 'show yb_enable_cbo' into cbo;
    execute 'show yb_enable_base_scans_cost_model' into bscm;
    execute 'show yb_enable_optimizer_statistics' into stats;
    vals := '(' || cbo || ', ' || bscm || ', ' || stats || ')';
    if vals = any(valid_vals) then
	return vals;
    else
	return '*** Invalid value combination: ' || vals;
    end if;
end;
$$;


reset yb_enable_cbo;
reset yb_enable_base_scans_cost_model;
reset yb_enable_optimizer_statistics;

select check_optimizer_guc() "(cbo, bscm, stats)";

--------------------------
-- change yb_enable_cbo --
--------------------------

set yb_enable_cbo = on;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_cbo = legacy_stats_mode;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_cbo = on;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_cbo = legacy_mode;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_cbo = on;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_cbo = legacy_bnl_mode;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_cbo = on;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_cbo = off;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_cbo = legacy_mode;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_cbo = off;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_cbo = legacy_stats_mode;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_cbo = legacy_mode;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_cbo = legacy_stats_mode;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_cbo = on;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_cbo = legacy_bnl_mode;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_cbo = off;
select check_optimizer_guc() "(cbo, bscm, stats)";


--------------------------------
-- turn on/off old parameters --
--------------------------------

-- (bscm, stats) off, off -> stats = on
set yb_enable_optimizer_statistics = on;
select check_optimizer_guc() "(cbo, bscm, stats)";

-- (bscm, stats) off, on -> bscm = on
set yb_enable_base_scans_cost_model = on;
select check_optimizer_guc() "(cbo, bscm, stats)";

-- (bscm, stats) on, on -> bscm = off
set yb_enable_base_scans_cost_model = off;
select check_optimizer_guc() "(cbo, bscm, stats)";

-- (bscm, stats) off, on -> stats = off
set yb_enable_optimizer_statistics = off;
select check_optimizer_guc() "(cbo, bscm, stats)";

-- (bscm, stats) off, off -> bscm = on
set yb_enable_base_scans_cost_model = on;
select check_optimizer_guc() "(cbo, bscm, stats)";

-- (bscm, stats) on, off -> stats = on
set yb_enable_optimizer_statistics = on;
select check_optimizer_guc() "(cbo, bscm, stats)";

-- (bscm, stats) on, on -> stats = off
set yb_enable_optimizer_statistics = off;
select check_optimizer_guc() "(cbo, bscm, stats)";

-- (bscm, stats) on, off -> bscm = off
set yb_enable_base_scans_cost_model = off;
select check_optimizer_guc() "(cbo, bscm, stats)";


--------------------------------------------------------------------
-- no legacy_(stats_)bnl_mode after changing either bscm or stats --
--------------------------------------------------------------------

set yb_enable_cbo = legacy_bnl_mode;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_base_scans_cost_model = on;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_cbo = legacy_bnl_mode;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_base_scans_cost_model = off;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_cbo = legacy_bnl_mode;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_optimizer_statistics = on;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_cbo = legacy_bnl_mode;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_optimizer_statistics = off;
select check_optimizer_guc() "(cbo, bscm, stats)";


set yb_enable_cbo = legacy_stats_bnl_mode;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_base_scans_cost_model = on;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_cbo = legacy_stats_bnl_mode;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_base_scans_cost_model = off;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_cbo = legacy_stats_bnl_mode;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_optimizer_statistics = on;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_cbo = legacy_stats_bnl_mode;
select check_optimizer_guc() "(cbo, bscm, stats)";

set yb_enable_optimizer_statistics = off;
select check_optimizer_guc() "(cbo, bscm, stats)";


---------------------
-- boolean aliases --
---------------------

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


-----------
-- error --
-----------

set yb_enable_cbo = oui;


---------------------------
-- test GUC_EXPLAIN flag --
---------------------------

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
