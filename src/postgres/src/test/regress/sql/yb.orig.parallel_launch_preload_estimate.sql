-- Test the planner's charge for the catalog data a parallel worker loads
-- where a new backend preloads its catalog caches.  The schedule runs with
-- the tserver gflag ysql_catalog_preload_additional_tables on.
set client_min_messages to 'warning';

drop database if exists preload_test with (force);
create database preload_test;
alter database preload_test set yb_explain_hide_non_deterministic_fields = off;
alter database preload_test set yb_enable_cbo = on;

\c preload_test
set client_min_messages to 'warning';

-- With preloading, a worker repeats the preload of the backend that plans the
-- query, and the relations of its plan are in its caches by then.  The delay
-- follows what is preloaded, so the Gather's startup cost has a charge that
-- the planner computes from the rows its own backend preloaded: 0.77 for each
-- relation of the database and 0.22 for each attribute, among other terms,
-- at the default yb_parallel_setup_cost_per_relation.  It has nothing for the
-- relations of the plan (#36424).

-- 25 partitions of 100 rows.  A condition on k leaves the first N of them
-- after plan-time pruning.
create table p (k int, v int, primary key (k asc)) partition by range (k);
select format('create table p%s partition of p for values from (%s) to (%s)',
              i, (i - 1) * 100 + 1, i * 100 + 1)
from generate_series(1, 25) i \gexec
insert into p select i, i from generate_series(1, 2500) i;
analyze p;

-- The Gather's startup cost less its startup cost with
-- yb_parallel_setup_cost_per_relation at 0.
create function launch_charge(query text) returns numeric
language plpgsql as
$$
declare
  gather_cost constant jsonpath :=
    '$.**?(@."Node Type" == "Gather")."Startup Cost"';
  per_relation constant text :=
    current_setting('yb_parallel_setup_cost_per_relation');
  plan jsonb;
  charge numeric;
begin
  execute 'explain (format json) ' || query into plan;
  charge := jsonb_path_query_first(plan, gather_cost);
  perform set_config('yb_parallel_setup_cost_per_relation', '0', true);
  execute 'explain (format json) ' || query into plan;
  charge := charge - jsonb_path_query_first(plan, gather_cost)::numeric;
  perform set_config('yb_parallel_setup_cost_per_relation', per_relation,
                     true);
  return round(charge, 2);
end;
$$;

-- Catalog Read Requests for each worker launched, the smallest of three runs.
create function worker_reads(query text) returns numeric
language plpgsql as
$$
declare
  plan jsonb;
  reads numeric;
  least_reads numeric;
begin
  for i in 1..3 loop
    execute 'explain (analyze, dist, timing off, format json) ' || query
      into plan;
    reads := round((plan->0->>'Catalog Read Requests')::numeric /
                   jsonb_path_query_first(plan, '$.**."Workers Launched"')::numeric,
                   2);
    least_reads := least(least_reads, reads);
  end loop;
  return least_reads;
end;
$$;

-- A new backend, so that the planning backend has preloaded the relations
-- created above as a worker does.
\c preload_test
set client_min_messages to 'warning';
set yb_test_force_parallel = force;
set yb_enable_parallel_append = on;
set max_parallel_workers_per_gather = 2;

-- The relations of the plan add neither to the charge nor to the catalog reads
-- of a worker.  should return 0 in both columns of every row.
with estimates as materialized (
    select n,
           launch_charge(format('select * from p where k <= %s', n * 100)) as charge,
           worker_reads(format('select * from p where k <= %s', n * 100)) as reads
    from (values (1), (2), (5), (10), (25)) as partitions(n)
)
select n as partitions,
       charge - first_value(charge) over (order by n) as added_charge,
       reads - first_value(reads) over (order by n) as added_reads
from estimates
order by n;

-- The charge follows the catalog of the database.  A table of two columns
-- with a primary key is two relations and ten attributes, counting the system
-- columns and the index's column, so 100 of them add 100 * (2 * 0.77 +
-- 10 * 0.22) = 374.  The planning backend counts them when it preloads, that
-- is, in a new connection.
select launch_charge('select * from p where k <= 100') as charge_before \gset
do $$
begin
  for i in 1..100 loop
    execute format('create table extra_%s (k int primary key, v int)', i);
  end loop;
end;
$$;

\c preload_test
set client_min_messages to 'warning';
set yb_test_force_parallel = force;
set yb_enable_parallel_append = on;
set max_parallel_workers_per_gather = 2;

-- should return 374.00
select launch_charge('select * from p where k <= 100') - :charge_before
       as added_charge;

\c yugabyte
drop database preload_test with (force);
