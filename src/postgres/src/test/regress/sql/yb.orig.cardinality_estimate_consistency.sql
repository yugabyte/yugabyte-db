-- Test cardinality estimate consistency
-- DEPENDENCY: yb.orig.cardinality_estimate_setup

set client_min_messages to warning;

analyze r;
analyze s;
analyze rd;

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

    (DEFAULT, $$select distinct a from r where a <= 500 and a % 100 <= 33 and non_pushable(a) <= 500$$),

    -- k = 5 implies s = 0 (rd_k_s).  The Index Scan and the Bitmap Index Scan
    -- should both estimate the rows of the condition on k alone.
    (DEFAULT, $$/*+ IndexScan(rd) */
        select pk from rd where k = 5 and s = 0$$),

    (DEFAULT, $$/*+ BitmapScan(rd) */
        select pk from rd where k = 5 and s = 0$$)
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


drop view index_scan_estimates;
drop table serial_scan_estimates;
drop table parallel_scan_estimates;
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

--
-- extended statistics in Bitmap Table Scan.
-- k = 5 implies s = 0 and t = 0 (rd_k_s, rd_k_t), so the scans using i_rd_k
-- should have the Estimated Nexts and the Estimated Table Roundtrips of
-- k = 5 alone, with a Filter as well.  With the partial index i_rd_k_part,
-- the Estimated Nexts should be the rows of the Bitmap Index Scan.
--
drop function if exists explain_analyze_debug_json cascade;
drop function if exists non_pushable cascade;

create function explain_analyze_debug_json(query_sql text)
returns table (explain_line json)
language plpgsql as
$$
begin
  return query execute
    'EXPLAIN (ANALYZE, DIST, DEBUG, FORMAT json) ' || query_sql;
end;
$$;

create function non_pushable(v int)
returns int
language plpgsql immutable as
$$
begin
    return v;
end;
$$;

-- EXPLAIN prints the estimates only with DEBUG.
set yb_explain_hide_non_deterministic_fields = off;

-- With these limits the Estimated Table Roundtrips follow the rows that the
-- scan returns, not only the rows that it fetches.
set yb_fetch_row_limit = 0;
set yb_fetch_size_limit = '1kB';

select
  q.cond,
  bts->'Plans'->0->'Index Name' bmis_index,
  bts->'Plans'->0->'Storage Index Filter' bmis_filter,
  bts->'Storage Filter' bmts_filter,
  bts->'Filter' bmts_local_filter,
  bts->'Plans'->0->'Plan Rows' bmis_rows,
  bts->'Estimated Nexts' est_nexts,
  bts->'Estimated Table Roundtrips' est_table_roundtrips
from
  (values
    (1, 'i_rd_k', 'k = 5'),
    (2, 'i_rd_k', 'k = 5 and s = 0'),
    (3, 'i_rd_k', 'k = 5 and t = 0'),
    (4, 'i_rd_k', 'k = 5 and t = 0 and non_pushable(pk) > 0'),
    (5, 'i_rd_k_part', 'k = 5 and pk <= 6000')) q(n, idx, cond),
  lateral explain_analyze_debug_json(
    '/*+ BitmapScan(rd ' || q.idx || ') */ select pk from rd where ' || q.cond) js,
  lateral to_json(js.explain_line->0->'Plan') bts
order by q.n;

reset yb_fetch_size_limit;
reset yb_fetch_row_limit;
reset yb_explain_hide_non_deterministic_fields;

drop function explain_analyze_debug_json;
drop function non_pushable;
