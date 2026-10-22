-- Test planner's roundtrip estimates for Parallel Index/IndexOnly scans.
-- DEPENDENCY: yb.orig.roundtrip_estimate_setup
\c roundtrip_test
set client_min_messages to 'warning';

-- No index rebuild here: the shared setup defines every secondary as
-- INCLUDE (vv).  Serial projects v/* (uncovered -> Index Scan); this suite
-- projects vv (covered -> Index Only Scan).  --memstore_size_mb=1 (see
-- TestPgRegressPlannerEstimates) flushes the setup's indexes to SST so the
-- parallel range splitter (Tablet::GetTabletKeyRanges, which walks SST index
-- blocks) measures real per-range read RPCs.  yb_test_force_parallel = force
-- ignores the parallel_workers reloption.

delete from queries;

-- QID categories:
--   1xxxx: contiguous key ranges
--   2xxxx: Seq Scan + wide key range + storage/local filters
--   3xxxx: sparse IN-list key probes
--   4xxxx: clustered IN-list key probes
--   5xxxx: seeknext-style contiguous index ranges
--   6xxxx: narrow ybctid-projecting Parallel Index Scan width probes
--   9xxxx: pushed-down aggregates and their non-pushed controls

-- range queries.
insert into queries (qid, query)
with relations(rel, qidrel) as (
    values ('ce.r',  0),
           ('ce.rC', 1000)
),
ranges(qidbase, predicate) as (
    values (100, '##COL## between 1 and 10000'),
           (200, '##COL## between 1 and 11000')
),
scans(qidoff, target, predicol) as (
    values (1, '*',  'pk'),  -- Parallel Index Scan using PK index
           (2, 'vv',  'b'),   -- Parallel Index Only Scan on non-PK index
           (3, 'b, vv', 'b'), -- Parallel Index Only Scan on non-PK index
           -- request ybctid.  returning ybctid from IOS unsupported.
           (4, 'ybctid, *',  'pk'),  -- Index Scan using PK index
           (5, 'ybctid, vv',  'b')    -- Index Scan using non-PK index
)
select
    10000 + r.qidrel + rng.qidbase + s.qidoff as qid,
    'select ' || s.target || ' from ' || r.rel || ' where ' ||
    replace(rng.predicate, '##COL##', s.predicol) as query
from relations r, ranges rng, scans s;

-- filter combination queries.
insert into queries (qid, query)
with relations(rel, qidrel) as (
    values ('ce.r',  0),
           ('ce.rC', 1000)
),
filter_combos(qidbase, qidoff, target, predicol, extra) as (
    -- Parallel Seq Scan
    values (000, 1, '*', null, null),
           (000, 2, 'ybctid, *', null, null),
    -- PK Index Scan: + storage filter, + local filter
           (100, 1, '*', 'pk', 'pk % 100 <= 33'),
           (100, 2, '*', 'pk', 'pk % 100 <= 33 and non_pushable(pk) <= 12345'),
    -- non-PK Index Scan: + storage index filter, + storage table filter,
    --                    + local filter
           (200, 1, 'vv', 'b',  'b % 100 <= 33'),
           (200, 2, 'vv', 'b',  'b % 100 <= 33 and e % 100 <= 33'),
           (200, 3, 'vv', 'b',  'b % 100 <= 33 and e % 100 <= 33 and non_pushable(e) <= 12345'),
    -- Index Only Scan: + storage filter, + local filter
           (300, 1, 'b, vv', 'b',  'b % 100 <= 33'),
           (300, 2, 'b, vv', 'b',  'b % 100 <= 33 and non_pushable(b) <= 12345')
)
select
    20000 + r.qidrel + fc.qidbase + fc.qidoff as qid,
    'select ' || fc.target || ' from ' || r.rel
    || case when fc.predicol is null then '' else
        ' where ' || fc.predicol || ' between 1 and 10000'
        || ' and ' || fc.extra end as query
from relations r, filter_combos fc;

-- IN-list queries with sparse, non-contiguous first-key matches.
insert into queries (qid, query)
with relations(rel, qidrel) as (
    values ('ce.r',  0),
           ('ce.rC', 1000)
),
in_list(qidoff, target, predicol, items) as (
    values (1, '*', 'pk',
            '1, 100, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 11000, 12000, 12345'),
           (2, 'vv', 'b',
            '1, 100, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 11000, 12000, 12345'),
           (3, 'b, vv', 'b',
            '1, 100, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 11000, 12000, 12345')
)
select
    30000 + r.qidrel + il.qidoff as qid,
    'select ' || il.target || ' from ' || r.rel
    || ' where ' || il.predicol || ' in (' || il.items || ')' as query
from relations r, in_list il;

-- IN-list queries with tightly clustered first-key matches (single scan page).
insert into queries (qid, query)
with relations(rel, qidrel) as (
    values ('ce.r',  0),
           ('ce.rC', 1000)
),
in_list(qidoff, target, predicol, items) as (
    values (1, '*', 'pk', '1, 2, 3, 4, 5, 6, 7, 8'),
           (2, 'vv', 'b',  '1, 2, 3, 4, 5, 6, 7, 8'),
           (3, 'b, vv', 'b', '1, 2, 3, 4, 5, 6, 7, 8')
)
select
    40000 + r.qidrel + il.qidoff as qid,
    'select ' || il.target || ' from ' || r.rel
    || ' where ' || il.predicol || ' in (' || il.items || ')' as query
from relations r, in_list il;

-- seeknext-style queries.
insert into queries (qid, query)
with colocations(colo, qidcolo) as (
    values ('',  0),
           ('C', 5000)
),
columns(col_id, col, forward_pred, backward_pred) as (
    values (1, 'dv1',   '<= 1',    '<= 1 order by dv1 desc'),
           (2, 'f1000', '<= 10',   '<= 10 order by f1000 desc'),
           (3, 'f100',  '<= 100',  '<= 100 order by f100 desc')
),
scans(qidbase, is_only_scan, is_backward) as (
    values (1000, false, false),
           (2000, true,  false),
           (3000, false, true),
           (4000, true,  true)
)
select
    50000 + s.qidbase + c_colo.qidcolo
        + row_number() over (partition by c_colo.colo, s.qidbase order by c.col_id) as qid,
    'select ' ||
    case when s.is_only_scan then c.col || ', vv' else 'vv, ' || c.col end ||
    ' from sn.r' || c_colo.colo ||
    ' where ' || c.col || ' ' ||
    case when s.is_backward then c.backward_pred else c.forward_pred end as query
from colocations c_colo,
     scans s,
     columns c;

-- Narrow ybctid-projecting Parallel Index Scan on the PK.
insert into queries values
    (60001, $$select ybctid, a from ce.r where pk <= 1000$$),
    (61001, $$select ybctid, a from ce.rC where pk <= 1000$$);

-- Pushed-down aggregates (#20955): a parallel scan fetches whole parallel
-- ranges, so DocDB returns one partial-aggregate row per range an executor
-- takes; the per-loop roundtrip estimate is the per-executor range count
-- with or without the pushdown, so these rows pin the pushdown decision
-- and est == act.  A non-colocated secondary Index Scan keeps one table
-- request per ybctid batch.  d and e have no index, so min/max over them
-- stay a plain Agg (an indexed column would become a MinMaxAgg InitPlan)
-- and sum/min/max/avg over them need the base table, which turns the
-- b-range scans into Index Scans; count(*) over the b range stays an Index
-- Only Scan unless its filter needs e.
insert into queries (qid, query)
with relations(rel, qidrel) as (
    values ('ce.r',  0),
           ('ce.rC', 1000)
),
aggs(qidagg, agg) as (
    values (000, 'count(*)'),
           (100, 'sum(d)'),
           (200, 'min(d), max(e)'),
           (300, 'avg(d)')
),
scans(qidoff, predicate) as (
    -- Parallel Seq Scan + storage filter (d has no index)
    values (1, 'd between 1 and 10000'),
    -- Parallel PK Index Scan, + storage filter
           (2, 'pk between 1 and 10000'),
           (3, 'pk between 1 and 10000 and pk % 100 <= 33'),
    -- non-PK index: Parallel Index Only Scan for count(*) on 4 and 5,
    -- Parallel Index Scan otherwise (sum/min/max/avg read d and e from
    -- the table, and 6 filters on e), + storage index filter, + storage
    -- table filter
           (4, 'b between 1 and 10000'),
           (5, 'b between 1 and 10000 and b % 100 <= 33'),
           (6, 'b between 1 and 10000 and e % 100 <= 33')
)
select
    90000 + r.qidrel + a.qidagg + s.qidoff as qid,
    'select ' || a.agg || ' from ' || r.rel || ' where ' ||
    s.predicate as query
from relations r, aggs a, scans s;

-- Not pushed down (#20955): a local filter and an internal transition type
-- (sum over bigint) keep the per-executor range estimate.  The aggregate
-- over a join is a serial-suite control only: a Parallel Hash's inner scan
-- runs on the participants that reach it before the build completes, so its
-- loop count is not the Gather's.
insert into queries values
    (99001, $$select count(*) from ce.r
        where pk between 1 and 10000 and non_pushable(pk) <= 12345$$),
    (99002, $$select sum(d) from ce.r
        where b between 1 and 10000 and non_pushable(b) <= 12345$$),
    (99003, $$select count(*) from ce.r
        where b between 1 and 10000 and non_pushable(b) <= 12345$$),
    (99005, $$select sum(d::bigint), count(v) from ce.r
        where pk between 1 and 10000$$),
    (99011, $$select count(*) from ce.rC
        where pk between 1 and 10000 and non_pushable(pk) <= 12345$$),
    (99012, $$select sum(d) from ce.rC
        where b between 1 and 10000 and non_pushable(b) <= 12345$$);

-- Block 1 (baseline): force parallel paths to surface estimation errors.
-- Range-size-vs-loops policy: if a block's range count creates per-executor
-- unevenness (e.g. only 2 ranges across 4+1 loops) so the integer-rounded
-- per-loop estimate flips on rounding alone, adjust range_size for that
-- block.  Do NOT use this rule to hide legitimate model mismatches such as
-- "way too many empty ranges": empty ranges per se shouldn't accumulate
-- error if model and reality both scale linearly with range count.
set yb_test_force_parallel = force;
set max_parallel_workers_per_gather = 4;

select * from queries order by qid;

update explain_query_options set with_analyze = false;

-- verify scan node types: first variant saves baseline; later variants assert symmetric diff is empty.
create temp table baseline_scan_nodes as
select coalesce(q.qid, x.qid) qid, x.nid, "Node Type",
       "Index Name", "Index Cond", "Storage Index Filter",
       "Storage Filter", "Filter", "Partial Aggregate"
from queries q full join check_roundtrip_estimates x on x.qid = q.qid;
select * from baseline_scan_nodes order by qid, nid;

update explain_query_options set with_analyze = true;

-- should not return any row; columns surface per-field counts on failure.
select qid, nid, "Node Type", "Index Name", scan_kind, is_colocated,
       "Estimated Table Roundtrips" est_t, "Estimated Index Roundtrips" est_i,
       "Storage Table Read Requests" act_t, "Storage Index Read Requests" act_i,
       "Estimated Roundtrips" est, "Actual Roundtrips" act,
       "Planned Loops" plan_loops, "Actual Loops" act_loops,
       "Estimated Total Roundtrips" est_total, "Actual Total Roundtrips" act_total,
       "Total Estimates", "Field Layout"
from check_roundtrip_estimates
where "Total Estimates" <> 'passed' or "Field Layout" <> 'passed'
order by qid, nid;

-- Block 1b: pin yb_parallel_range_size to the pre-bump 1MB default.  Block 1
-- runs at the current (~16MB) default where the swept ranges collapse to a
-- single parallel partition (degenerate parallelism); this block exercises
-- the smaller-range regime where each executor sweeps >= 1 partition, so the
-- per-loop roundtrip counters are non-zero and est/act are compared directly.
set yb_parallel_range_size = 1048576;

update explain_query_options set with_analyze = false;

(table baseline_scan_nodes
 except all
 select coalesce(q.qid, x.qid) qid, x.nid, "Node Type",
        "Index Name", "Index Cond", "Storage Index Filter",
        "Storage Filter", "Filter", "Partial Aggregate"
 from queries q full join check_roundtrip_estimates x on x.qid = q.qid)
union all
(select coalesce(q.qid, x.qid) qid, x.nid, "Node Type",
        "Index Name", "Index Cond", "Storage Index Filter",
        "Storage Filter", "Filter", "Partial Aggregate"
 from queries q full join check_roundtrip_estimates x on x.qid = q.qid
 except all
 table baseline_scan_nodes);

update explain_query_options set with_analyze = true;

-- should not return any row; columns surface per-field counts on failure.
select qid, nid, "Node Type", "Index Name", scan_kind, is_colocated,
       "Estimated Table Roundtrips" est_t, "Estimated Index Roundtrips" est_i,
       "Storage Table Read Requests" act_t, "Storage Index Read Requests" act_i,
       "Estimated Roundtrips" est, "Actual Roundtrips" act,
       "Planned Loops" plan_loops, "Actual Loops" act_loops,
       "Estimated Total Roundtrips" est_total, "Actual Total Roundtrips" act_total,
       "Total Estimates", "Field Layout"
from check_roundtrip_estimates
where "Total Estimates" <> 'passed' or "Field Layout" <> 'passed'
order by qid, nid;

-- Block 2: halve yb_parallel_range_size to roughly double planned partitions
-- (~5 -> ~10).  Keeps partition count comfortably >= planned loops while
-- avoiding the under-population that 1/8x produced for sparse IN-list probes.
set yb_parallel_range_size = 524288;

update explain_query_options set with_analyze = false;

(table baseline_scan_nodes
 except all
 select coalesce(q.qid, x.qid) qid, x.nid, "Node Type",
        "Index Name", "Index Cond", "Storage Index Filter",
        "Storage Filter", "Filter", "Partial Aggregate"
 from queries q full join check_roundtrip_estimates x on x.qid = q.qid)
union all
(select coalesce(q.qid, x.qid) qid, x.nid, "Node Type",
        "Index Name", "Index Cond", "Storage Index Filter",
        "Storage Filter", "Filter", "Partial Aggregate"
 from queries q full join check_roundtrip_estimates x on x.qid = q.qid
 except all
 table baseline_scan_nodes);

update explain_query_options set with_analyze = true;

-- should not return any row; columns surface per-field counts on failure.
select qid, nid, "Node Type", "Index Name", scan_kind, is_colocated,
       "Estimated Table Roundtrips" est_t, "Estimated Index Roundtrips" est_i,
       "Storage Table Read Requests" act_t, "Storage Index Read Requests" act_i,
       "Estimated Roundtrips" est, "Actual Roundtrips" act,
       "Planned Loops" plan_loops, "Actual Loops" act_loops,
       "Estimated Total Roundtrips" est_total, "Actual Total Roundtrips" act_total,
       "Total Estimates", "Field Layout"
from check_roundtrip_estimates
where "Total Estimates" <> 'passed' or "Field Layout" <> 'passed'
order by qid, nid;

-- Block 3: leader-off isolates parallel_leader_participation from range-size.
-- Reset range_size to baseline so any new diffs come from leader-off alone.
reset yb_parallel_range_size;
set parallel_leader_participation = off;

update explain_query_options set with_analyze = false;

(table baseline_scan_nodes
 except all
 select coalesce(q.qid, x.qid) qid, x.nid, "Node Type",
        "Index Name", "Index Cond", "Storage Index Filter",
        "Storage Filter", "Filter", "Partial Aggregate"
 from queries q full join check_roundtrip_estimates x on x.qid = q.qid)
union all
(select coalesce(q.qid, x.qid) qid, x.nid, "Node Type",
        "Index Name", "Index Cond", "Storage Index Filter",
        "Storage Filter", "Filter", "Partial Aggregate"
 from queries q full join check_roundtrip_estimates x on x.qid = q.qid
 except all
 table baseline_scan_nodes);

update explain_query_options set with_analyze = true;

-- should not return any row; columns surface per-field counts on failure.
select qid, nid, "Node Type", "Index Name", scan_kind, is_colocated,
       "Estimated Table Roundtrips" est_t, "Estimated Index Roundtrips" est_i,
       "Storage Table Read Requests" act_t, "Storage Index Read Requests" act_i,
       "Estimated Roundtrips" est, "Actual Roundtrips" act,
       "Planned Loops" plan_loops, "Actual Loops" act_loops,
       "Estimated Total Roundtrips" est_total, "Actual Total Roundtrips" act_total,
       "Total Estimates", "Field Layout"
from check_roundtrip_estimates
where "Total Estimates" <> 'passed' or "Field Layout" <> 'passed'
order by qid, nid;

reset parallel_leader_participation;

-- yb_enable_index_aggregate_pushdown = off (#20955): a Parallel Index Scan
-- (not a Parallel Index Only Scan) keeps its aggregate on the PG side;
-- Parallel Seq Scans and Parallel Index Only Scans stay pushed down.  A
-- parallel scan's request estimate does not depend on the pushdown (the
-- per-executor range count, or the ybctid batches of a non-colocated
-- secondary Index Scan), so this block pins the decision and est == act
-- under the GUC.  The 1MB range size keeps the per-loop counters non-zero,
-- as in Block 1b.
set yb_parallel_range_size = 1048576;
set yb_enable_index_aggregate_pushdown = off;

update explain_query_options set with_analyze = false;

-- the scans still pushed down: no Parallel Index Scan among them.
select qid, nid, "Node Type", "Index Name", "Partial Aggregate"
from check_roundtrip_estimates
where qid between 90000 and 99999 and "Partial Aggregate"
order by qid, nid;

update explain_query_options set with_analyze = true;

-- should not return any row; columns surface per-field counts on failure.
select qid, nid, "Node Type", "Index Name", scan_kind, is_colocated,
       "Estimated Table Roundtrips" est_t, "Estimated Index Roundtrips" est_i,
       "Storage Table Read Requests" act_t,
       "Storage Index Read Requests" act_i,
       "Estimated Roundtrips" est, "Actual Roundtrips" act,
       "Planned Loops" plan_loops, "Actual Loops" act_loops,
       "Estimated Total Roundtrips" est_total,
       "Actual Total Roundtrips" act_total,
       "Total Estimates", "Field Layout"
from check_roundtrip_estimates
where qid between 90000 and 99999
  and ("Total Estimates" <> 'passed' or "Field Layout" <> 'passed')
order by qid, nid;

reset yb_enable_index_aggregate_pushdown;
reset yb_parallel_range_size;

drop table baseline_scan_nodes;
