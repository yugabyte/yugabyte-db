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

-- generate colocation x forward/backward distinct index scan combinations,
-- each paired with the same query without DISTINCT (qid + 100: a plain index
-- only scan over the same range) so the two estimates can be compared.  pk is
-- unique, so it has no distinct path.  The range condition keeps 1000 groups
-- (fewer for the coarser columns) and puts the group size on both sides of
-- the seek-forward threshold: forward, f1..f3 skip with nexts and f4 onward
-- with a seek per group; backward skips seek from two rows per group up, so
-- only f1 avoids them.  The skewed columns exercise the MCV based group size
-- profile.
insert into queries (qid, query)
with colocations(colo, qidcolo) as (
    values ('', 10000),
           ('C', 20000)
),
columns(col_id, col) as (
    values (2, 'f1'), (3, 'f2'), (4, 'f3'), (5, 'f4'), (6, 'f5'),
           (7, 'f10'), (8, 'f100'), (9, 'f1000'), (10, 'dv1'),
           (11, 'skw1'), (12, 'skw2'), (13, 'skw3')
),
scans(qidbase, is_backward, is_distinct) as (
    values (7000, false, true),  -- distinct index only scan
           (8000, true,  true),  -- backward distinct index only scan
           (7100, false, false), -- index only scan over the same range
           (8100, true,  false)  -- backward index only scan over the same range
)
select
    s.qidbase + c_colo.qidcolo + row_number() over (partition by c_colo.colo, s.qidbase order by c.col_id) as qid,
    'select ' || case when s.is_distinct then 'distinct ' else '' end || c.col ||
    ' from r' || c_colo.colo ||
    ' where ' || c.col || ' <= 1000' ||
    case when s.is_backward then ' order by ' || c.col || ' desc' else '' end as query
from colocations c_colo,
     scans s,
     columns c;

-- IN lists on f10, paired with the same query without DISTINCT (qid + 100)
-- and ordered both ways.  The sparse list has three present values and
-- twelve past the column's last value; the dense list has eight adjacent
-- present values.  DocDB seeks once per present value and stops once the
-- list is exhausted, so the rows fail when the estimate counts a group per
-- listed value or charges a skip after the last one.
insert into queries (qid, query)
with colocations(colo, qidcolo) as (
    values ('', 10000),
           ('C', 20000)
),
in_lists(qidoff, items) as (
    values (21, '1, 100, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 11000, 12000, 12345'),
           (22, '1, 2, 3, 4, 5, 6, 7, 8')
),
scans(qidbase, is_backward, is_distinct) as (
    values (7000, false, true),  -- distinct index only scan
           (8000, true,  true),  -- backward distinct index only scan
           (7100, false, false), -- index only scan over the same list
           (8100, true,  false)  -- backward index only scan over the same list
)
select
    s.qidbase + c_colo.qidcolo + il.qidoff as qid,
    'select ' || case when s.is_distinct then 'distinct ' else '' end || 'f10' ||
    ' from r' || c_colo.colo ||
    ' where f10 in (' || il.items || ')' ||
    ' order by f10' || case when s.is_backward then ' desc' else '' end as query
from colocations c_colo,
     scans s,
     in_lists il;

-- distinct scans on the composite primary key's prefix read the base table
-- itself (Distinct Index Scan rather than Distinct Index Only Scan).
insert into queries values
    (19601, $$select distinct g from rp$$),
    (19602, $$select distinct g from rp where g <= 100$$),
    (19603, $$select distinct g from rp where g between 100 and 200$$),
    (19604, $$select distinct g from rp where g = 100$$),
    (19605, $$select distinct g from rp order by g desc$$),
    (19606, $$select distinct g from rp where g <= 100 order by g desc$$),
    (29601, $$select distinct g from rpC$$),
    (29602, $$select distinct g from rpC where g <= 100$$),
    (29603, $$select distinct g from rpC where g between 100 and 200$$),
    (29604, $$select distinct g from rpC where g = 100$$),
    (29605, $$select distinct g from rpC order by g desc$$),
    (29606, $$select distinct g from rpC where g <= 100 order by g desc$$);

