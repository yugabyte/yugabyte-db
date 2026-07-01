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

-- Block 1 (baseline): force parallel paths to surface estimation errors.
-- Range-size-vs-loops policy: if a block's range count creates per-executor
-- unevenness (e.g. only 2 ranges across 4+1 loops) so the integer-rounded
-- per-loop estimate flips on rounding alone, adjust range_size for that
-- block.  Do NOT use this rule to hide legitimate model mismatches such as
-- "way too many empty ranges": empty ranges per se shouldn't accumulate
-- error if model and reality both scale linearly with range count.

-- 2026.1: use parallel_*_cost and yb_parallel_range_rows to encourage
-- parallelism instead of yb_test_force_parallel.
set parallel_setup_cost = 0;
set parallel_tuple_cost = 0;
set yb_parallel_range_rows = 3000;  -- ~4 workers for 12345-row table
set max_parallel_workers_per_gather = 4;

select * from queries order by qid;

update explain_query_options set with_analyze = false;

-- verify scan node types: first variant saves baseline; later variants assert symmetric diff is empty.
-- 2026.1: some unparallelized nodeds expected because of lack of yb_test_force_parallel enforcement.
create temp table baseline_scan_nodes as
select coalesce(q.qid, x.qid) qid, x.nid, "Node Type",
       "Index Name", "Index Cond", "Storage Index Filter",
       "Storage Filter", "Filter"
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

-- 2026.1: expected to see some unparallelized nodeds because of lack of yb_test_force_parallel.
(table baseline_scan_nodes
 except all
 select coalesce(q.qid, x.qid) qid, x.nid, "Node Type",
        "Index Name", "Index Cond", "Storage Index Filter",
        "Storage Filter", "Filter"
 from queries q full join check_roundtrip_estimates x on x.qid = q.qid)
union all
(select coalesce(q.qid, x.qid) qid, x.nid, "Node Type",
        "Index Name", "Index Cond", "Storage Index Filter",
        "Storage Filter", "Filter"
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

-- 2026.1: some unparallelized nodeds expected because of lack of yb_test_force_parallel enforcement.
(table baseline_scan_nodes
 except all
 select coalesce(q.qid, x.qid) qid, x.nid, "Node Type",
        "Index Name", "Index Cond", "Storage Index Filter",
        "Storage Filter", "Filter"
 from queries q full join check_roundtrip_estimates x on x.qid = q.qid)
union all
(select coalesce(q.qid, x.qid) qid, x.nid, "Node Type",
        "Index Name", "Index Cond", "Storage Index Filter",
        "Storage Filter", "Filter"
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
        "Storage Filter", "Filter"
 from queries q full join check_roundtrip_estimates x on x.qid = q.qid)
union all
(select coalesce(q.qid, x.qid) qid, x.nid, "Node Type",
        "Index Name", "Index Cond", "Storage Index Filter",
        "Storage Filter", "Filter"
 from queries q full join check_roundtrip_estimates x on x.qid = q.qid
 except all
 table baseline_scan_nodes);

update explain_query_options set with_analyze = true;

-- should not return any row; columns surface per-field counts on failure.
-- 2026.1: some unparallelized nodeds expected because of lack of yb_test_force_parallel enforcement.
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

drop table baseline_scan_nodes;
