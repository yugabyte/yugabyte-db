--
-- Test parallel BNL plan correctness
--

-- Create colocated tables so we can test parallel plans, too.
\c yugabyte
set client_min_messages = 'warning';
drop database if exists colocateddb with (force);
create database colocateddb with colocation = on;
\c colocateddb

create table r (a int, x char(10), data text, primary key(a))
with (colocation = on);

create table s (b int, data text, primary key(b))
with (colocation = on);

create table t (a int, b int, data text, primary key(a, b))
with (colocation = on);

create table u (a int, b int, c int, data text, primary key(a, b, c))
with (colocation = on);

insert into r
  select i, rpad((i%2)::bpchar||(i%3)::bpchar, 10, '*'), lpad(i::bpchar, 5000, '#')::text
  from generate_series(1, 100) i;

insert into s select i, lpad(i::bpchar, 5000, '#')::text from generate_series(1, 200) i;

insert into t
  select r.a, s.b, lpad((r.a+s.b)::bpchar, 5000, '#')::text from r, s
    where ((r.a + s.b * 5000) % (200000/10000)) = 0;

insert into u
  select i / 10, i / 10, i, lpad(i::text, 5000, '#')::text from generate_series(1, 1000) i;

analyze r, s, t, u;


set yb_enable_cbo = on;
set yb_prefer_bnl = off; -- disable pre-BNL-hint compatibility mode

-- Force BNL by default
SET enable_hashjoin = off;
SET enable_mergejoin = off;
SET enable_seqscan = off;
SET enable_material = off;
SET enable_nestloop = off;

SET yb_bnl_batch_size = 3;

-- Run `query` batched in parallel, batched serially, and unbatched, and report
-- each plan followed by how the three row sets compare.  A batching bug that
-- drops rows then shows up as a count difference instead of only as a plan
-- nobody reads.  Each regime's rows are left behind in bnl_parallel,
-- bnl_serial and bnl_unbatched so a caller can assert more than the counts;
-- the next call replaces them.
--
-- `query` carries its own hint comment.  pg_hint_plan prefers a nested
-- statement's own hints to the caller's, and everything built in front of the
-- comment here -- EXPLAIN options, a table name -- stays within the characters
-- it allows to precede a hint.  The settings are local, so the caller's session
-- is unchanged on return.
create function bnl_variants(query text) returns setof text
language plpgsql as $$
declare
  regimes  text[] := array['parallel', 'serial', 'unbatched'];
  regime   text;
  line     text;
  n        bigint;
  counts   text := '';
begin
  foreach regime in array regimes
  loop
    if to_regclass('bnl_' || regime) is not null then
      execute 'drop table bnl_' || regime;
    end if;
  end loop;

  foreach regime in array regimes
  loop
    perform set_config('yb_test_force_parallel',
                       case regime when 'parallel' then 'force' else 'off' end,
                       true);
    perform set_config('max_parallel_workers_per_gather',
                       case regime when 'parallel' then '2' else '0' end, true);
    perform set_config('yb_bnl_batch_size',
                       case regime when 'unbatched' then '1' else '3' end, true);
    -- Batching is the only join method the settings above leave enabled, so
    -- plain nested loops have to come back for the run that takes it away.
    perform set_config('enable_nestloop',
                       case regime when 'unbatched' then 'on' else 'off' end,
                       true);

    return next '-- ' || regime;
    begin
      for line in execute 'explain (costs off) ' || query
      loop
        return next line;
      end loop;

      execute 'create temporary table bnl_' || regime || ' as ' || query;
    exception when others then
      -- One regime failing must not take the others down with it, or a planner
      -- error in any of them hides every result this reports.  Parenthesized
      -- numbers are masked because the ones seen so far are path ids, which
      -- move with path enumeration order.
      return next regime || ' failed: '
                  || regexp_replace(sqlerrm, '\(\d+\)', '(masked)', 'g');
    end;
  end loop;

  foreach regime in array regimes
  loop
    if to_regclass('bnl_' || regime) is null then
      counts := counts || ' ' || regime || '=none';
    else
      execute 'select count(*) from bnl_' || regime into n;
      counts := counts || ' ' || regime || '=' || n;
    end if;
  end loop;
  return next 'rows:' || counts;

  foreach regime in array regimes
  loop
    continue when regime = 'unbatched';

    if to_regclass('bnl_' || regime) is null
       or to_regclass('bnl_unbatched') is null
    then
      return next regime || ' vs unbatched: not compared';
      continue;
    end if;

    execute 'select count(*) from ('
            || '(select * from bnl_' || regime
            || ' except all select * from bnl_unbatched)'
            || ' union all '
            || '(select * from bnl_unbatched'
            || ' except all select * from bnl_' || regime || ')) d'
      into n;
    return next regime || ' vs unbatched: '
                || case when n = 0 then 'same rows' else n || ' rows differ' end;
  end loop;