-- the same queries without DISTINCT (qid + 100): plain Index Scans on the
-- primary key.  The unbounded ones keep the index's order, since without it
-- they would be Seq Scans.
insert into queries values
    (19701, $$select g from rp order by g$$),
    (19702, $$select g from rp where g <= 100$$),
    (19703, $$select g from rp where g between 100 and 200$$),
    (19704, $$select g from rp where g = 100$$),
    (19705, $$select g from rp order by g desc$$),
    (19706, $$select g from rp where g <= 100 order by g desc$$),
    (29701, $$select g from rpC order by g$$),
    (29702, $$select g from rpC where g <= 100$$),
    (29703, $$select g from rpC where g between 100 and 200$$),
    (29704, $$select g from rpC where g = 100$$),
    (29705, $$select g from rpC order by g desc$$),
    (29706, $$select g from rpC where g <= 100 order by g desc$$);

-- a storage filter on the prefix column rejects whole groups, and DocDB still
-- reads one row per group and skips from it, so the rows fail when the
-- estimate charges a rejected group more than one row and one skip.  The
-- modulus keeps one value in 200, the selectivity the planner assumes for an
-- expression without statistics, so the result rows are estimated right.
-- Paired with the forward query without DISTINCT (qid + 100) only: the
-- backward plain scan's seek count moves between runs by one, across the
-- tolerance.
insert into queries values
    (19607, $$select distinct g from rp where g % 200 = 0 order by g$$),
    (19608, $$select distinct g from rp where g % 200 = 0 order by g desc$$),
    (29607, $$select distinct g from rpC where g % 200 = 0 order by g$$),
    (29608, $$select distinct g from rpC where g % 200 = 0 order by g desc$$);

insert into queries values
    (19707, $$select g from rp where g % 200 = 0 order by g$$),
    (29707, $$select g from rpC where g % 200 = 0 order by g$$);

-- a distinct scan whose prefix groups hold one row each, with an index
-- condition on the second key column: nine groups in ten yield nothing, but
-- every group is still stepped over, so the nexts do not scale with the
-- condition's selectivity.  Paired with the same query without DISTINCT
-- (qid + 100), which walks the same rows.
insert into queries values
    (19801, $$select distinct u from rq where k = 3$$),
    (19802, $$select distinct u from rq where k = 3 order by u desc$$),
    (29801, $$select distinct u from rqC where k = 3$$),
    (29802, $$select distinct u from rqC where k = 3 order by u desc$$);

insert into queries values
    (19901, $$select u from rq where k = 3 order by u$$),
    (19902, $$select u from rq where k = 3 order by u desc$$),
    (29901, $$select u from rqC where k = 3 order by u$$),
    (29902, $$select u from rqC where k = 3 order by u desc$$);

-- distinct scans on a secondary index that read the base table: the hint
-- keeps the index-only variant out, and the scan fetches one base-table row
-- per result group, a seek and a next each on top of the index walk, so the
-- rows fail when the estimate leaves the fetch out or charges it per row in
-- range.  Paired with the same query without DISTINCT (qid + 100).
insert into queries values
    (19001, $$/*+IndexScan(r r_f10_idx)*/ select distinct f10 from r where f10 <= 1000 order by f10$$),
    (19002, $$/*+IndexScan(r r_f10_idx)*/ select distinct f10 from r where f10 <= 1000 order by f10 desc$$),
    (29001, $$/*+IndexScan(rc rc_f10_idx)*/ select distinct f10 from rC where f10 <= 1000 order by f10$$),
    (29002, $$/*+IndexScan(rc rc_f10_idx)*/ select distinct f10 from rC where f10 <= 1000 order by f10 desc$$);

insert into queries values
    (19101, $$/*+IndexScan(r r_f10_idx)*/ select f10 from r where f10 <= 1000 order by f10$$),
    (19102, $$/*+IndexScan(r r_f10_idx)*/ select f10 from r where f10 <= 1000 order by f10 desc$$),
    (29101, $$/*+IndexScan(rc rc_f10_idx)*/ select f10 from rC where f10 <= 1000 order by f10$$),
    (29102, $$/*+IndexScan(rc rc_f10_idx)*/ select f10 from rC where f10 <= 1000 order by f10 desc$$);

-- distinct scans on a hash-sharded primary key: every skip seeks to the next
-- hash group inside its tablet, and an IN list on h is one seek per listed
-- value whatever the values, since hash order scatters them; one listed
-- value is absent.  The unconditioned scan has no plain counterpart: without
-- a condition a hash index offers no plain Index Scan path.
insert into queries values
    (19201, $$select distinct h from rh$$),
    (19202, $$select distinct h from rh where h in (1, 2, 3, 500, 100000)$$);

insert into queries values
    (19302, $$select h from rh where h in (1, 2, 3, 500, 100000)$$);

