-- Test planner's roundtrip estimates for Parallel Index/IndexOnly scans.
-- DEPENDENCY: yb.orig.roundtrip_estimate_parallel_setup
--
-- This test uses its own seed tables so the parallel plan choice can be
-- validated without perturbing the serial counterpart.
\c roundtrip_parallel_test
delete from queries;

-- range queries.
insert into queries (qid, query)
with relations(rel, qidrel) as (
    values ('ce.r',  10000),
           ('ce.rC', 20000)
),
ranges(qidbase, predicate) as (
    values (300, '##COL## between 1 and 10000'),
           (400, '##COL## between 1 and 11000')
),
scans(qidoff, target, predicol) as (
    values (1, '*',  'pk'),  -- Parallel Index Scan using PK index
           (2, 'v',  'b'),   -- Parallel Index Scan using non-PK index
           (3, 'b, v', 'b')  -- Parallel Index Only Scan on non-PK index
)
select
    r.qidrel + rng.qidbase + s.qidoff as qid,
    'select ' || s.target || ' from ' || r.rel || ' where ' ||
    replace(rng.predicate, '##COL##', s.predicol) as query
from relations r, ranges rng, scans s;

-- filter combination queries.
insert into queries (qid, query)
with relations(rel, qidrel) as (
    values ('ce.r',  10000),
           ('ce.rC', 20000)
),
filter_combos(qidbase, qidoff, target, predicol, extra) as (
    -- PK Index Scan: + storage filter, + local filter
    values (500, 1, '*', 'pk', 'pk % 100 <= 33'),
           (500, 2, '*', 'pk', 'pk % 100 <= 33 and non_pushable(pk) <= 12345'),
    -- non-PK Index Scan: + storage index filter, + storage table filter,
    --                    + local filter
           (600, 1, 'v', 'b',  'b % 100 <= 33'),
           (600, 2, 'v', 'b',  'b % 100 <= 33 and e % 100 <= 33'),
           (600, 3, 'v', 'b',  'b % 100 <= 33 and e % 100 <= 33 and non_pushable(e) <= 12345'),
    -- Index Only Scan: + storage filter, + local filter
           (700, 1, 'b, v', 'b',  'b % 100 <= 33'),
           (700, 2, 'b, v', 'b',  'b % 100 <= 33 and non_pushable(b) <= 12345')
)
select
    r.qidrel + fc.qidbase + fc.qidoff as qid,
    'select ' || fc.target || ' from ' || r.rel
    || ' where ' || fc.predicol || ' between 1 and 10000'
    || ' and ' || fc.extra as query
from relations r, filter_combos fc;

-- seeknext-style queries.
insert into queries (qid, query)
with colocations(colo, qidcolo) as (
    values ('',  50000),
           ('C', 60000)
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
    s.qidbase + c_colo.qidcolo
        + row_number() over (partition by c_colo.colo, s.qidbase order by c.col_id) as qid,
    'select ' ||
    case when s.is_only_scan then c.col || ', vv' else 'vv, ' || c.col end ||
    ' from sn.r' || c_colo.colo ||
    ' where ' || c.col || ' ' ||
    case when s.is_backward then c.backward_pred else c.forward_pred end as query
from colocations c_colo,
     scans s,
     columns c;

set enable_seqscan = off;
set enable_bitmapscan = off;
set yb_enable_bitmapscan = off;
set enable_sort = off;

-- Cap workers; everything else (parallel_setup_cost, parallel_tuple_cost,
-- min_parallel_*_scan_size) is left at the default so the cost model still
-- decides whether parallel wins.  min_parallel_*_scan_size is a no-op for
-- YB relations / indexes anyway.
set max_parallel_workers_per_gather = 4;

-- Keep parallel ranges small enough to split the scan, but not so small that
-- parallel transfer costs dominate.  A smaller serial fetch-size limit makes
-- serial scans more expensive while parallel scan costs still come from
-- yb_parallel_range_*.
set yb_parallel_range_size = '128kB';
reset yb_parallel_range_rows;
set yb_fetch_size_limit = '16kB';
set yb_fetch_row_limit = 0;

select * from queries order by qid;

update explain_query_options set with_analyze = false;

-- should not return any row.  mismatches indicate non-index scan nodes or a
-- scan node that did not actually execute in parallel.
select q.qid, q.query, x.qid, x.nid, "Node Type", "Index Name",
       "Actual Workers", "Went Parallel", "Index Cond"
from queries q full join check_roundtrip_estimates x on x.qid = q.qid
where x.qid is null or q.qid is null or "Actual Workers" <= 0
order by coalesce(q.qid, x.qid), x.nid;

update explain_query_options set with_analyze = true;

-- should not return any row.  surface the per-field counts on failure so
-- that both totals mismatches and field-layout mismatches are diagnosable.
-- Estimated and actual counts are both per-worker for parallel scans (see
-- yb_add_base_table_fetch_cost / yb_cost_index in costsize.c, which use
-- adjusted_index_tuples = index->tuples / parallel_divisor when computing
-- estimated_num_*_result_pages, and show_yb_rpc_stats which divides actual
-- counters by nloops), so the same comparison used for serial scans applies.
-- Fails until https://github.com/yugabyte/yugabyte-db/issues/31134 is fixed.
select qid, nid, "Node Type", "Index Name",
       "Actual Workers", "Went Parallel",
       scan_kind, is_colocated,
       "Estimated Table Roundtrips" est_t, "Estimated Index Roundtrips" est_i,
       "Storage Table Read Requests" act_t, "Storage Index Read Requests" act_i,
       "Estimated Roundtrips" est, "Actual Roundtrips" act,
       "Total Estimates", "Field Layout"
from check_roundtrip_estimates
where "Total Estimates" <> 'passed' or "Field Layout" <> 'passed'
order by qid, nid;
