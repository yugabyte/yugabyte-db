-- Test the planner's estimate of the catalog reads a parallel worker makes
-- for the relations in its plan against the reads that EXPLAIN reports.
-- DEPENDENCY: yb.orig.roundtrip_estimate_setup
\c roundtrip_test
set client_min_messages to 'warning';

-- A parallel worker starts with empty catalog caches and builds a relcache
-- entry for every table and index its plan opens.  A Gather's startup cost
-- has yb_parallel_setup_cost_per_relation for each of them, which at the
-- default is three catalog reads at yb_local_roundtrip_cost (#36424).  This
-- test reads both sides for the same query:
--   estimated: the Gather's startup cost with the setting at its default less
--     the cost with the setting at 0, in relations;
--   actual: Catalog Read Requests of EXPLAIN (ANALYZE, DIST) for each worker
--     launched.  The leader's caches are warm, so the workers make all of
--     the reads.  The smallest of three runs is taken, because the leader
--     refreshes its own caches once after the DDL below.
-- A worker also makes catalog reads that do not depend on the relations in its
-- plan, and the planner charges those in parallel_setup_cost.  The queries
-- therefore come in groups that differ only in the number of partitions left
-- after pruning, and a group passes when each relation it adds to the plan
-- adds three reads.

create schema pl;

-- 25 partitions of 100 rows.  A condition on k leaves the first N of them
-- after plan-time pruning.
create table pl.p (k int, v int, vv text, primary key (k asc))
    partition by range (k);
select format('create table pl.p%s partition of pl.p for values from (%s) to (%s)',
              i, (i - 1) * 100 + 1, i * 100 + 1)
from generate_series(1, 25) i \gexec
insert into pl.p select i, i, repeat('x', 16) from generate_series(1, 2500) i;
analyze pl.p;

-- The same without a primary key, so that a scan can only use the secondary
-- index.
create table pl.q (k int, v int, vv text) partition by range (k);
select format('create table pl.q%s partition of pl.q for values from (%s) to (%s)',
              i, (i - 1) * 100 + 1, i * 100 + 1)
from generate_series(1, 25) i \gexec
create index nonconcurrently q_v_idx on pl.q (v asc);
insert into pl.q select * from pl.p;
analyze pl.q;

create table pl.t (k int, v int, primary key (k asc));
insert into pl.t select i, i from generate_series(1, 2500) i;
analyze pl.t;

-- Tables of other kinds: five partitions of 100 rows each, which differ from
-- a plain table in one property.
create type pl.color as enum ('red', 'green');
create domain pl.positive as int check (value > 0);
create function pl.noop() returns trigger language plpgsql as
$$ begin return new; end; $$;

create table pl.kinds (kid int primary key, kind text, columns text);
insert into pl.kinds values
    (1, 'plain', 'k int, v int, primary key (k asc)'),
    (2, 'not_null', 'k int, v int not null, primary key (k asc)'),
    (3, 'default', 'k int, v int default 7, primary key (k asc)'),
    (4, 'check', 'k int, v int check (v > 0), primary key (k asc)'),
    (5, 'generated', 'k int, v int, g int generated always as (v * 2) stored, primary key (k asc)'),
    (6, 'enum', 'k int, v int, c pl.color, primary key (k asc)'),
    (7, 'domain', 'k int, v pl.positive, primary key (k asc)'),
    (8, 'fkey', 'k int references pl.t, v int, primary key (k asc)'),
    (9, 'trigger', 'k int, v int, primary key (k asc)'),
    (10, 'policy', 'k int, v int, primary key (k asc)'),
    (11, 'wide', 'k int, v int, ' ||
         (select string_agg(format('c%s int', i), ', ')
          from generate_series(1, 40) i) || ', primary key (k asc)'),
    -- no primary key, so that a scan can only use the secondary index
    (12, 'expr_index', 'k int, v int'),
    (13, 'partial_index', 'k int, v int');

select format('create table pl.k_%s (%s) partition by range (k)',
              kind, columns)
from pl.kinds order by kid \gexec
select format('create table pl.k_%s_%s partition of pl.k_%s for values from (%s) to (%s)',
              kind, i, kind, (i - 1) * 100 + 1, i * 100 + 1)
from pl.kinds, generate_series(1, 5) i order by kid, i \gexec
create trigger k_trigger_noop before insert on pl.k_trigger
    for each row execute function pl.noop();
select format('alter table pl.k_policy_%s enable row level security', i)
from generate_series(1, 5) i \gexec
select format('create policy k_policy on pl.k_policy_%s using (v > 0)', i)
from generate_series(1, 5) i \gexec
create index nonconcurrently k_expr_index_idx on pl.k_expr_index ((v + 1) asc);
create index nonconcurrently k_partial_index_idx on pl.k_partial_index (v asc)
    where v > 0;
select format('insert into pl.k_%s (k, v) select i, i from generate_series(1, 500) i',
              kind)
from pl.kinds order by kid \gexec
select format('analyze pl.k_%s', kind) from pl.kinds order by kid \gexec

create function launch_estimates(qid_from int, qid_to int)
returns table (qid int, relations numeric, act_reads numeric)
language plpgsql as
$$
declare
  gather_cost constant jsonpath :=
    '$.**?(@."Node Type" == "Gather" || @."Node Type" == "Gather Merge")."Startup Cost"';
  per_relation constant text :=
    current_setting('yb_parallel_setup_cost_per_relation');
  q record;
  plan jsonb;
  charge numeric;
  reads numeric;
begin
  for q in select * from queries qs
           where qs.qid between qid_from and qid_to order by qs.qid
  loop
    qid := q.qid;

    execute 'explain (format json) ' || q.query into plan;
    charge := jsonb_path_query_first(plan, gather_cost);
    perform set_config('yb_parallel_setup_cost_per_relation', '0', true);
    execute 'explain (format json) ' || q.query into plan;
    charge := charge - jsonb_path_query_first(plan, gather_cost)::numeric;
    perform set_config('yb_parallel_setup_cost_per_relation', per_relation,
                       true);
    relations := round(charge / per_relation::numeric, 2);

    act_reads := null;
    for i in 1..3 loop
      execute 'explain (analyze, dist, timing off, format json) ' || q.query
        into plan;
      reads := round((plan->0->>'Catalog Read Requests')::numeric /
                     jsonb_path_query_first(plan, '$.**."Workers Launched"')::numeric,
                     2);
      act_reads := least(act_reads, reads);
    end loop;

    return next;
  end loop;
end;
$$;

-- The relations and the reads that each query of a group adds to those of the
-- group's first query.
create function check_launch_estimates(qid_from int, qid_to int)
returns table (qid int, relations numeric, added_relations numeric,
               added_reads numeric, verdict text)
language sql as
$$
  with e as (select * from launch_estimates(qid_from, qid_to)),
       first as (select * from e order by qid limit 1)
  select e.qid, e.relations,
         e.relations - first.relations,
         e.act_reads - first.act_reads,
         case when e.act_reads - first.act_reads =
                   3 * (e.relations - first.relations)
              then 'passed' else 'failed' end
  from e, first
  order by e.qid;
$$;

-- The same for groups of ten qids, one row for each group: the reads for each
-- relation the group's queries add, taken between its first and last query,
-- and whether the queries between them are on that line.
create function check_launch_estimates_by_group(qid_from int, qid_to int)
returns table (qid_group int, query text, reads_per_relation numeric,
               linear bool, verdict text)
language sql as
$$
  with e as (select l.*, l.qid / 10 as grp
             from launch_estimates(qid_from, qid_to) l),
       ends as (select grp, min(qid) as first_qid, max(qid) as last_qid
                from e group by grp),
       slopes as (select ends.grp, f.qid, f.relations, f.act_reads,
                         (l.act_reads - f.act_reads) /
                         nullif(l.relations - f.relations, 0) as slope
                  from ends
                       join e f on f.qid = ends.first_qid
                       join e l on l.qid = ends.last_qid)
  select s.grp, (select q.query from queries q where q.qid = s.qid),
         round(s.slope, 2),
         bool_and(e.act_reads - s.act_reads =
                  s.slope * (e.relations - s.relations)),
         case when s.slope = 3 then 'passed' else 'failed' end
  from slopes s join e on e.grp = s.grp
  group by s.grp, s.qid, s.slope
  order by s.grp;
$$;

delete from queries;

-- QID categories:
--   1xx: Seq Scan of each partition
--   2xx: Index Scan of each partition by its primary key, sorted under a
--        Gather Merge
--   3xx: Index Scan of each partition by a secondary index
--   4xx: Index Only Scan of each partition by its primary key
--   5xx: the partitions joined to a table
--   6xx: the partitions pruned at executor startup, not at plan time: now()
--        is stable, so the planner cannot evaluate the condition
--   1xxx: Seq Scan of each partition of a table of another kind
--   2xxx: Index Scan by an index of another kind
insert into queries (qid, query)
with shapes(qidbase, query) as (
    values (100, 'select * from pl.p where k <= ##K##'),
           (200, 'select * from pl.p where k <= ##K## order by v'),
           (300, 'select * from pl.q where v > 0 and k <= ##K##'),
           (400, 'select k from pl.p where k <= ##K##'),
           (500, 'select * from pl.p p, pl.t t where p.k = t.k and p.k <= ##K##'),
           (600, 'select * from pl.p where k <= ##K## + extract(year from now())::int * 0')
),
partitions(n) as (
    values (1), (2), (5), (10), (25)
)
select s.qidbase + p.n as qid,
       replace(s.query, '##K##', (p.n * 100)::text) as query
from shapes s, partitions p;

insert into queries (qid, query)
select 1000 + kid * 10 + n,
       format('select * from pl.k_%s where k <= %s', kind, n * 100)
from pl.kinds, (values (1), (2), (5)) as partitions(n)
where kind not in ('expr_index', 'partial_index');

insert into queries (qid, query)
select 2000 + kid * 10 + n,
       format('select * from pl.k_%s where %s and k <= %s', kind,
              case kind when 'expr_index' then 'v + 1 > 0' else 'v > 0' end,
              n * 100)
from pl.kinds, (values (1), (2), (5)) as partitions(n)
where kind in ('expr_index', 'partial_index');

select * from queries order by qid;

set yb_test_force_parallel = force;
set yb_enable_parallel_append = on;
set max_parallel_workers_per_gather = 2;

-- Seq Scans.  should return only 'passed' rows.
set enable_indexscan = off;
set enable_indexonlyscan = off;
set enable_bitmapscan = off;
select * from check_launch_estimates(100, 199);

-- Seq Scans of tables of other kinds.  should return only the rows explained
-- below as 'failed'.
-- A worker makes four reads, one more than the charge counts, for a table
-- with a column default, a check constraint, a generated column, a foreign
-- key, a trigger or a row security policy.
select * from check_launch_estimates_by_group(1000, 1999);
reset enable_indexscan;
reset enable_indexonlyscan;
reset enable_bitmapscan;

-- Index Scans by the primary key.  should return only 'passed' rows.
set enable_seqscan = off;
set enable_bitmapscan = off;
select * from check_launch_estimates(200, 299);

-- Index Scans by a secondary index.  should return only 'passed' rows.
select * from check_launch_estimates(300, 399);

-- Index Only Scans.  should return only 'passed' rows.
select * from check_launch_estimates(400, 499);

-- Index Scans by indexes of other kinds.  should return only 'passed' rows.
select * from check_launch_estimates_by_group(2000, 2999);
reset enable_seqscan;
reset enable_bitmapscan;

-- A join under the Gather.  should return only 'passed' rows.
select * from check_launch_estimates(500, 599);

-- Partitions pruned at executor startup.  should return only the rows
-- explained below as 'failed'.
-- The charge counts every partition in the plan, 25 tables and their primary
-- keys, whatever the condition leaves.  A worker reads the catalog only for
-- the partitions that are left after the pruning, five reads for each.
select * from check_launch_estimates(600, 699);
select format('explain (costs off) %s', query) from queries where qid = 605 \gexec

reset yb_test_force_parallel;
reset yb_enable_parallel_append;
reset max_parallel_workers_per_gather;

drop schema pl cascade;