-- distinct scans with a condition on the key column after the prefix: the
-- skip lands on each group's first row in scan direction, and the scan steps
-- over the rows before the first match, with a seek once they pass the
-- seek-forward threshold, so the rows fail when the estimate leaves that
-- positioning out or charges it to a group whose landing row matches.
-- Paired with the same query without DISTINCT (qid + 100).
insert into queries values
    (19401, $$select distinct g from ps where k = 0 order by g$$),
    (19402, $$select distinct g from ps where k = 2 order by g$$),
    (19403, $$select distinct g from ps where k >= 5 order by g$$),
    (19404, $$select distinct g from ps where k = 0 order by g desc$$),
    (19405, $$select distinct g from ps where k = 2 order by g desc$$),
    (19406, $$select distinct g from ps where k >= 5 order by g desc$$),
    (29401, $$select distinct g from psC where k = 0 order by g$$),
    (29402, $$select distinct g from psC where k = 2 order by g$$),
    (29403, $$select distinct g from psC where k >= 5 order by g$$),
    (29404, $$select distinct g from psC where k = 0 order by g desc$$),
    (29405, $$select distinct g from psC where k = 2 order by g desc$$),
    (29406, $$select distinct g from psC where k >= 5 order by g desc$$);

insert into queries values
    (19501, $$select g from ps where k = 0 order by g$$),
    (19502, $$select g from ps where k = 2 order by g$$),
    (19503, $$select g from ps where k >= 5 order by g$$),
    (19504, $$select g from ps where k = 0 order by g desc$$),
    (19505, $$select g from ps where k = 2 order by g desc$$),
    (19506, $$select g from ps where k >= 5 order by g desc$$),
    (29501, $$select g from psC where k = 0 order by g$$),
    (29502, $$select g from psC where k = 2 order by g$$),
    (29503, $$select g from psC where k >= 5 order by g$$),
    (29504, $$select g from psC where k = 0 order by g desc$$),
    (29505, $$select g from psC where k = 2 order by g desc$$),
    (29506, $$select g from psC where k >= 5 order by g desc$$);

-- IN lists on ps, whose values sit 100 apart: the sparse list spans 0..190
-- and hits two groups, the dense list spans 0..1900 and hits all twenty, so
-- the rows fail when the estimate counts a group for an absent value inside
-- the list's range.  Paired with the same query without DISTINCT (qid + 100).
insert into queries values
    (19411, $$select distinct g from ps where g in (0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 170, 180, 190) order by g$$),
    (19412, $$select distinct g from ps where g in (0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700, 1800, 1900) order by g$$),
    (19413, $$select distinct g from ps where g in (0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 170, 180, 190) order by g desc$$),
    (19414, $$select distinct g from ps where g in (0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700, 1800, 1900) order by g desc$$),
    (29411, $$select distinct g from psC where g in (0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 170, 180, 190) order by g$$),
    (29412, $$select distinct g from psC where g in (0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700, 1800, 1900) order by g$$),
    (29413, $$select distinct g from psC where g in (0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 170, 180, 190) order by g desc$$),
    (29414, $$select distinct g from psC where g in (0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700, 1800, 1900) order by g desc$$);

insert into queries values
    (19511, $$select g from ps where g in (0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 170, 180, 190) order by g$$),
    (19512, $$select g from ps where g in (0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700, 1800, 1900) order by g$$),
    (19513, $$select g from ps where g in (0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 170, 180, 190) order by g desc$$),
    (19514, $$select g from ps where g in (0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700, 1800, 1900) order by g desc$$),
    (29511, $$select g from psC where g in (0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 170, 180, 190) order by g$$),
    (29512, $$select g from psC where g in (0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700, 1800, 1900) order by g$$),
    (29513, $$select g from psC where g in (0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 170, 180, 190) order by g desc$$),
    (29514, $$select g from psC where g in (0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700, 1800, 1900) order by g desc$$);