end $$;

--
-- #28112: BNL joining `s` and `u` must not have mixture of batched and
-- unbatched variables from `r` and `t`.
--
-- Note: join condition push down to the outer relation of inner BNL in
-- cascaded BNL setup not supported yet:
-- https://github.com/yugabyte/yugabyte-db/issues/28847
--

-- The explicit joins and the collapse limit keep the planner on the join tree
-- the Leading hint names, which keeps the Leading hint pruning from meeting a
-- Gather it does not tolerate yet.
-- Workaround for https://github.com/yugabyte/yugabyte-db/issues/28510
set join_collapse_limit = 1;

select bnl_variants($q$
/*+
  Leading(((r t) (s u)))
  IndexScan(r)
  IndexScan(s)
  IndexScan(t)
  IndexScan(u)
  YbBatchedNL(r t)
  YbBatchedNL(s u)
  YbBatchedNL(r s t u)
*/
select t.a, s.b, u.c
from
  (r join t on r.a = t.a)
  join (s join u on s.b = u.b) on s.b = t.b and r.a = u.a
where r.x like '%0%'
$q$);

-- Check stats of the results
select count(*) nrows,
    count(a) a_cnt, count(distinct a) a_ndv, min(a) a_min, max(a) a_max,
    count(b) b_cnt, count(distinct b) b_ndv, min(b) b_min, max(b) b_max,
    count(c) c_cnt, count(distinct c) c_ndv, min(c) c_min, max(c) c_max
  from bnl_parallel;

reset join_collapse_limit;

--
-- #34069: a batched index condition whose outer expression spans two relations
-- batched by two different joins.  t2b is probed on its first key column with
-- t1b.b + t1a.b, but one join fills t1b's batch slots and another fills t1a's,
-- and the probe list is built with one index into both batches, so it holds
-- their diagonal instead of their cross product and rows go missing.
--
-- No Leading hint: that hint is what enables the join pruning that rejects a
-- Gather at an intermediate join (#28510), and without it that pruning never
-- runs.  The explicit joins and the collapse limit fix the join tree instead,
-- and the data fixes each join's direction: t1a and t1b are small, t2 is not,
-- so each join is cheapest with the t1 side, or the filtered t2b, as its
-- batched outer.
-- Expected: the top join cannot batch t1b without splitting the expression
-- across two joins, so it stays a plain nested loop despite its YbBatchedNL
-- hint, and all three runs return the same rows.
--

create table t1 (a int, b int, primary key (a asc));
create table t2 (a int, b int, primary key (a asc, b asc));
insert into t1 select g, g % 5 from generate_series(1, 20) g;
insert into t2
  select a, b from generate_series(1, 100) a, generate_series(1, 10) b;
analyze t1, t2;

set join_collapse_limit = 1;

select bnl_variants($q$
/*+
  IndexScan(t1a)
  IndexScan(t2a)
  IndexScan(t2b)
  YbBatchedNL(t2a t2b)
  YbBatchedNL(t1a t2a t2b)
  YbBatchedNL(t1a t1b t2a t2b)
*/
select t2a.a, t2a.b, t2b.b as c
from
  t1 t1b
  join (t1 t1a
        join (t2 t2b join t2 t2a on t2a.b = t2b.a) on t2a.a = t1a.a)
    on t2a.b = t1b.b + t1a.b
where t2b.b <= 3
$q$);

reset join_collapse_limit;
