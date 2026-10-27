-- Test cardinality estimate consistency
-- DEPENDENCY: yb.orig.cardinality_estimate_setup

set client_min_messages to warning;

analyze r;
analyze s;

set yb_enable_cbo = on;
set yb_enable_bitmapscan = on;
set enable_bitmapscan = on;

--
-- Index Scan node row count estimates
--
set enable_seqscan = off;

-- Keep the baseline serial: yb_enable_cbo = on dynamically raises
-- yb_parallel_range_rows.
set yb_parallel_range_rows = 0;

drop function if exists non_pushable cascade;

-- parallel safe: plpgsql functions are parallel unsafe by default, which
-- would keep the queries using it from having parallel plans.
create function non_pushable(v bigint)
returns bigint
language plpgsql immutable parallel safe as
$$
begin
  return v + 1;
end;
$$;

drop view if exists index_scan_estimates cascade;

drop table if exists queries;

create table queries (qid serial, query text, primary key (qid ASC));

insert into queries values
    (DEFAULT, $$select pk from r where pk <= 500$$),

    (DEFAULT, $$select pk from r where pk <= 500 and pk % 100 <= 33$$),

    (DEFAULT, $$select pk from r where pk <= 500 and pk % 100 <= 33 and non_pushable(pk) <= 500$$),

    (DEFAULT, $$select pk from r where b <= 500$$),

    (DEFAULT, $$select pk from r where b <= 500 and b % 100 <= 33$$),

    (DEFAULT, $$select pk from r where b <= 500 and b % 100 <= 33 and non_pushable(b) <= 500$$),

    (DEFAULT, $$select b from r where b <= 500$$),

    (DEFAULT, $$select b from r where b <= 500 and b % 100 <= 33$$),

    (DEFAULT, $$select b from r where b <= 500 and b % 100 <= 33 and non_pushable(b) <= 500$$),

    (DEFAULT, $$select pk from r where pk <= 500 or b <= 500$$),

    (DEFAULT, $$select pk from r where pk <= 500 and pk % 100 <= 33 or b <= 500 and b % 100 <= 33$$),

    (DEFAULT, $$select pk from r where pk <= 500 and pk % 100 <= 33 and non_pushable(pk) <= 50
         or b <= 500 and b % 100 <= 33 and non_pushable(b) <= 500$$),

    (DEFAULT, $$/*+ BitmapScan(r) */
        select pk from r where pk <= 500$$),

    (DEFAULT, $$/*+ BitmapScan(r) */
        select pk from r where pk <= 500 and pk % 100 <= 33$$),

    (DEFAULT, $$/*+ BitmapScan(r) */
        select pk from r where pk <= 500 and pk % 100 <= 33 and non_pushable(pk) <= 500$$),

    (DEFAULT, $$/*+ BitmapScan(r) */
        select pk from r where b <= 500$$),

    (DEFAULT, $$/*+ BitmapScan(r) */
        select pk from r where b <= 500 and b % 100 <= 33$$),

    (DEFAULT, $$/*+ BitmapScan(r) */
        select pk from r where b <= 500 and b % 100 <= 33 and non_pushable(b) <= 500$$),

    (DEFAULT, $$/*+ BitmapScan(r) */
        select b from r where b <= 500$$),

    (DEFAULT, $$/*+ BitmapScan(r) */
        select b from r where b <= 500 and b % 100 <= 33$$),

    (DEFAULT, $$/*+ BitmapScan(r) */
        select b from r where b <= 500 and b % 100 <= 33 and non_pushable(b) <= 500$$),

    -- a has 10 rows per value (i_r_a).  A distinct index scan returns one row
    -- per group, so at every stage its estimate should be one tenth of the
    -- plain scan's over the same predicates: the index condition on groups,
    -- the storage filter on the prefix column passing or failing whole
    -- groups, the local filter on the output.
    (DEFAULT, $$select a from r where a <= 500$$),

    (DEFAULT, $$select a from r where a <= 500 and a % 100 <= 33$$),

    (DEFAULT, $$select a from r where a <= 500 and a % 100 <= 33 and non_pushable(a) <= 500$$),

    (DEFAULT, $$select distinct a from r where a <= 500$$),

    (DEFAULT, $$select distinct a from r where a <= 500 and a % 100 <= 33$$),

    (DEFAULT, $$select distinct a from r where a <= 500 and a % 100 <= 33 and non_pushable(a) <= 500$$)
;

drop type if exists index_plan_rows cascade;

create type index_plan_rows as (
    "Node Type" text,
    "Index Name" text,
    "Index Cond" text,
    "Storage Index Filter" text,
    "Storage Filter" text,
    "Filter" text,
    "Plan Rows" bigint,
    "Total Cost" float8,
    "Parallel Aware" boolean
);