-- distinct scans whose first match sits at different depths in each group
-- of ps.  Under a strict bound the scan steps over the bound value's own
-- rows as well (19421, 19422), the tighter of two bounds decides in either
-- order (19423, 19424), and under an IN list DocDB binds only the listed
-- values the other conditions accept, so the first of those decides
-- (19425-19427).  The rows fail when the estimate counts the rows before the
-- wrong value across the seek-forward threshold.  29421-29427 are the
-- colocated twins.
insert into queries values
    (19421, $$select distinct g from ps where k > 2 order by g$$),
    (19422, $$select distinct g from ps where k < 9 order by g desc$$),
    (19423, $$select distinct g from ps where k > 2 and k >= 1 order by g$$),
    (19424, $$select distinct g from ps where k >= 1 and k > 2 order by g$$),
    (19425, $$select distinct g from ps where k in (4, 9) and k >= 5 order by g$$),
    (19426, $$select distinct g from ps where k in (4, 9) and k in (5, 9) order by g$$),
    (19427, $$select distinct g from ps where k in (3, 7) and k > 3 order by g$$),
    (29421, $$select distinct g from psC where k > 2 order by g$$),
    (29422, $$select distinct g from psC where k < 9 order by g desc$$),
    (29423, $$select distinct g from psC where k > 2 and k >= 1 order by g$$),
    (29424, $$select distinct g from psC where k >= 1 and k > 2 order by g$$),
    (29425, $$select distinct g from psC where k in (4, 9) and k >= 5 order by g$$),
    (29426, $$select distinct g from psC where k in (4, 9) and k in (5, 9) order by g$$),
    (29427, $$select distinct g from psC where k in (3, 7) and k > 3 order by g$$);

-- the same on psd's descending k: a forward scan meets the rows an upper
-- bound rejects first (19432) and an IN list's highest value first (19436),
-- a backward scan the rows a lower bound rejects (19434) and an IN list's
-- lowest value (19437), and the opposite bound matches the group's first
-- row (19431, 19433, 19435).  29431-29437 are the colocated twins.
insert into queries values
    (19431, $$select distinct g from psd where k >= 5 order by g$$),
    (19432, $$select distinct g from psd where k < 5 order by g$$),
    (19433, $$select distinct g from psd where k > 5 order by g$$),
    (19434, $$select distinct g from psd where k >= 5 order by g desc$$),
    (19435, $$select distinct g from psd where k < 5 order by g desc$$),
    (19436, $$select distinct g from psd where k in (3, 7) order by g$$),
    (19437, $$select distinct g from psd where k in (0, 6) order by g desc$$),
    (29431, $$select distinct g from psdC where k >= 5 order by g$$),
    (29432, $$select distinct g from psdC where k < 5 order by g$$),
    (29433, $$select distinct g from psdC where k > 5 order by g$$),
    (29434, $$select distinct g from psdC where k >= 5 order by g desc$$),
    (29435, $$select distinct g from psdC where k < 5 order by g desc$$),
    (29436, $$select distinct g from psdC where k in (3, 7) order by g$$),
    (29437, $$select distinct g from psdC where k in (0, 6) order by g desc$$);

-- NULLs, which no bound accepts: the scan steps over them when it meets them
-- first, going forward over pn's NULLS FIRST column and backward over pna's
-- NULLS LAST column.  The NULLs alone precede the match under a bound the
-- first value passes (19441, 19443, 19444) and under IS NOT NULL (19448);
-- under k = 0 the nine values above 0 come before it as well (19447).  The
-- scan does not step over them when it meets them last (19442, 19445), or on
-- pnp's partial index, which leaves them out (19446).  29441-29448 are the
-- colocated twins.
insert into queries values
    (19441, $$select distinct g from pn where k >= 5 order by g$$),
    (19442, $$select distinct g from pn where k < 5 order by g desc$$),
    (19443, $$select distinct g from pn where k <= 9 order by g$$),
    (19444, $$select distinct g from pna where k <= 9 order by g desc$$),
    (19445, $$select distinct g from pna where k >= 0 order by g$$),
    (19446, $$select distinct g from pnp where k <= 9 order by g desc$$),
    (19447, $$select distinct g from pn where k = 0 order by g$$),
    (19448, $$select distinct g from pn where k is not null order by g$$),
    (29441, $$select distinct g from pnC where k >= 5 order by g$$),
    (29442, $$select distinct g from pnC where k < 5 order by g desc$$),
    (29443, $$select distinct g from pnC where k <= 9 order by g$$),
    (29444, $$select distinct g from pnaC where k <= 9 order by g desc$$),
    (29445, $$select distinct g from pnaC where k >= 0 order by g$$),
    (29446, $$select distinct g from pnpC where k <= 9 order by g desc$$),
    (29447, $$select distinct g from pnC where k = 0 order by g$$),
    (29448, $$select distinct g from pnC where k is not null order by g$$);

