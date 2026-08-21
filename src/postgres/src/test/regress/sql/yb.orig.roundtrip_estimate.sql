-- Test planner's roundtrip estimates
-- DEPENDENCY: yb.orig.roundtrip_estimate_setup
\c roundtrip_test

delete from queries;

-- QID categories:
--   1xxxx: contiguous key ranges
--   2xxxx: Seq Scan + wide key range + storage/local filters
--   3xxxx: sparse IN-list key probes
--   4xxxx: clustered IN-list key probes
--   5xxxx: seeknext-style contiguous index ranges
--   6xxxx: narrow ybctid-projecting PK Index Scan width probes
--   7xxxx: serial-only cardinality-estimate consistency cases

-- generate colocation x scan kind x index cond range combinations
insert into queries (qid, query)
with relations(rel, qidrel) as (
    values ('ce.r',  0),
           ('ce.rC', 1000)
),
ranges(qidbase, predicate) as (
    values (100, '##COL## = 1'),
           (200, '##COL## between 1 and 100'),
           (300, '##COL## between 1 and 5000'),
           (400, '##COL## between 1 and 12345')
),
scans(qidoff, target, predicol) as (
    values (1, '*',  'pk'),  -- Index Scan using PK index
           (2, 'v',  'b'),   -- Index Scan using non-PK index
           (3, 'b',  'b'),   -- Index Only Scan on non-PK index
           -- request ybctid.  returning ybctid from IOS unsupported.
           (4, 'ybctid, *',  'pk'),  -- Index Scan using PK index
           (5, 'ybctid, v',  'b')    -- Index Scan using non-PK index
)
select
    10000 + r.qidrel + rng.qidbase + s.qidoff as qid,
    'select ' || s.target || ' from ' || r.rel || ' where ' ||
    replace(rng.predicate, '##COL##', s.predicol) as query
from relations r, ranges rng, scans s;

-- generate colocation x scan kind x filter combination queries:
-- (index cond, +storage [index] filter, +storage table filter, +local filter)
insert into queries (qid, query)
with relations(rel, qidrel) as (
    values ('ce.r',  0),
           ('ce.rC', 1000)
),
filter_combos(qidbase, qidoff, target, predicol, extra) as (
    -- Seq Scan
    values (000, 1, '*', null, null),
           (000, 2, 'ybctid, *', null, null),
    -- PK Index Scan: + storage filter, + local filter
           (100, 1, '*', 'pk', 'pk % 100 <= 33'),
           (100, 2, '*', 'pk', 'pk % 100 <= 33 and non_pushable(pk) <= 5000'),
    -- non-PK Index Scan: + storage index filter, + storage table filter,
    --                    + local filter
           (200, 1, 'v', 'b',  'b % 100 <= 33'),
           (200, 2, 'v', 'b',  'b % 100 <= 33 and e % 100 <= 33'),
           (200, 3, 'v', 'b',  'b % 100 <= 33 and e % 100 <= 33 and non_pushable(e) <= 5000'),
    -- Index Only Scan: + storage filter, + local filter
           (300, 1, 'b', 'b',  'b % 100 <= 33'),
           (300, 2, 'b', 'b',  'b % 100 <= 33 and non_pushable(b) <= 5000')
)
select
    20000 + r.qidrel + fc.qidbase + fc.qidoff as qid,
    'select ' || fc.target || ' from ' || r.rel
    || case when fc.predicol is null then '' else
        ' where ' || fc.predicol || ' between 1 and 5000'
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
           (2, 'v', 'b',
            '1, 100, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 11000, 12000, 12345'),
           (3, 'b', 'b',
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
           (2, 'v', 'b',  '1, 2, 3, 4, 5, 6, 7, 8'),
           (3, 'b', 'b', '1, 2, 3, 4, 5, 6, 7, 8')
)
select
    40000 + r.qidrel + il.qidoff as qid,
    'select ' || il.target || ' from ' || r.rel
    || ' where ' || il.predicol || ' in (' || il.items || ')' as query
from relations r, in_list il;

-- queries from yb.orig.cardinality_estimate_consistency (single index scan only).
insert into queries values
    (60001, $$select ybctid, a from ce.r where pk <= 1000$$),
    (61001, $$select ybctid, a from ce.rC where pk <= 1000$$),
    (70001, $$select pk from ce.r where pk <= 500$$),
    (70002, $$select pk from ce.r where pk <= 500 and pk % 100 <= 33$$),
    (70003, $$select pk from ce.r where pk <= 500 and pk % 100 <= 33 and non_pushable(pk) <= 500$$),
    (70004, $$select pk from ce.r where b <= 500$$),
    (70005, $$select pk from ce.r where b <= 500 and b % 100 <= 33$$),
    (70006, $$select pk from ce.r where b <= 500 and b % 100 <= 33 and non_pushable(b) <= 500$$),
    (70007, $$select b from ce.r where b <= 500$$),
    (70008, $$select b from ce.r where b <= 500 and b % 100 <= 33$$),
    (70009, $$select b from ce.r where b <= 500 and b % 100 <= 33 and non_pushable(b) <= 500$$);

-- queries from yb.orig.seeknext_estimate (colocation x index/IOS x fwd/back).
insert into queries (qid, query)
with colocations(colo, qidcolo) as (
    values ('',  0),
           ('C', 5000)
),
columns(col_id, col) as (
    values (1, 'pk'), (2, 'f1'), (3, 'f2'), (4, 'f3'), (5, 'f4'), (6, 'f5'),
           (7, 'f10'), (8, 'f100'), (9, 'f1000'), (10, 'dv1'),
           (11, 'skw1'), (12, 'skw2'), (13, 'skw3')
),
scans(qidbase, is_only_scan, is_backward) as (
    values (1000, false, false), -- index scan
           (2000, true,  false), -- index only scan
           (3000, false, true),  -- backward index scan
           (4000, true,  true)   -- backward index only scan
)
select
    50000 + s.qidbase + c_colo.qidcolo
        + row_number() over (partition by c_colo.colo, s.qidbase order by c.col_id) as qid,
    'select ' ||
    case when s.is_only_scan then c.col else 'v, ' || c.col end ||
    ' from sn.r' || c_colo.colo ||
    ' where ' || c.col ||
    case when s.is_backward
         then ' <= 1 order by ' || c.col || ' desc'
         else ' = 1'
    end as query
from colocations c_colo,
     scans s,
     columns c
where not (s.is_only_scan and c.col = 'pk');

set enable_seqscan = off;
set enable_bitmapscan = off;
set yb_enable_bitmapscan = off;
set enable_sort = off;
set max_parallel_workers_per_gather = 0;

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

-- Fetch limit variant: row-count pagination
set yb_fetch_size_limit = 0;
set yb_fetch_row_limit = 128;

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

-- Fetch limit variant: byte-size pagination
set yb_fetch_size_limit = '8kB';
set yb_fetch_row_limit = 0;

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

-- Fetch limit variant: unlimited
set yb_fetch_size_limit = 0;
set yb_fetch_row_limit = 0;

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