select * from queries order by qid;

-- Index Scan node estimates from each query's plan under the current
-- settings.  workers = Gather's "Workers Planned", or 0 in a serial plan.
create view index_scan_estimates as
select
    qid,
    row_number() over (partition by qid) nid,
    "Node Type",
    "Index Name",
    "Index Cond",
    coalesce("Storage Index Filter", "Storage Filter") as "Index Filter",
    "Filter",
    "Plan Rows",
    "Total Cost",
    "Parallel Aware",
    coalesce(
        (jsonb_path_query_first(
            js.explain_line::jsonb,
            'strict $.** ? (@."Node Type" == "Gather" || @."Node Type" == "Gather Merge")'
        ) ->> 'Workers Planned')::int,
        0) workers
from
  queries,
  lateral explain_query_json(query) js,
  lateral jsonb_path_query(
    js.explain_line::jsonb,
    'strict $.** ? (@."Node Type" like_regex "Index.* Scan")'
  ) pln,
  lateral jsonb_populate_record(null::index_plan_rows, pln) rec;

drop table if exists serial_scan_estimates;

create table serial_scan_estimates as
select * from index_scan_estimates;

-- Repeat with parallel plans forced.  yb_test_force_parallel overrides the
-- cost-based plan choice and the scan method hints.
set yb_test_force_parallel = force;
set max_parallel_workers_per_gather = 2;

drop table if exists parallel_scan_estimates;

create table parallel_scan_estimates as
select * from index_scan_estimates;

reset max_parallel_workers_per_gather;
reset yb_test_force_parallel;
reset yb_parallel_range_rows;

-- Row count estimate consistency between the equivalent serial scans.
select
    "Index Cond",
    "Index Filter",
    "Filter",
    "Plan Rows",
    rank() over (
        partition by
            regexp_replace("Index Cond", 'pk', 'b'),
            regexp_replace("Index Filter", 'pk', 'b'),
            regexp_replace("Filter", 'pk', 'b')
        order by "Total Cost"
    ) cost_rank,
    "Node Type",
    "Index Name",
    qid,
    nid
from serial_scan_estimates
order by
    regexp_replace("Index Cond", 'pk', 'b'),
    regexp_replace("Index Filter", 'pk', 'b'),
    regexp_replace("Filter", 'pk', 'b'),
    "Plan Rows",
    cost_rank,
    "Index Name",
    "Node Type",
    qid,
    nid;

-- Parallel scan shapes; also guards the comparison below from passing
-- vacuously.
select "Node Type", "Index Name", workers, count(*) scans
from parallel_scan_estimates
where "Parallel Aware"
group by "Node Type", "Index Name", workers
order by "Node Type", "Index Name", workers;

-- Parallel scan row estimates should be the serial estimates divided by the
-- parallel divisor per get_parallel_divisor(), allowing 1 row of slack for
-- rounding.  Only the scans that kept their serial plan shape are
-- comparable.  Expect no rows.
select
    s."Index Cond",
    s."Index Filter",
    s."Filter",
    "Node Type",
    "Index Name",
    s."Plan Rows" serial_rows,
    p.workers,
    p."Plan Rows" parallel_rows,
    round(s."Plan Rows" / d.divisor) expected_rows,
    qid,
    nid
from
    serial_scan_estimates s
    join parallel_scan_estimates p
        using (qid, nid, "Node Type", "Index Name"),
    lateral (
        select case
                   when p."Parallel Aware"
                       then p.workers + greatest(0, 1 - 0.3 * p.workers)
                   else 1
               end::numeric divisor
    ) d
where abs(p."Plan Rows" - round(s."Plan Rows" / d.divisor)) > 1
order by qid, nid;