-- an enum key column after the prefix, which the opfamily compares as
-- anyenum rather than as the enum type: going backward the scan meets the
-- labels above the bound first, under an equality (19451) and an IN list
-- (19452), and the rows fail when the estimate leaves those labels out.
-- 29451 and 29452 are the colocated twins.
insert into queries values
    (19451, $$select distinct g from pe where m = 'e5' order by g desc$$),
    (19452, $$select distinct g from pe where m in ('e3', 'e5') order by g desc$$),
    (29451, $$select distinct g from peC where m = 'e5' order by g desc$$),
    (29452, $$select distinct g from peC where m in ('e3', 'e5') order by g desc$$);

-- distinct scans over the two-column prefix (a, b).  A condition on b picks
-- values inside each value of a.  The scan enters each value of a from the
-- previous one's last group and steps over the rows the condition rejects
-- before the first value it accepts: forward under a lower bound or an IN
-- list that leaves out b's lowest values (19465, 19467), backward under an
-- upper bound or an IN list that leaves out its highest (19464, 19468).  The
-- same step positions the group it lands on for a condition on c (19471).
-- The rows fail when the estimate leaves those steps out, or charges them
-- where the rejected rows come last in scan order (19463, 19466), where an
-- IN list on a makes DocDB seek to each value of a with b's lower bound
-- (19469), or where an equality on a makes the condition on b bound the
-- scan (19470).  29461-29471 are the colocated twins.
insert into queries values
    (19461, $$select distinct a, b from pt order by a, b$$),
    (19462, $$select distinct a, b from pt order by a desc, b desc$$),
    (19463, $$select distinct a, b from pt where b <= 5 order by a, b$$),
    (19464, $$select distinct a, b from pt where b <= 5 order by a desc, b desc$$),
    (19465, $$select distinct a, b from pt where b >= 46 order by a, b$$),
    (19466, $$select distinct a, b from pt where b >= 46 order by a desc, b desc$$),
    (19467, $$select distinct a, b from pt where b in (3, 17, 40) order by a, b$$),
    (19468, $$select distinct a, b from pt where b in (3, 17, 40) order by a desc, b desc$$),
    (19469, $$select distinct a, b from pt where a in (5, 50, 150) and b between 11 and 20 order by a, b$$),
    (19470, $$select distinct a, b from pt where a = 5 and b >= 46 order by a, b$$),
    (19471, $$select distinct a, b from pt where b >= 46 and c >= 3 order by a, b$$),
    (29461, $$select distinct a, b from ptC order by a, b$$),
    (29462, $$select distinct a, b from ptC order by a desc, b desc$$),
    (29463, $$select distinct a, b from ptC where b <= 5 order by a, b$$),
    (29464, $$select distinct a, b from ptC where b <= 5 order by a desc, b desc$$),
    (29465, $$select distinct a, b from ptC where b >= 46 order by a, b$$),
    (29466, $$select distinct a, b from ptC where b >= 46 order by a desc, b desc$$),
    (29467, $$select distinct a, b from ptC where b in (3, 17, 40) order by a, b$$),
    (29468, $$select distinct a, b from ptC where b in (3, 17, 40) order by a desc, b desc$$),
    (29469, $$select distinct a, b from ptC where a in (5, 50, 150) and b between 11 and 20 order by a, b$$),
    (29470, $$select distinct a, b from ptC where a = 5 and b >= 46 order by a, b$$),
    (29471, $$select distinct a, b from ptC where b >= 46 and c >= 3 order by a, b$$);


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

-- the plans where a wrong estimate shows only as a plan change.  Going
-- forward over f2's groups of two rows on r and rC (17002, 27002), the
-- distinct scan does the plain scan's nexts and returns half the rows, so it
-- must win.  On psd's descending k under k > 5 (19433, 29433), each group's
-- first row matches, so the distinct scan seeks once per group and must win
-- over Index Scan plus Unique; counting the rows below the bound instead
-- prices it above that plan, whose estimates pass the checks.  The backward
-- scans of the same 2-row groups, 18002 and 28002, are not pinned: a
-- backward skip seeks on every group of more than one row, which costs about
-- what returning half the rows saves, and the two plans ran about the same
-- on a 2,000,000-row table of such groups.
select qid, "Node Type" from check_seeknext_estimates
where qid in (17002, 19433, 27002, 29433)
order by qid;

update explain_query_options set with_analyze = true;

-- The first scan of enum labels through an IN list in a session reads the
-- catalog while it runs, and the scan's storage metrics count those reads:
-- 19452 measures extra seeks and nexts on its first execution.  Run one such
-- scan first, so the checks below see only the scans' own reads.
select count(*) from pe where m in ('e3', 'e5');

