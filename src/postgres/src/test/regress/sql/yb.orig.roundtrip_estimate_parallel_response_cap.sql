-- Test planner's roundtrip estimates for parallel scans whose ranges take
-- several responses each.
-- DEPENDENCY: yb.orig.roundtrip_estimate_setup
\c roundtrip_test
set client_min_messages to 'warning';

-- A parallel range is fetched without the session's fetch limits, so its
-- response ends only at the RPC response size cap.  This file runs with the
-- cap lowered to about 65kB (TestPgRegressPlannerEstimatesResponseSizeCap),
-- which a range of these tables exceeds several times over.
-- yb.orig.roundtrip_estimate_parallel covers the ranges that answer in one
-- response.

delete from queries;

-- QID categories:
--   1xxxx: Parallel Seq Scan, + storage filter
--   2xxxx: Parallel Index Only Scan, + storage filter
--   3xxxx: Parallel Index Scan using PK index, + storage filter
--   4xxxx: Parallel Index Scan using non-PK index, + storage index filter
--          and storage table filter
insert into queries (qid, query)
with relations(rel, qidrel) as (
    values ('ce.r',  0),
           ('ce.rC', 1000)
),
scans(qid, target, predicate) as (
    values (10001, 'v, vv', null),
           (10002, 'v, vv', 'b between 1 and 11000'),
           (10003, 'v, vv', 'e <= 600'),
           (20001, 'b, vv', 'b between 1 and 10000'),
           (20002, 'b, vv', 'b between 1 and 10000 and b % 100 <= 33'),
           (30001, 'pk, v, vv', 'pk between 1 and 10000'),
           (30002, 'pk, v, vv', 'pk between 1 and 10000 and pk % 100 <= 33'),
           (40001, 'b, v', 'b between 1 and 3000'),
           (40002, 'vv', 'b between 1 and 10000 and b % 100 <= 33 and e % 100 <= 33')
)
select
    s.qid + r.qidrel as qid,
    'select ' || s.target || ' from ' || r.rel
    || coalesce(' where ' || s.predicate, '') as query
from relations r, scans s;

-- Block 1: the default yb_parallel_range_size, at which a table is one or two
-- ranges.
set yb_test_force_parallel = force;
set max_parallel_workers_per_gather = 4;

select * from queries order by qid;

update explain_query_options set with_analyze = false;

-- verify scan node types: first variant saves baseline; later variants assert symmetric diff is empty.
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

-- Block 2: 4MB ranges, about one range for each executor.
set yb_parallel_range_size = 4194304;

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

drop table baseline_scan_nodes;
