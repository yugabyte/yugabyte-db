-- Test planner's seek/next estimates
\c seeknext_test

delete from queries;

-- generate colocation x index/index-only x forward/backward scan combinations

-- Range variants come in forward/backward pairs differing only in the sort
-- direction, so a next/prev estimate error shows up as an asymmetry between
-- the two halves of a pair rather than as an absolute error in both.
create temp table query_gen as
with colocations(colo, qidcolo) as (
    values ('', 10000),
           ('C', 20000)
),
columns(col_id, col) as (
    values (1, 'pk'), (2, 'f1'), (3, 'f2'), (4, 'f3'), (5, 'f4'), (6, 'f5'),
           (7, 'f10'), (8, 'f100'), (9, 'f1000'), (10, 'dv1'),
           (11, 'skw1'), (12, 'skw2'), (13, 'skw3')
),
scans(qidbase, is_only_scan, is_backward, is_range) as (
    values (1000, false, false, false), -- index scan
           (2000, true,  false, false), -- index only scan
           (3000, false, true,  true),  -- backward index scan
           (4000, true,  true,  true),  -- backward index only scan
           (5000, false, false, true),  -- forward index scan, mirrors 3000
           (6000, true,  false, true)   -- forward index only scan, mirrors 4000
)
select
    s.qidbase + c_colo.qidcolo + row_number() over (partition by c_colo.colo, s.qidbase order by c.col_id) as qid,
    'select ' ||
    case when s.is_only_scan then c.col else 'v, ' || c.col end ||
    ' from r' || c_colo.colo ||
    ' where ' || c.col ||
    case when not s.is_range then ' = 1'
         when s.is_backward then ' <= 1 order by ' || c.col || ' desc'
         else ' <= 1 order by ' || c.col
    end as query,
    s.is_backward
from colocations c_colo,
     scans s,
     columns c
where not (s.is_only_scan and c.col = 'pk');

insert into queries (qid, query) select qid, query from query_gen;


set enable_seqscan = off;
set enable_sort = off;

select * from queries;

update explain_query_options set with_analyze = false;

-- should not return any row.  mismatches indicate non-index scan nodes.
select q.*, x.qid, x.nid, "Node Type", "Index Name", "Index Cond"
from queries q full join check_seeknext_estimates x on x.qid = q.qid
where x.qid is null or q.qid is null;

select count(*) from queries q full join check_seeknext_estimates x on x.qid = q.qid
where x.qid is not null and q.qid is not null;

-- should not return any row.  mismatches indicate the planner scanned in a
-- direction other than the one the query's sort order requires, which would
-- make the next/prev estimates below meaningless.
select x.qid, x.nid, "Node Type", "Index Name", g.is_backward expected_backward
from check_seeknext_estimates x, query_gen g
where g.qid = x.qid
  and ("Node Type" like '% Backward') <> g.is_backward
order by x.qid, x.nid;

update explain_query_options set with_analyze = true;

-- should not return any row.
-- TODO(#34895) 24 queries fail until Next estimation issue is fixed.
select * from check_seeknext_estimates
where "Seek Estimates" <> 'passed' or "Next Estimates" <> 'passed' or "Prev Estimates" <> 'passed'
order by qid, nid;
