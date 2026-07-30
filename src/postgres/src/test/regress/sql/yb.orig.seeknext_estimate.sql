-- Test planner's seek/next estimates
\c seeknext_test

delete from queries;

-- generate colocation x index/index-only x forward/backward scan combinations

insert into queries (qid, query)
with colocations(colo, qidcolo) as (
    values ('', 10000),
           ('C', 20000)
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
    ' from r' || c_colo.colo ||
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
set enable_sort = off;

select * from queries;

update explain_query_options set with_analyze = false;

-- should not return any row.  mismatches indicate non-index scan nodes.
select q.*, x.qid, x.nid, "Node Type", "Index Name", "Index Cond"
from queries q full join check_seeknext_estimates x on x.qid = q.qid
where x.qid is null or q.qid is null;

select count(*) from queries q full join check_seeknext_estimates x on x.qid = q.qid
where x.qid is not null and q.qid is not null;

update explain_query_options set with_analyze = true;

-- should not return any row.
select * from check_seeknext_estimates
where "Seek Estimates" <> 'passed' or "Next/Prev Estimates" <> 'passed'
order by qid, nid;