-- Pushed-down aggregates (#20955): the scan returns one partial-aggregate
-- row per read op (one per request here: each scan reads one tablet), so
-- its row estimate is the request count, not the filtered row count the
-- same scan has without the aggregate.  A local filter keeps the aggregate
-- on the PG side, and then the scan's estimate is its plain counterpart's.
-- EXPLAIN ANALYZE supplies the request counts.
drop function if exists explain_analyze_query_json;

create function explain_analyze_query_json(query_sql text)
returns table (explain_line json)
language plpgsql as
$$
begin
  return query execute
    'explain (format json, analyze, dist, summary off) ' || query_sql;
end;
$$;

-- Serial, as the baseline above: yb_enable_cbo = on dynamically raises
-- yb_parallel_range_rows.
set yb_parallel_range_rows = 0;

create temp table agg_queries (qid int, plain_qid int, query text);

-- plain_qid: the query in "queries" with the same scan and no aggregate.
insert into agg_queries values
    (101, 1, $$select count(*) from r where pk <= 500$$),

    (102, 2, $$select count(*) from r where pk <= 500 and pk % 100 <= 33$$),

    (103, 3, $$select count(*) from r
        where pk <= 500 and pk % 100 <= 33 and non_pushable(pk) <= 500$$),

    -- a is not in i_r_b: Index Scan
    (104, 4, $$select sum(a) from r where b <= 500$$),

    (105, 5, $$select sum(a) from r where b <= 500 and b % 100 <= 33$$),

    (106, 6, $$select sum(a) from r
        where b <= 500 and b % 100 <= 33 and non_pushable(b) <= 500$$),

    -- Index Only Scan
    (107, 7, $$select count(*) from r where b <= 500$$),

    (108, 8, $$select count(*) from r where b <= 500 and b % 100 <= 33$$),

    (109, 9, $$select count(*) from r
        where b <= 500 and b % 100 <= 33 and non_pushable(b) <= 500$$),

    (113, 13, $$/*+ BitmapScan(r) */
        select count(*) from r where pk <= 500$$),

    (115, 15, $$/*+ BitmapScan(r) */
        select count(*) from r
        where pk <= 500 and pk % 100 <= 33 and non_pushable(pk) <= 500$$),

    -- The Subquery Scan of a UNION ALL member that emits fewer columns than
    -- its subquery stays in the plan, so the aggregate is not pushed down
    -- and the scan keeps its plain estimate.
    (116, 1, $$select count(*) from
        ((select pk, a from r where pk <= 500)
         union all (select pk, a from r where false)) u$$)
;

-- The scan under the aggregate, and the same scan in its plain counterpart.
-- The request count is the table side when it has requests, else the index
-- side (Index Only Scan).
create temp view agg_scan_estimates as
select
    q.qid,
    pln->>'Node Type' "Node Type",
    pln->>'Index Name' "Index Name",
    coalesce((pln->>'Partial Aggregate')::bool, false) pushed,
    (pln->>'Plan Rows')::float8 plan_rows,
    (pln->>'Actual Rows')::float8 actual_rows,
    coalesce(nullif((pln->>'Storage Table Read Requests')::float8, 0),
             (pln->>'Storage Index Read Requests')::float8) requests,
    (plain->>'Plan Rows')::float8 plain_rows
from
  agg_queries q
  join queries p on p.qid = q.plain_qid,
  lateral explain_analyze_query_json(q.query) js,
  lateral jsonb_path_query(
    js.explain_line::jsonb,
    'strict $.** ? (@."Node Type" like_regex "^(Seq|Index.*|YB.*) Scan$")'
  ) pln,
  lateral explain_query_json(p.query) pjs,
  lateral jsonb_path_query(
    pjs.explain_line::jsonb,
    'strict $.** ? (@."Node Type" like_regex "^(Seq|Index.*|YB.*) Scan$")'
  ) plain;

-- Scan shapes and the pushdown decision.
select qid, "Node Type", "Index Name", pushed
from agg_scan_estimates
order by qid;

-- Pushed down: Plan Rows = Actual Rows = requests (one row per request).
-- Not pushed down: Plan Rows = the plain counterpart's.  Expect no rows;
-- a missing request count is a mismatch, not a pass.
select qid, "Node Type", pushed, plan_rows, actual_rows, requests, plain_rows
from agg_scan_estimates
where case when pushed
           then plan_rows is distinct from actual_rows
                or plan_rows is distinct from requests
           else plan_rows is distinct from plain_rows end
order by qid;

reset yb_parallel_range_rows;

drop view index_scan_estimates;
drop table serial_scan_estimates;
drop table parallel_scan_estimates;
drop view agg_scan_estimates;
drop table agg_queries;
drop function explain_analyze_query_json;
drop function non_pushable cascade;
drop type index_plan_rows cascade;
drop table queries;


--
-- parameterized filter condition in Bitmap Table Scan.
-- the selectivity should be close to DEFAULT_INEQ_SEL (0.3333333333333333).
--
select
  bts->'Node Type' bmts,
  bts->'Storage Filter' bmts_filter,
  round((bts->'Plan Rows')::text::numeric / (bts->'Plans'->0->'Plan Rows')::text::numeric, 2) sel
from
  explain_query_json($$/*+ Leading((s r)) NestLoop(s r) BitmapScan(r) Set(yb_bnl_batch_size 1) */select * from r, s where (a = x or b <= 300) and a + b >= y$$) js,
  lateral to_json(js.explain_line->0->'Plan'->'Plans'->1) bts;

explain (costs off)
/*+ Leading((s r)) NestLoop(s r) BitmapScan(r) Set(yb_bnl_batch_size 1) */select * from r, s where (a = x or b <= 300) and a + b >= y;
