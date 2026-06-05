-- Test planner's roundtrip estimates
-- DEPENDENCY: yb.orig.roundtrip_estimate_setup
\c roundtrip_test

delete from queries;

-- generate colocation x scan kind x index cond range combinations
insert into queries (qid, query)
with relations(rel, qidrel) as (
    values ('ce.r',  10000),
           ('ce.rC', 20000)
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
           (3, 'b',  'b')    -- Index Only Scan on non-PK index
)
select
    r.qidrel + rng.qidbase + s.qidoff as qid,
    'select ' || s.target || ' from ' || r.rel || ' where ' ||
    replace(rng.predicate, '##COL##', s.predicol) as query
from relations r, ranges rng, scans s;

-- generate colocation x scan kind x filter combination queries:
-- (index cond, +storage [index] filter, +storage table filter, +local filter)
insert into queries (qid, query)
with relations(rel, qidrel) as (
    values ('ce.r',  10000),
           ('ce.rC', 20000)
),
filter_combos(qidbase, qidoff, target, predicol, extra) as (
    -- PK Index Scan: + storage filter, + local filter
    values (500, 1, '*', 'pk', 'pk % 100 <= 33'),
           (500, 2, '*', 'pk', 'pk % 100 <= 33 and non_pushable(pk) <= 5000'),
    -- non-PK Index Scan: + storage index filter, + storage table filter,
    --                    + local filter
           (600, 1, 'v', 'b',  'b % 100 <= 33'),
           (600, 2, 'v', 'b',  'b % 100 <= 33 and e % 100 <= 33'),
           (600, 3, 'v', 'b',  'b % 100 <= 33 and e % 100 <= 33 and non_pushable(e) <= 5000'),
    -- Index Only Scan: + storage filter, + local filter
           (700, 1, 'b', 'b',  'b % 100 <= 33'),
           (700, 2, 'b', 'b',  'b % 100 <= 33 and non_pushable(b) <= 5000')
)
select
    r.qidrel + fc.qidbase + fc.qidoff as qid,
    'select ' || fc.target || ' from ' || r.rel
    || ' where ' || fc.predicol || ' between 1 and 5000'
    || ' and ' || fc.extra as query
from relations r, filter_combos fc;

-- generate colocation x scan kind, single predicate with a 15-item IN list
insert into queries (qid, query)
with relations(rel, qidrel) as (
    values ('ce.r',  10000),
           ('ce.rC', 20000)
),
in_list(qidoff, target, predicol, items) as (
    values (1, '*', 'pk',
            '100, 500, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 11000, 12000, 12345'),
           (2, 'v', 'b',
            '100, 500, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 11000, 12000, 12345'),
           (3, 'b', 'b',
            '100, 500, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 11000, 12000, 12345')
)
select
    r.qidrel + 900 + il.qidoff as qid,
    'select ' || il.target || ' from ' || r.rel
    || ' where ' || il.predicol || ' in (' || il.items || ')' as query
from relations r, in_list il;

-- queries from yb.orig.cardinality_estimate_consistency (single index scan
-- variants; the OR / bitmap combinations are not included here)
insert into queries values
    (31001, $$select pk from ce.r where pk <= 500$$),
    (31002, $$select pk from ce.r where pk <= 500 and pk % 100 <= 33$$),
    (31003, $$select pk from ce.r where pk <= 500 and pk % 100 <= 33 and non_pushable(pk) <= 500$$),
    (31004, $$select pk from ce.r where b <= 500$$),
    (31005, $$select pk from ce.r where b <= 500 and b % 100 <= 33$$),
    (31006, $$select pk from ce.r where b <= 500 and b % 100 <= 33 and non_pushable(b) <= 500$$),
    (31007, $$select b from ce.r where b <= 500$$),
    (31008, $$select b from ce.r where b <= 500 and b % 100 <= 33$$),
    (31009, $$select b from ce.r where b <= 500 and b % 100 <= 33 and non_pushable(b) <= 500$$);

-- queries from yb.orig.seeknext_estimate (colocation x index/index-only
-- x forward/backward scan combinations)
insert into queries (qid, query)
with colocations(colo, qidcolo) as (
    values ('',  50000),
           ('C', 60000)
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
    s.qidbase + c_colo.qidcolo + row_number() over (partition by c_colo.colo, s.qidbase order by c.col_id) as qid,
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

select * from queries order by qid;

update explain_query_options set with_analyze = false;

-- should not return any row.  mismatches indicate non-index scan nodes.
select q.qid, q.query, x.qid, x.nid, "Node Type", "Index Name", "Index Cond"
from queries q full join check_roundtrip_estimates x on x.qid = q.qid
where x.qid is null or q.qid is null
order by coalesce(q.qid, x.qid), x.nid;

update explain_query_options set with_analyze = true;

-- should not return any row.  surface the per-field counts on failure so
-- that both totals mismatches and field-layout mismatches are diagnosable.
select qid, nid, "Node Type", "Index Name", scan_kind, is_colocated,
       "Estimated Table Roundtrips" est_t, "Estimated Index Roundtrips" est_i,
       "Storage Table Read Requests" act_t, "Storage Index Read Requests" act_i,
       "Estimated Roundtrips" est, "Actual Roundtrips" act,
       "Total Estimates", "Field Layout"
from check_roundtrip_estimates
where "Total Estimates" <> 'passed' or "Field Layout" <> 'passed'
order by qid, nid;