-- should return only the rows explained below.
-- TODO(#34895) 24 queries fail until Next estimation issue is fixed.
--
-- A backward scan that skips between groups steps forward once, and back
-- once more, for each row it returns that is the first of its group, unless
-- that row is the tablet's first; the estimates leave those steps out.  The
-- counts give this pattern; its mechanism is unknown.  skw3 has four groups
-- of one row, its lowest value's among them, which starts the index:
-- 17012/18012 (on r) measure 3 nexts and 27012/28012 (on rC) 4 against none.
-- Under k = 0 on ps, each group's first row matches: 19404 measures 999
-- nexts against none, 29404 1000, and the plain counterpart 19504 999.  rq's
-- groups have one row each: 19802 (k = 3) measures 1235 nexts and 13582
-- prevs against none and 12345, and its colocated twin and its plain
-- counterpart 19902 measure the same.
--
-- 17010/18010 (skw1 backward on r) measure 18 seeks against 16, and
-- 27010/28010 (on rC) 33 prevs and a next against 31 prevs: with 16 groups,
-- two extra steps exceed the tolerance.  The next is skw1's one-row group,
-- which on r starts the tablet.  On r the first backward seek, at the bound
-- 1000, lands past the tablet's last key and PreparePrev falls back to
-- SeekToLast; the cause of the other extra steps is unknown.  The plain
-- backward scan of the same range (18110) measures the extra seek too,
-- within its tolerance.
--
-- A condition on the key column after the prefix gives a group a match with
-- probability 1 - (1 - s)^n, for n rows per group each passing with
-- selectivity s, as if the passing rows fell into the groups independently.
-- ps, psd and pe hold every value of that column in every group, so every
-- group has a match where the estimate expects 65% (k = 0 or 2, m = 'e5'),
-- 72% (19427) or 89% (19437) of them to, and charges the groups it takes as
-- unmatched a cheaper exit: 19401 (k = 0) and 19402 (k = 2) estimate 2651
-- and 4651 nexts against 3000 and 5000, 19405 (k = 2 backward) and 19451
-- 2623 and 2651 prevs against 3000, 19427 4721 nexts against 5000, 19437
-- 1893 prevs against 2000, and 19404 1651 prevs against 3000.  The colocated
-- twins measure the same.
--
-- TODO(#34776): cap the IN-list groups by the values the column has, which
-- takes the sparse-list rows below off this list.
-- 17021/18021/27021/28021 (sparse IN list on f10) and 19411/19413/29411/29413
-- (sparse IN list on ps) count a group per listed value: 15 against the 3
-- present on f10, whose other 12 values lie past the column's last value, and
-- 20 against the 2 present on ps, whose other 18 fall between groups.  19202
-- (IN list on the hash column) estimates 13 nexts against 10: the absent
-- value costs its seek but has no row to read or skip from.
--
-- The rest are plain scans under the regular model.  19704 and its colocated
-- twin, an equality on the leading key column, estimate 12 nexts against 10.
-- 19901 and its backward and colocated twins (k = 3 on rq) are charged 24691
-- seeks, two per row of rq: 19901 measures 2, walking the range, and 19902
-- 11113, about one for each of the 11110 rows that fail k = 3.  The IN lists
-- on a range key column (17121/17122/18121/18122, 19511-19514 and their
-- colocated twins) are charged a seek and a group's rows for every listed
-- value, present or not: 15 seeks and 165 nexts against 3 and 35 for the
-- sparse list on f10, 8 seeks against 1 for the dense one.  19302, the IN
-- list on the hash column, expects rows for the absent value: 60 nexts
-- against 46.  On ps, an equality on the second key column is charged two
-- seeks per group whatever the position of the match: forward, 19501/29501
-- (k = 0, the group's first row) and 19502/29502 (k = 2, reached with nexts)
-- measure one seek per group, and 19502 5000 nexts against 4000; backward,
-- 19504/29504 (k = 0) measure one seek per group, and 19505/29505 (k = 2)
-- 3000 prevs against 4000.  A range on it, k >= 5, is charged about the rows
-- it keeps: 19503/29503 estimate 6000 nexts against 7004, and backward
-- 19506/29506 6000 prevs against 10008 prevs, every row of each group, and
-- 4000 nexts.
select * from check_seeknext_estimates
where "Seek Estimates" <> 'passed' or "Next Estimates" <> 'passed' or "Prev Estimates" <> 'passed'
order by qid, nid;
