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
                         '{"(legacy_stats_mode, off, on)"}'::text[];
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

-- change yb_enable_cbo

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

set yb_enable_cbo = off;
select check_optimizer_guc() "(cbo, bscm, stats)";


-- turn on/off old parameters

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

