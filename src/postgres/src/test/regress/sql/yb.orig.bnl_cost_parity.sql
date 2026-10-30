-- Compare the planner's cost estimates for a 2-table join (outer ce.s joined
-- to inner ce.r / ce.rC) run with four join methods -- YB Batched Nested Loop
-- (BNL), Hash Join (HJ), Merge Join (MJ) and an un-batched Nested Loop (NL) --
-- against the work each plan actually performs at run time.
--
-- DEPENDENCY: yb.orig.bnl_cost_parity_setup
--
-- Every method is pinned with a Leading((s r)) hint so s is always the outer
-- and r (ce.r or ce.rC, aliased r) the inner; the inner/outer alias and the
-- realized join node type are validated (method_ok / leading_ok) once, up
-- front, from a plain non-executing EXPLAIN (join_plans).  All four methods
-- return the same rows, so actual_rows must agree per query.
--
-- Each query prints the deterministic run-time work every plan actually did
-- (EXPLAIN ANALYZE, DEBUG, DIST, SUMMARY ON):
--   read_reqs     query-level "Storage Read Requests" (DocDB network round
--                 trips -- the dominant latency factor for probe-bound plans)
--   rows_scanned  query-level "Storage Rows Scanned" (dominant for the
--                 scan-bound LIMIT plans, where round trips are cheap)
--   table_reads,
--   index_reads   read requests split by side (sum over scan nodes)
-- Wall-clock timings vary run to run and are NOT printed; the predicates are
-- chosen so this work is well separated, so it tracks the real (measured)
-- execution-time ordering.
--
-- The output is COLLAPSED to one row per query, with two method orderings
-- and the planner's own choice:
--   cost_rank  labels ordered by the planner's (total cost, startup cost) --
--              the order CBO would prefer (cheapest first).
--   chosen     the method the planner picks when the hint fixes only the join
--              order (Leading((s r)), see the setup's join_choice).  It is
--              cost_rank's first label; on an exact tie it shows which of the
--              tied methods the planner keeps.
--   rt_rank    labels ordered by the work that dominates the query's run time
--              (fastest first): read_reqs for the probe-bound joins (#30580,
--              #30565), rows_scanned for the scan-bound LIMIT joins (#30738).
--              The choice reflects the environment the expected output was
--              recorded in (single node, sub-ms round trips); on a real
--              network round trips weigh far more relative to rows.
-- (read_reqs, rows_scanned) is sufficient to rank run time here.  The
-- rocksdb seek/next counters stay in join_costs but are NOT printed in the
-- 1xx-3xx sections: they depend on the memtable/SST layout at execution time
-- (the Java harness runs 1 MB memstores, so flush boundaries move run to
-- run), and a backward scan trades a next for an extra seek at each layout
-- boundary -- a +-1 flap per scan.  They also cannot reorder those methods:
-- on insert-only tables nexts tracks rows_scanned, and seeks amount to a few
-- per request plus one per batched key -- each ~1000x cheaper than a round
-- trip.  The 4xx DISTINCT section is the exception: its skip scans do a seek
-- per group against one scanned row per group, which (read_reqs,
-- rows_scanned) cannot rank, so it ranks by read_reqs and then by the work
-- key nexts + 3 seeks, with a tolerance that absorbs the flap (see the
-- section and the setup's seek_weight / work_slack).
-- Labels tied on a ranking's keys are joined with '=' (e.g. 'BNL=NL MJ HJ');
-- keys compare exactly, so any real movement breaks a tie visibly.
-- Cost columns list their values in cost_rank order, work columns in
-- rt_rank order.  The cost model mis-ranks the plans -- the point of these
-- tests -- where cost_rank puts a method ahead of one that rt_rank puts
-- ahead of it, and each report prints only those queries: a tie on either
-- side agrees with any order on the other (rank_consistent in the setup),
-- so a query whose orderings differ only by a tie is left out.  Costs are
-- masked to one '#' per integer digit to pin each plan's magnitude and the
-- ranking without churning the expected output on small cost moves.
--
-- The tests were written for three BNL cost-model defects, since fixed, and
-- keep recording the rankings around them:
--
--   #30580  (fixed) The semi/anti/inner-unique ntuples overcount (a missing
--           "/ batch_size") costed BNL far above HJ/MJ although it does the
--           least work; cost_rank now agrees with rt_rank (BNL first).  On
--           the 1-outer rows BNL and NL do the same work and are costed
--           alike (see the section).
--
--   #30565  (fixed) BNL omitted the cost of skipping the leading all-empty
--           batches from its startup cost, so with the outer sorted on a
--           non-unique indexed column and the first 2-3 batches matching
--           nothing it looked the cheapest to start while not the fastest
--           plan.  The empty batches are charged to startup now, and MJ
--           leads BNL by startup cost, total cost and work alike.
--
--   #30738  (fixed) With LIMIT, the uncharged startup made BNL the cheapest
--           plan by (limit-adjusted) total cost while it scanned far more
--           rows than the un-batched NL: the execution-time limit pushdown
--           shrinks the first value list to values that are all absent from
--           the inner, gets an empty first scan, then falls back to a full
--           batch that scans far more rows than the LIMIT needs.  MJ leads
--           both orderings now; BNL is still costed below NL from LIMIT 100
--           up while NL scans fewer rows.
--
-- The 4xx section (#18943) records no defect: it ranks DISTINCT joins whose
-- inner is a distinct index scan against the same joins with the plain inner
-- ('/p' methods), by requests and storage work.
--
-- The 5xx section joins many outer rows on a non-unique key that most of them
-- do not reach.  BNL is costed above HJ and MJ although it scans fewer rows:
-- its inner scan is estimated as if every key of a batch had rows.
--
-- The 6xx section joins a 1024-row outer on a two-column key, whose inner
-- scan estimate applies the batch size once per key column (#19549), so
-- the planner's choice moves away from BNL once the join takes more than
-- one batch.
\c bnl_cost_parity_test
set client_min_messages to 'warning';

------------------------------------------------------------------------------
-- Build the query set.  Each base query is templated over the inner relation
-- (@R@ -> ce.r non-colocated, ce.rC colocated; +1000 to the qid for ce.rC).
------------------------------------------------------------------------------
delete from queries;

insert into queries
with rels(rel, qoff) as (
    values ('ce.r', 0), ('ce.rC', 1000)
),
base(qid, descr, tmpl) as (
    values
    -- #30580: inner join / semijoin / antijoin on a unique PK (pk), a
    -- secondary unique index (b) and a non-unique secondary index over a
    -- unique-valued column (c).  For c the inner-join case is the control:
    -- the planner cannot tell the column is unique, so BNL is NOT overcosted;
    -- the semi/anti cases are overcosted regardless of index uniqueness.
    --
    -- The outer is restricted to s.z <= 1024 (z is the shuffled key, so this is
    -- 1024 rows -- exactly one default BNL batch -- whose join values are spread
    -- across the whole inner key range).  BNL then point-probes those keys in a
    -- single batch (fewest requests, fewest rows scanned), while MJ/HJ must scan
    -- a wide swath / all of the 12345-row inner and NL pays one request per outer
    -- row -- so BNL really is the least-work, fastest plan even though the bug
    -- costs it above MJ and HJ (cost_rank 3).
    (101, '30580 pk inner',
        'select s.id from ce.s s join @R@ r on r.pk = s.id where s.z <= 1024 and r.e <= 600 order by s.id'),
    (102, '30580 pk semi',
        'select s.id from ce.s s where s.z <= 1024 and exists (select 1 from @R@ r where r.pk = s.id and r.e <= 600) order by s.id'),
    (103, '30580 pk anti',
        'select s.id from ce.s s where s.z <= 1024 and not exists (select 1 from @R@ r where r.pk = s.id and r.e <= 600) order by s.id'),
    (104, '30580 b  inner',
        'select s.id from ce.s s join @R@ r on r.b = s.x where s.z <= 1024 and r.e <= 600 order by s.x'),
    (105, '30580 b  semi',
        'select s.id from ce.s s where s.z <= 1024 and exists (select 1 from @R@ r where r.b = s.x and r.e <= 600) order by s.x'),
    (106, '30580 b  anti',
        'select s.id from ce.s s where s.z <= 1024 and not exists (select 1 from @R@ r where r.b = s.x and r.e <= 600) order by s.x'),
    (107, '30580 c  inner',
        'select s.id from ce.s s join @R@ r on r.c = s.v::bpchar where s.z <= 1024 and r.e <= 600 order by s.v'),
    (108, '30580 c  semi',
        'select s.id from ce.s s where s.z <= 1024 and exists (select 1 from @R@ r where r.c = s.v::bpchar and r.e <= 600) order by s.v'),
    (109, '30580 c  anti',
        'select s.id from ce.s s where s.z <= 1024 and not exists (select 1 from @R@ r where r.c = s.v::bpchar and r.e <= 600) order by s.v'),
    -- 1-outer: a single outer row, both estimated and actual, via an equality
    -- on a unique indexed column (see the #30580 report comment).  The outer
    -- restriction is never on the join key itself -- that would derive a
    -- constant inner equality and replace the batched clause.  s.x = s.id =
    -- 1000 selects the same row either way; its match r row (pk = b = 1000)
    -- passes r.e <= 600.
    (111, '30580 pk inner 1-outer',
        'select s.id from ce.s s join @R@ r on r.pk = s.id where s.x = 1000 and r.e <= 600 order by s.id'),
    (112, '30580 pk semi 1-outer',
        'select s.id from ce.s s where s.x = 1000 and exists (select 1 from @R@ r where r.pk = s.id and r.e <= 600) order by s.id'),
    (113, '30580 pk anti 1-outer',
        'select s.id from ce.s s where s.x = 1000 and not exists (select 1 from @R@ r where r.pk = s.id and r.e <= 600) order by s.id'),
    (114, '30580 b  inner 1-outer',
        'select s.id from ce.s s join @R@ r on r.b = s.x where s.id = 1000 and r.e <= 600 order by s.x'),
    (115, '30580 b  semi 1-outer',
        'select s.id from ce.s s where s.id = 1000 and exists (select 1 from @R@ r where r.b = s.x and r.e <= 600) order by s.x'),
    (116, '30580 b  anti 1-outer',
        'select s.id from ce.s s where s.id = 1000 and not exists (select 1 from @R@ r where r.b = s.x and r.e <= 600) order by s.x'),
    (117, '30580 c  inner 1-outer',
        'select s.id from ce.s s join @R@ r on r.c = s.v::bpchar where s.x = 1000 and r.e <= 600 order by s.v'),
    (118, '30580 c  semi 1-outer',
        'select s.id from ce.s s where s.x = 1000 and exists (select 1 from @R@ r where r.c = s.v::bpchar and r.e <= 600) order by s.v'),
    (119, '30580 c  anti 1-outer',
        'select s.id from ce.s s where s.x = 1000 and not exists (select 1 from @R@ r where r.c = s.v::bpchar and r.e <= 600) order by s.v'),
    -- 1-outer on the non-unique key a: r.a spans 3886..5120, so a = s.y has
    -- match fraction ~ 1235/5120 (< 0.5) and y = 1000 itself has no match
    -- (the semi returns nothing, the anti returns the row).
    (121, '30580 a  inner 1-outer',
        'select s.id from ce.s s join @R@ r on r.a = s.y where s.x = 1000 and r.e <= 600 order by s.y'),
    (122, '30580 a  semi 1-outer',
        'select s.id from ce.s s where s.x = 1000 and exists (select 1 from @R@ r where r.a = s.y and r.e <= 600) order by s.id'),
    (123, '30580 a  anti 1-outer',
        'select s.id from ce.s s where s.x = 1000 and not exists (select 1 from @R@ r where r.a = s.y and r.e <= 600) order by s.id'),
    -- Left joins on the unique keys, the many-outer and the 1-outer forms of
    -- 101/104/107 and 111/114/117.  The inner filter sits in ON so the join
    -- stays a left join, and r.d is projected so the join is not removed.
    (131, '30580 pk left',
        'select s.id, r.d from ce.s s left join @R@ r on r.pk = s.id and r.e <= 600 where s.z <= 1024 order by s.id'),
    (134, '30580 b  left',
        'select s.id, r.d from ce.s s left join @R@ r on r.b = s.x and r.e <= 600 where s.z <= 1024 order by s.x'),
    (137, '30580 c  left',
        'select s.id, r.d from ce.s s left join @R@ r on r.c = s.v::bpchar and r.e <= 600 where s.z <= 1024 order by s.v'),
    (141, '30580 pk left 1-outer',
        'select s.id, r.d from ce.s s left join @R@ r on r.pk = s.id and r.e <= 600 where s.x = 1000 order by s.id'),
    (144, '30580 b  left 1-outer',
        'select s.id, r.d from ce.s s left join @R@ r on r.b = s.x and r.e <= 600 where s.id = 1000 order by s.x'),
    (147, '30580 c  left 1-outer',
        'select s.id, r.d from ce.s s left join @R@ r on r.c = s.v::bpchar and r.e <= 600 where s.x = 1000 order by s.v'),
    -- #30565: outer ce.s scanned by the non-unique indexed column y FORWARD
    -- (ascending -- no backward index scan), joined to the non-unique inner key
    -- r.a (values 3886..5120, the top of s.y's range).  Rows with y < 3886
    -- match nothing, so with "s.y >= 1621" the first ~2 batches (y in
    -- 1621..3885) are empty before the matches at y >= 3886.  The "no-empty"
    -- companion (s.y >= 3886) matches from the first row.
    (201, '30565 empty-lead',
        'select s.y from ce.s s join @R@ r on r.a = s.y where s.y >= 1621 order by s.y'),
    (202, '30565 no-empty',
        'select s.y from ce.s s join @R@ r on r.a = s.y where s.y >= 3886 order by s.y'),
    -- Positive control: a case where HJ legitimately wins, so the cost model
    -- should (and does) rank it first.  The outer is joined on s.v (the only
    -- non-indexed outer column), which forces MJ to sort the 5120-row outer,
    -- and a sparse r.e <= 600 filter trims the inner.  HJ just builds one hash
    -- and probes once: it is the cheapest (MJ pays the sort) and the joint-
    -- fewest read requests, with BNL and NL clearly worse -- cost_rank and
    -- rt_rank agree, both led by HJ.  This is ce.r only: on a colocated inner a
    -- 1-to-1 join is so probe-friendly that BNL does the least work (and is
    -- mis-costed 3rd, a #30580-style case), so it would not illustrate HJ-best.
    (203, '30565 hj-best',
        'select s.id from ce.s s join @R@ r on r.c = s.v::bpchar where r.e <= 600'),
    -- Trailing mismatches
    -- #33001: MJ stops as soon as finding a mismatch
    --   <= on outer
    (210, 'id <= 1500',
        'select s.id from ce.s s join @R@ r on s.id = r.pk where s.id <= 1500 order by s.id'),
    (211, 'xx <= 150',
        'select s.xx from ce.s s join @R@ r on s.xx = r.pk where s.xx <= 150 order by s.xx'),
    (212, 'x <= 1500',
        'select s.x from ce.s s join @R@ r on s.x = r.pk where s.x <= 1500 order by s.x'),
    --   <= on inner
    (220, 'pk <= 1500',
        'select r.pk from ce.s s join @R@ r on s.x = r.pk where r.pk <= 1500 order by r.pk'),
    (221, 'a <= 3950',
        'select r.a from ce.s s join @R@ r on s.x = r.a where r.a <= 3950 order by r.a'),
    (222, 'b <= 1500',
        'select r.b from ce.s s join @R@ r on s.x = r.b where r.b <= 1500 order by r.b'),
    -- backward scan
    --   >= on outer
    (230, 'id >= 1500',
        'select s.id from ce.s s join @R@ r on s.id = r.pk where s.id >= 1500 order by s.id desc'),
    (231, 'xx >= 150, pk <= 512',
        'select s.xx from ce.s s join @R@ r on s.xx = r.pk where s.xx >= 150 and r.pk <= 512 order by s.xx desc'),
    (232, 'x >= 1500',
        'select s.x from ce.s s join @R@ r on s.x = r.pk where s.x >= 1500 order by s.x desc'),
    --   >= on inner
    (240, '1500 <= pk <= 5120',
        'select r.pk from ce.s s join @R@ r on s.x = r.pk where r.pk >= 1500 and r.pk <= 5120 order by r.pk desc'),
    (241, 'a >= 4800',
        'select r.a from ce.s s join @R@ r on s.x = r.a where r.a >= 4800 order by r.a desc'),
    (242, '1500 <= b <= 5120',
        'select r.b from ce.s s join @R@ r on s.x = r.b where r.b >= 1500 and r.b <= 5120 order by r.b desc'),
    -- forward scan, leading mismatches: the same inner bounds as 24x but read
    -- ascending, so the bound moves the merge's start instead of its end
    --   >= on inner
    (250, '1500 <= pk <= 5120 fwd',
        'select r.pk from ce.s s join @R@ r on s.x = r.pk where r.pk >= 1500 and r.pk <= 5120 order by r.pk'),
    (251, 'a >= 4800 fwd',
        'select r.a from ce.s s join @R@ r on s.x = r.a where r.a >= 4800 order by r.a'),
    (252, '1500 <= b <= 5120 fwd',
        'select r.b from ce.s s join @R@ r on s.x = r.b where r.b >= 1500 and r.b <= 5120 order by r.b'),
    -- #30738: same forward-scanned join, with LIMIT.  Two families:
    --   skip   -- "s.y >= 3836": the first 50 outer values (3836..3885) are all
    --             absent from r.a, then matches at y >= 3886.  NL walks the
    --             absent values with one cheap (empty) request each and stops as
    --             soon as the limit is met -- a few hundred rows scanned.  BNL
    --             pushes the limit into a tiny first value list that lands
    --             entirely in the absent range, gets an empty first scan, then
    --             falls back to a full batch and scans ~10k rows regardless of
    --             the limit -- far more than NL, which its startup cost charges.
    --   noskip -- "s.y >= 3886": matches from the very first row, so there is no
    --             leading mismatch.  Control: BNL's limit pushdown works, so BNL
    --             scans a few times NL's rows instead of tens of times.
    (301, '30738 skip limit 1',
        'select s.y from ce.s s join @R@ r on r.a = s.y where s.y >= 3836 order by s.y limit 1'),
    (302, '30738 skip limit 10',
        'select s.y from ce.s s join @R@ r on r.a = s.y where s.y >= 3836 order by s.y limit 10'),
    (303, '30738 skip limit 100',
        'select s.y from ce.s s join @R@ r on r.a = s.y where s.y >= 3836 order by s.y limit 100'),
    (304, '30738 skip limit 1000',
        'select s.y from ce.s s join @R@ r on r.a = s.y where s.y >= 3836 order by s.y limit 1000'),
    (305, '30738 noskip limit 1',
        'select s.y from ce.s s join @R@ r on r.a = s.y where s.y >= 3886 order by s.y limit 1'),
    (306, '30738 noskip limit 10',
        'select s.y from ce.s s join @R@ r on r.a = s.y where s.y >= 3886 order by s.y limit 10'),
    (307, '30738 noskip limit 100',
        'select s.y from ce.s s join @R@ r on r.a = s.y where s.y >= 3886 order by s.y limit 100'),
    (308, '30738 noskip limit 1000',
        'select s.y from ce.s s join @R@ r on r.a = s.y where s.y >= 3886 order by s.y limit 1000'),
    -- 5xx: the 1024-row shuffled outer of the 1xx rows joined on the
    -- non-unique key a, whose values (3886..5120) only 231 of the outer keys
    -- reach: the many-outer forms of 121-123, plus the left join.
    (501, 'a  inner z<=1024',
        'select s.id, r.d from ce.s s join @R@ r on r.a = s.y and r.e <= 600 where s.z <= 1024 order by s.id'),
    (502, 'a  semi z<=1024',
        'select s.id from ce.s s where s.z <= 1024 and exists (select 1 from @R@ r where r.a = s.y and r.e <= 600) order by s.id'),
    (503, 'a  anti z<=1024',
        'select s.id from ce.s s where s.z <= 1024 and not exists (select 1 from @R@ r where r.a = s.y and r.e <= 600) order by s.id'),
    (504, 'a  left z<=1024',
        'select s.id, r.d from ce.s s left join @R@ r on r.a = s.y and r.e <= 600 where s.z <= 1024 order by s.id')
)
select b.qid + r.qoff, b.descr || ' [' || r.rel || ']', replace(b.tmpl, '@R@', r.rel)
from base b, rels r
-- qid 203 (HJ-best control) is non-colocated only; see its comment above.
where not (b.qid = 203 and r.rel = 'ce.rC');

-- 6xx (#19549): the two-column key (g, h) of ce.r2, its colocated twin
-- ce.r2C and its hash-keyed twin ce.r2H, probed by the 1024 shuffled outer
-- rows of ce.s2, of which 26% have an inner row, and by the first 1024 ids,
-- which all have one.
insert into queries
with rels(rel, qoff) as (
    values ('ce.r2', 0), ('ce.r2C', 1000), ('ce.r2H', 2000)
),
base(qid, descr, tmpl) as (
    values
    (601, 'g,h inner z<=1024',
        'select s.id, r.d from ce.s2 s join @R@ r on r.g = s.g and r.h = s.h where s.z <= 1024 order by s.id'),
    (602, 'g,h inner id<=1024',
        'select s.id, r.d from ce.s2 s join @R@ r on r.g = s.g and r.h = s.h where s.id <= 1024 order by s.id'),
    (603, 'g,h semi  z<=1024',
        'select s.id from ce.s2 s where s.z <= 1024 and exists (select 1 from @R@ r where r.g = s.g and r.h = s.h) order by s.id'),
    (604, 'g,h left  z<=1024',
        'select s.id, r.d from ce.s2 s left join @R@ r on r.g = s.g and r.h = s.h where s.z <= 1024 order by s.id')
)
select b.qid + r.qoff, b.descr || ' [' || r.rel || ']', replace(b.tmpl, '@R@', r.rel)
from base b, rels r;

-- 4xx (#18943): DISTINCT joins whose inner can be a distinct index scan on
-- r.a (10 rows per value) or r.a5 (5 rows per value); flagged distinct_inner
-- so the '/p' methods (distinct pushdown off) run for them too.  401/402: the
-- shuffled 1024-row outer of the #30580 rows, one BNL batch of spread keys.
-- 403: a 200-row outer, small enough for the plain inner's rows to fit one
-- response like the skip scan's.  404/405: the outer driven by the y index
-- over the inner's whole key range, matching from the first row (405 is the
-- DISTINCT twin of 202).
insert into queries (qid, descr, query, distinct_inner)
with rels(rel, qoff) as (
    values ('ce.r', 0), ('ce.rC', 1000)
),
base(qid, descr, tmpl) as (
    values
    (401, 'distinct a  z<=1024',
        'select distinct s.y from ce.s s join @R@ r on r.a = s.y where s.z <= 1024 order by s.y'),
    (402, 'distinct a5 z<=1024',
        'select distinct s.y from ce.s s join @R@ r on r.a5 = s.y where s.z <= 1024 order by s.y'),
    (403, 'distinct a5 z<=200',
        'select distinct s.y from ce.s s join @R@ r on r.a5 = s.y where s.z <= 200 order by s.y'),
    (404, 'distinct a5 y<=2469',
        'select distinct s.y from ce.s s join @R@ r on r.a5 = s.y where s.y <= 2469 order by s.y'),
    (405, 'distinct a  y>=3886',
        'select distinct s.y from ce.s s join @R@ r on r.a = s.y where s.y >= 3886 order by s.y')
)
select b.qid + r.qoff, b.descr || ' [' || r.rel || ']', replace(b.tmpl, '@R@', r.rel), true
from base b, rels r;

select qid, query from queries order by qid;

------------------------------------------------------------------------------
-- Plan validation: every (query, method) pair must have realized the intended
-- join method (method_ok) with s outer and r inner (leading_ok).  Expect no
-- rows -- if any appear, the hints failed and the cost/run-time comparison
-- below would be meaningless.  join_plans only plans the queries (plain
-- EXPLAIN), so nothing is executed here.
------------------------------------------------------------------------------
select qid, label, join_node_type, outer_alias, inner_alias, method_ok, leading_ok
from join_plans
where not (method_ok and leading_ok)
order by qid, label;

------------------------------------------------------------------------------
-- The full report is produced twice: once at the default BNL batch size
-- (1024, set explicitly so the expected output does not depend on the
-- cluster default) and once at 512.  At 512 each query needs twice as many
-- batches, which roughly doubles BNL's per-batch inner work (and, for
-- #30580, splits the single-batch point-probe into two).
------------------------------------------------------------------------------
set yb_bnl_batch_size = 1024;
select current_setting('yb_bnl_batch_size') as yb_bnl_batch_size;

------------------------------------------------------------------------------
-- #30580 (fixed): the ntuples overcount used to cost BNL above HJ and MJ
-- (cost_rank 3) on every unique / semi / anti case despite BNL doing the
-- least work; cost_rank now agrees with rt_rank (BNL first) on every row,
-- so the section prints nothing.  "c inner" was the unaffected control: the
-- non-unique index hides the column's uniqueness, so no semi/anti/unique
-- costing applies.
--
-- These probe-bound plans are dominated by round trips, so rt_rank ranks by
-- read_reqs (then rows_scanned): BNL point-probes a whole batch per request,
-- MJ/HJ scan a wide swath / all of the inner, NL pays one request per outer
-- row.
--
-- The 1-outer rows pin the degenerate single-key probe, where BNL and NL do
-- identical work (every metric ties, seeks and nexts included).  The costs
-- tie as well, and the planner keeps NL, except on the c and "a" inner
-- joins, where BNL is costed below NL by less than 0.1; either order agrees
-- with the tie in work.
--
-- The left joins (...131-...147) rank like the inner joins on the same keys,
-- 1-outer forms included.  They are the controls of the 5xx section.
------------------------------------------------------------------------------
with m as (
    select qid, descr, label, total_cost, actual_rows,
           read_reqs, rows_scanned,
           table_reads, index_reads,
           rank() over (partition by qid order by total_cost, startup_cost) cost_rank,
           rank() over (partition by qid order by read_reqs, rows_scanned)  rt_rank
    from join_costs
    where qid % 1000 between 101 and 199
)
select qid, descr,
       rank_labels(array_agg(label order by cost_rank, label),
                   array_agg(cost_rank order by cost_rank, label))               cost_rank,
       chosen,
       rank_labels(array_agg(label order by rt_rank, label),
                   array_agg(rt_rank order by rt_rank, label))                   rt_rank,
       string_agg(cost_mask(total_cost), ' ' order by cost_rank, label)          total_cost,
       string_agg(round(read_reqs)::bigint::text, ' ' order by rt_rank, label)   read_reqs,
       string_agg(round(rows_scanned)::bigint::text, ' ' order by rt_rank, label) rows_scanned,
       string_agg(round(table_reads)::bigint::text, ' ' order by rt_rank, label) table_reads,
       string_agg(round(index_reads)::bigint::text, ' ' order by rt_rank, label) index_reads,
       max(actual_rows)                                                          actual_rows
from m join join_choice using (qid)
group by qid, descr, chosen
having not rank_consistent(array_agg(cost_rank order by label),
                           array_agg(rt_rank order by label))
order by qid;

------------------------------------------------------------------------------
-- #30565 (fixed): with leading empty batches (qid ...201) BNL must grind
-- through the empty batches before its first row (visible as extra read
-- requests and rows scanned against the no-empty companion, qid ...202), a
-- skip its startup cost used to leave out, so it looked the cheapest to
-- start while MJ did fewer round trips.  The skip is charged now: MJ leads
-- both rows by startup cost and total cost as it leads the work, and
-- neither row is printed.  rt_rank ranks by read_reqs (then rows_scanned),
-- the round-trip cost that dominates these plans.
--
-- qid ...203 (hj-best) is the positive control: HJ genuinely wins (cheapest
-- and joint-fewest read requests), and the cost model ranks it first too --
-- cost_rank and rt_rank agree, both led by HJ, so the row is not printed.
-- It confirms the misorderings above are specific to BNL, not noise in the
-- harness.
--
-- #33001 (qid ...210-...252): a range bound on either join key limits how much
-- of the other input the merge has to read.  On the ascending rows
-- (...210-...212 bound the outer, ...220-...222 the inner) MJ is costed first
-- throughout, and leads rt_rank as well except on ...210 and ...212, where BNL
-- issues the same number of requests and scans fewer rows -- it point-probes
-- the bounded outer in a single batch.  Those two rows print, and ...221,
-- where NL does the work of HJ yet is costed behind BNL.
--
-- ...230-...242 are the descending companions ("order by ... desc", planned as
-- backward index scans on both inputs).  Read descending, the bounds swap
-- roles: a lower bound stops the merge early and an upper bound moves its
-- start, and mergejoinscansel() folds them in the same way.  On ...240 and
-- ...242 the lower bound stops the merge early, so MJ scans 7717 rows to
-- HJ's 8741; counting that puts MJ ahead of HJ by cost, as by the work.
-- Costed as reading to the statistical extreme, the backward scans would
-- rank HJ first.  On ...230 and ...232 BNL leads both rankings.  The
-- descending rows all agree and none is printed.
--
-- ...250-...252 read the ...240-...242 bounds forward.  Ascending, a lower
-- bound moves the merge's start, not its end: the outer rows below it are read
-- and discarded before the first match, so MJ scans what HJ and BNL scan and
-- the bound buys no round trips (rt_rank ties them).  Descending, the same
-- bounds stop the merge early instead -- 7717 rows on ...240 against 8741
-- here.  The clamp charges the discarded rows to startup rather than claiming
-- a saving it does not get, and the rows agree and are not printed.
------------------------------------------------------------------------------
with m as (
    select qid, descr, label, startup_cost, total_cost, actual_rows,
           read_reqs, rows_scanned,
           rank() over (partition by qid order by total_cost, startup_cost) cost_rank,
           rank() over (partition by qid order by read_reqs, rows_scanned)  rt_rank
    from join_costs
    where qid % 1000 between 201 and 299
)
select qid, descr,
       rank_labels(array_agg(label order by cost_rank, label),
                   array_agg(cost_rank order by cost_rank, label))               cost_rank,
       chosen,
       rank_labels(array_agg(label order by rt_rank, label),
                   array_agg(rt_rank order by rt_rank, label))                   rt_rank,
       string_agg(cost_mask(startup_cost), ' ' order by cost_rank, label)        startup_cost,
       string_agg(cost_mask(total_cost), ' ' order by cost_rank, label)          total_cost,
       string_agg(round(read_reqs)::bigint::text, ' ' order by rt_rank, label)   read_reqs,
       string_agg(round(rows_scanned)::bigint::text, ' ' order by rt_rank, label) rows_scanned,
       max(actual_rows)                                                          actual_rows
from m join join_choice using (qid)
group by qid, descr, chosen
having not rank_consistent(array_agg(cost_rank order by label),
                           array_agg(rt_rank order by label))
order by qid;

------------------------------------------------------------------------------
-- #30738 (fixed): with LIMIT, the uncharged startup made BNL the cheapest
-- plan by (limit-adjusted) total cost although it scanned far more rows than
-- NL; MJ leads both orderings now.  Here the limit makes the plans
-- scan-bound, not round-trip-bound (NL's many requests are cheap empty
-- probes), so rt_rank ranks by rows_scanned (then read_reqs).  The "skip"
-- rows (...301-304) have a 50-row leading mismatch: BNL's full-batch fallback
-- scans about 10k rows against NL's 200 at LIMIT 100 and 4065 against 2000
-- at LIMIT 1000, where BNL is costed below NL; at LIMIT 1 and 10 BNL is
-- costed above NL as well, the orderings agree and the rows are not printed.
-- The "noskip" rows (...305-308) start matching at the first row -- the
-- control where BNL's limit pushdown works and BNL scans two to five times
-- NL's rows instead of fifty, yet is still costed below NL from LIMIT 10 up;
-- at LIMIT 1 the orderings agree as well.
------------------------------------------------------------------------------
with m as (
    select qid, descr, label, startup_cost, total_cost, actual_rows,
           read_reqs, rows_scanned,
           table_reads, index_reads,
           rank() over (partition by qid order by total_cost, startup_cost) cost_rank,
           rank() over (partition by qid order by rows_scanned, read_reqs)  rt_rank
    from join_costs
    where qid % 1000 between 301 and 399
)
select qid, descr,
       rank_labels(array_agg(label order by cost_rank, label),
                   array_agg(cost_rank order by cost_rank, label))               cost_rank,
       chosen,
       rank_labels(array_agg(label order by rt_rank, label),
                   array_agg(rt_rank order by rt_rank, label))                   rt_rank,
       string_agg(cost_mask(startup_cost), ' ' order by cost_rank, label)        startup_cost,
       string_agg(cost_mask(total_cost), ' ' order by cost_rank, label)          total_cost,
       string_agg(round(read_reqs)::bigint::text, ' ' order by rt_rank, label)   read_reqs,
       string_agg(round(rows_scanned)::bigint::text, ' ' order by rt_rank, label) rows_scanned,
       string_agg(round(table_reads)::bigint::text, ' ' order by rt_rank, label) table_reads,
       string_agg(round(index_reads)::bigint::text, ' ' order by rt_rank, label) index_reads,
       max(actual_rows)                                                          actual_rows
from m join join_choice using (qid)
group by qid, descr, chosen
having not rank_consistent(array_agg(cost_rank order by label),
                           array_agg(rt_rank order by label))
order by qid;

------------------------------------------------------------------------------
-- 4xx (#18943): DISTINCT joins whose inner is a distinct index scan.  Every
-- join method runs twice: with the skip-scan inner, and with distinct
-- pushdown off ('/p' labels), where the inner is the plain index scan and
-- the DISTINCT is resolved above the join.  Under BNL the skip scan carries
-- the batched IN-list (one group per matched value), under HJ/MJ it is the
-- full skip scan (a seek per group), under NL either a one-value probe or a
-- materialized full scan behind a join filter, whose per-row comparisons no
-- storage counter sees (rt_rank flatters that plan).
--
-- A skip scan does a seek per group against one scanned row per group, so
-- rows_scanned undercounts it; rt_rank here ranks by read_reqs and then by
-- work = nexts + seek_weight() * seeks (see the setup), with ties within
-- work_slack() so the layout flap cannot move a rank; work is printed masked
-- like the costs, one '#' per digit.  Column a has 10 rows per value and a5
-- has 5: at 5 a skip pays a seek and 3 nexts where the plain scan pays 5
-- nexts.  The rows that print are those where the flattered NL ties the
-- fastest plans by work while the cost model ranks it last (...401, ...405);
-- on a5 the orderings agree at batch size 1024, and at 512 ...402 prints,
-- where BNL/p needs two batches and falls behind HJ and MJ by work but not
-- by cost.
------------------------------------------------------------------------------
select qid, descr,
       rank_labels(array_agg(label order by cost_rank, label),
                   array_agg(cost_rank order by cost_rank, label))               cost_rank,
       chosen,
       rank_labels(array_agg(label order by rt_rank, label),
                   array_agg(rt_rank order by rt_rank, label))                   rt_rank,
       string_agg(cost_mask(total_cost), ' ' order by cost_rank, label)          total_cost,
       string_agg(round(read_reqs)::bigint::text, ' ' order by rt_rank, label)   read_reqs,
       string_agg(round(rows_scanned)::bigint::text, ' ' order by rt_rank, label) rows_scanned,
       string_agg(cost_mask(work), ' ' order by rt_rank, label)                  work,
       max(actual_rows)                                                          actual_rows
from distinct_join_ranks join join_choice using (qid)
group by qid, descr, chosen
having not rank_consistent(array_agg(cost_rank order by label),
                           array_agg(rt_rank order by label))
order by qid;

------------------------------------------------------------------------------
-- 5xx: the 1024 shuffled outer keys joined on the non-unique key a, which has
-- 10 rows for each value in 3886..5120, so only 231 of the keys have rows.
-- BNL is costed 2.2 to 2.8 times HJ for the inner, semi, anti and left joins
-- alike, while it does no more work.  It scans 6668 to 9740 rows against
-- HJ's and MJ's 14393, and makes 3 or 4 requests against their 7 on ce.rC
-- and at most one more than theirs on ce.r.  The cost comes from BNL's inner
-- scan, estimated as if every key of the batch had its 10 rows: 4907 rows
-- against 1131 per batch on ce.rC.  rt_rank
-- ranks by read_reqs (then rows_scanned), as in the 1xx section, so cost_rank
-- disagrees with it on ce.rC and on the ce.r semi and anti joins.  On the
-- ce.r inner and left joins BNL's extra request ranks it behind HJ and MJ by
-- work as well, though not by the 2.6 times its cost says, so those rows are
-- not printed; at batch size 512 the ce.r semi and anti joins agree too.  The
-- left join ranks like the inner join, as the 1xx left joins do on the unique
-- keys.
------------------------------------------------------------------------------
with m as (
    select qid, descr, label, total_cost, actual_rows,
           read_reqs, rows_scanned,
           table_reads, index_reads,
           rank() over (partition by qid order by total_cost, startup_cost) cost_rank,
           rank() over (partition by qid order by read_reqs, rows_scanned)  rt_rank
    from join_costs
    where qid % 1000 between 501 and 599
)
select qid, descr,
       rank_labels(array_agg(label order by cost_rank, label),
                   array_agg(cost_rank order by cost_rank, label))               cost_rank,
       chosen,
       rank_labels(array_agg(label order by rt_rank, label),
                   array_agg(rt_rank order by rt_rank, label))                   rt_rank,
       string_agg(cost_mask(total_cost), ' ' order by cost_rank, label)          total_cost,
       string_agg(round(read_reqs)::bigint::text, ' ' order by rt_rank, label)   read_reqs,
       string_agg(round(rows_scanned)::bigint::text, ' ' order by rt_rank, label) rows_scanned,
       string_agg(round(table_reads)::bigint::text, ' ' order by rt_rank, label) table_reads,
       string_agg(round(index_reads)::bigint::text, ' ' order by rt_rank, label) index_reads,
       max(actual_rows)                                                          actual_rows
from m join join_choice using (qid)
group by qid, descr, chosen
having not rank_consistent(array_agg(cost_rank order by label),
                           array_agg(rt_rank order by label))
order by qid;

------------------------------------------------------------------------------
-- 6xx (#19549): the 1024 shuffled outer rows of ce.s2 joined on the
-- two-column key (g, h), which 265 of them reach, and the first 1024 ids,
-- which all do.  The inner scan's estimate applies the batch size once per
-- key column, so a batch of 16 pairs is estimated at 256 rows and a batch
-- of 128 or more at the whole 16384-row inner.  With one batch of 1024
-- that is about what HJ and MJ scan, and BNL, which point-probes the batch
-- in 2 or 3 requests against their 7 to 18, is costed and ranked first, so
-- the first pass prints nothing.
-- With two batches of 512 (second pass) the estimate doubles while BNL's
-- work grows by one request, and BNL falls to third by cost behind MJ and
-- HJ on ce.r2 and ce.r2C and to second behind HJ on ce.r2H, still first by
-- work.  rt_rank ranks by read_reqs (then rows_scanned), as in the 1xx
-- section.
------------------------------------------------------------------------------
with m as (
    select qid, descr, label, total_cost, actual_rows,
           read_reqs, rows_scanned,
           table_reads, index_reads,
           rank() over (partition by qid order by total_cost, startup_cost) cost_rank,
           rank() over (partition by qid order by read_reqs, rows_scanned)  rt_rank
    from join_costs
    where qid % 1000 between 601 and 699
)
select qid, descr,
       rank_labels(array_agg(label order by cost_rank, label),
                   array_agg(cost_rank order by cost_rank, label))               cost_rank,
       chosen,
       rank_labels(array_agg(label order by rt_rank, label),
                   array_agg(rt_rank order by rt_rank, label))                   rt_rank,
       string_agg(cost_mask(total_cost), ' ' order by cost_rank, label)          total_cost,
       string_agg(round(read_reqs)::bigint::text, ' ' order by rt_rank, label)   read_reqs,
       string_agg(round(rows_scanned)::bigint::text, ' ' order by rt_rank, label) rows_scanned,
       string_agg(round(table_reads)::bigint::text, ' ' order by rt_rank, label) table_reads,
       string_agg(round(index_reads)::bigint::text, ' ' order by rt_rank, label) index_reads,
       max(actual_rows)                                                          actual_rows
from m join join_choice using (qid)
group by qid, descr, chosen
having not rank_consistent(array_agg(cost_rank order by label),
                           array_agg(rt_rank order by label))
order by qid;

------------------------------------------------------------------------------
-- Second pass: identical report at yb_bnl_batch_size = 512.  Compare the BNL
-- column against the 1024 pass.
------------------------------------------------------------------------------
set yb_bnl_batch_size = 512;
select current_setting('yb_bnl_batch_size') as yb_bnl_batch_size;

-- #30580 at batch size 512 (see the 1024 pass above for the explanation).
with m as (
    select qid, descr, label, total_cost, actual_rows,
           read_reqs, rows_scanned,
           table_reads, index_reads,
           rank() over (partition by qid order by total_cost, startup_cost) cost_rank,
           rank() over (partition by qid order by read_reqs, rows_scanned)  rt_rank
    from join_costs
    where qid % 1000 between 101 and 199
)
select qid, descr,
       rank_labels(array_agg(label order by cost_rank, label),
                   array_agg(cost_rank order by cost_rank, label))               cost_rank,
       chosen,
       rank_labels(array_agg(label order by rt_rank, label),
                   array_agg(rt_rank order by rt_rank, label))                   rt_rank,
       string_agg(cost_mask(total_cost), ' ' order by cost_rank, label)          total_cost,
       string_agg(round(read_reqs)::bigint::text, ' ' order by rt_rank, label)   read_reqs,
       string_agg(round(rows_scanned)::bigint::text, ' ' order by rt_rank, label) rows_scanned,
       string_agg(round(table_reads)::bigint::text, ' ' order by rt_rank, label) table_reads,
       string_agg(round(index_reads)::bigint::text, ' ' order by rt_rank, label) index_reads,
       max(actual_rows)                                                          actual_rows
from m join join_choice using (qid)
group by qid, descr, chosen
having not rank_consistent(array_agg(cost_rank order by label),
                           array_agg(rt_rank order by label))
order by qid;

-- #30565 at batch size 512.
with m as (
    select qid, descr, label, startup_cost, total_cost, actual_rows,
           read_reqs, rows_scanned,
           rank() over (partition by qid order by total_cost, startup_cost) cost_rank,
           rank() over (partition by qid order by read_reqs, rows_scanned)  rt_rank
    from join_costs
    where qid % 1000 between 201 and 299
)
select qid, descr,
       rank_labels(array_agg(label order by cost_rank, label),
                   array_agg(cost_rank order by cost_rank, label))               cost_rank,
       chosen,
       rank_labels(array_agg(label order by rt_rank, label),
                   array_agg(rt_rank order by rt_rank, label))                   rt_rank,
       string_agg(cost_mask(startup_cost), ' ' order by cost_rank, label)        startup_cost,
       string_agg(cost_mask(total_cost), ' ' order by cost_rank, label)          total_cost,
       string_agg(round(read_reqs)::bigint::text, ' ' order by rt_rank, label)   read_reqs,
       string_agg(round(rows_scanned)::bigint::text, ' ' order by rt_rank, label) rows_scanned,
       max(actual_rows)                                                          actual_rows
from m join join_choice using (qid)
group by qid, descr, chosen
having not rank_consistent(array_agg(cost_rank order by label),
                           array_agg(rt_rank order by label))
order by qid;

-- #30738 at batch size 512.
with m as (
    select qid, descr, label, startup_cost, total_cost, actual_rows,
           read_reqs, rows_scanned,
           table_reads, index_reads,
           rank() over (partition by qid order by total_cost, startup_cost) cost_rank,
           rank() over (partition by qid order by rows_scanned, read_reqs)  rt_rank
    from join_costs
    where qid % 1000 between 301 and 399
)
select qid, descr,
       rank_labels(array_agg(label order by cost_rank, label),
                   array_agg(cost_rank order by cost_rank, label))               cost_rank,
       chosen,
       rank_labels(array_agg(label order by rt_rank, label),
                   array_agg(rt_rank order by rt_rank, label))                   rt_rank,
       string_agg(cost_mask(startup_cost), ' ' order by cost_rank, label)        startup_cost,
       string_agg(cost_mask(total_cost), ' ' order by cost_rank, label)          total_cost,
       string_agg(round(read_reqs)::bigint::text, ' ' order by rt_rank, label)   read_reqs,
       string_agg(round(rows_scanned)::bigint::text, ' ' order by rt_rank, label) rows_scanned,
       string_agg(round(table_reads)::bigint::text, ' ' order by rt_rank, label) table_reads,
       string_agg(round(index_reads)::bigint::text, ' ' order by rt_rank, label) index_reads,
       max(actual_rows)                                                          actual_rows
from m join join_choice using (qid)
group by qid, descr, chosen
having not rank_consistent(array_agg(cost_rank order by label),
                           array_agg(rt_rank order by label))
order by qid;

-- 4xx at batch size 512.
select qid, descr,
       rank_labels(array_agg(label order by cost_rank, label),
                   array_agg(cost_rank order by cost_rank, label))               cost_rank,
       chosen,
       rank_labels(array_agg(label order by rt_rank, label),
                   array_agg(rt_rank order by rt_rank, label))                   rt_rank,
       string_agg(cost_mask(total_cost), ' ' order by cost_rank, label)          total_cost,
       string_agg(round(read_reqs)::bigint::text, ' ' order by rt_rank, label)   read_reqs,
       string_agg(round(rows_scanned)::bigint::text, ' ' order by rt_rank, label) rows_scanned,
       string_agg(cost_mask(work), ' ' order by rt_rank, label)                  work,
       max(actual_rows)                                                          actual_rows
from distinct_join_ranks join join_choice using (qid)
group by qid, descr, chosen
having not rank_consistent(array_agg(cost_rank order by label),
                           array_agg(rt_rank order by label))
order by qid;

-- 5xx at batch size 512.
with m as (
    select qid, descr, label, total_cost, actual_rows,
           read_reqs, rows_scanned,
           table_reads, index_reads,
           rank() over (partition by qid order by total_cost, startup_cost) cost_rank,
           rank() over (partition by qid order by read_reqs, rows_scanned)  rt_rank
    from join_costs
    where qid % 1000 between 501 and 599
)
select qid, descr,
       rank_labels(array_agg(label order by cost_rank, label),
                   array_agg(cost_rank order by cost_rank, label))               cost_rank,
       chosen,
       rank_labels(array_agg(label order by rt_rank, label),
                   array_agg(rt_rank order by rt_rank, label))                   rt_rank,
       string_agg(cost_mask(total_cost), ' ' order by cost_rank, label)          total_cost,
       string_agg(round(read_reqs)::bigint::text, ' ' order by rt_rank, label)   read_reqs,
       string_agg(round(rows_scanned)::bigint::text, ' ' order by rt_rank, label) rows_scanned,
       string_agg(round(table_reads)::bigint::text, ' ' order by rt_rank, label) table_reads,
       string_agg(round(index_reads)::bigint::text, ' ' order by rt_rank, label) index_reads,
       max(actual_rows)                                                          actual_rows
from m join join_choice using (qid)
group by qid, descr, chosen
having not rank_consistent(array_agg(cost_rank order by label),
                           array_agg(rt_rank order by label))
order by qid;

-- 6xx at batch size 512.
with m as (
    select qid, descr, label, total_cost, actual_rows,
           read_reqs, rows_scanned,
           table_reads, index_reads,
           rank() over (partition by qid order by total_cost, startup_cost) cost_rank,
           rank() over (partition by qid order by read_reqs, rows_scanned)  rt_rank
    from join_costs
    where qid % 1000 between 601 and 699
)
select qid, descr,
       rank_labels(array_agg(label order by cost_rank, label),
                   array_agg(cost_rank order by cost_rank, label))               cost_rank,
       chosen,
       rank_labels(array_agg(label order by rt_rank, label),
                   array_agg(rt_rank order by rt_rank, label))                   rt_rank,
       string_agg(cost_mask(total_cost), ' ' order by cost_rank, label)          total_cost,
       string_agg(round(read_reqs)::bigint::text, ' ' order by rt_rank, label)   read_reqs,
       string_agg(round(rows_scanned)::bigint::text, ' ' order by rt_rank, label) rows_scanned,
       string_agg(round(table_reads)::bigint::text, ' ' order by rt_rank, label) table_reads,
       string_agg(round(index_reads)::bigint::text, ' ' order by rt_rank, label) index_reads,
       max(actual_rows)                                                          actual_rows
from m join join_choice using (qid)
group by qid, descr, chosen
having not rank_consistent(array_agg(cost_rank order by label),
                           array_agg(rt_rank order by label))
order by qid;

reset yb_bnl_batch_size;
