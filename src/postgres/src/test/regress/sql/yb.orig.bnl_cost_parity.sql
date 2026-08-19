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
-- front.  All four methods return the same rows, so actual_rows must agree per
-- query.
--
-- Each query prints the deterministic run-time work every plan actually did
-- (EXPLAIN ANALYZE, DEBUG, DIST, SUMMARY ON):
--   read_reqs     query-level "Storage Read Requests" (DocDB network round
--                 trips -- the dominant latency factor for probe-bound plans)
--   rows_scanned  query-level "Storage Rows Scanned" (dominant for the
--                 scan-bound LIMIT plans, where round trips are cheap)
--   seeks         query-level rocksdb seeks
--   nexts         query-level rocksdb iterator next + prev steps
--   table_reads,
--   index_reads   read requests split by side (sum over scan nodes)
-- Wall-clock timings vary run to run and are NOT printed; the predicates are
-- chosen so this work is well separated, so it tracks the real (measured)
-- execution-time ordering.
--
-- The output is COLLAPSED to one row per query, with two method orderings:
--   cost_rank  the four labels ordered by the planner's (total cost, startup
--              cost) -- the order CBO would prefer (cheapest first).
--   rt_rank    the four labels ordered by the deterministic work that dominates
--              that query's run time: read_reqs for the probe-bound joins
--              (#30580, #30565), rows_scanned for the scan-bound LIMIT joins
--              (#30738) -- i.e. fastest first.  (Noted per section.)
-- The cost columns (startup_cost, total_cost) list their four values in
-- cost_rank order (lined up with the cost_rank labels); the execution-work
-- columns (read_reqs, rows_scanned, seeks, nexts, table_reads, index_reads)
-- list theirs in rt_rank order (lined up with the rt_rank labels, fastest
-- first).  When cost_rank and rt_rank list the methods in a different order,
-- the cost model is mis-ranking the plans -- the whole point of these tests.
--
-- Cost columns (startup_cost, total_cost) are masked to one '#' per integer
-- digit (fraction dropped): this shows each plan's relative magnitude and the
-- ranking while keeping the golden stable across the small cost-model changes
-- that the fixes for these bugs will produce.
--
-- The tests EXPOSE three known BNL cost-model defects.  They document today's
-- (buggy) numbers; the costs are expected to move once the fixes land.
--
--   #30580  BNL cost too high vs MJ/HJ/NL when the inner key is unique or the
--           join is a semi/anti join.  The per-batch inner tuples are counted
--           without dividing by the batch size, so BNL is costed far above HJ
--           and MJ (cost_rank 3) even though it does the least work and is the
--           fastest plan (rt_rank 1).
--
--   #30565  BNL omits the cost of skipping the leading all-empty batches from
--           its startup cost.  With the outer sorted on a non-unique indexed
--           column and the first 2-3 batches matching nothing, BNL's startup
--           cost is identical to the no-skip case (and far below the merge
--           join's, which does charge the skip) -- so BNL is not the fastest
--           plan yet looks the cheapest to start.
--
--   #30738  With LIMIT, #30565's undercosted startup makes BNL the cheapest
--           plan by (limit-adjusted) total cost (cost_rank 1), while at run
--           time it scans far more rows than the un-batched NL and is slower
--           (rt_rank worse than NL's).  The execution-time limit pushdown
--           shrinks the first value list to values that are all absent from the
--           inner, gets an empty first scan, then falls back to a full batch
--           that scans far more rows than the LIMIT needs.
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
    -- #30565: outer ce.s scanned by the non-unique indexed column y FORWARD
    -- (ascending -- no backward index scan), joined to the non-unique inner key
    -- r.a (values 3886..5120, the top of s.y's range).  Rows with y < 3886
    -- match nothing, so with "s.y >= 1621" the first ~2 batches (y in
    -- 1621..3885) are empty before the matches at y >= 3886.  The "no-empty"
    -- companion (s.y >= 3886) matches from the first row; BNL's startup cost is
    -- identical in both -- the missing empty-batch cost.
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
    -- #30738: same forward-scanned join, with LIMIT.  Two families:
    --   skip   -- "s.y >= 3836": the first 50 outer values (3836..3885) are all
    --             absent from r.a, then matches at y >= 3886.  NL walks the
    --             absent values with one cheap (empty) request each and stops as
    --             soon as the limit is met -- a few hundred rows scanned.  BNL
    --             pushes the limit into a tiny first value list that lands
    --             entirely in the absent range, gets an empty first scan, then
    --             falls back to a full batch and scans ~10k rows regardless of
    --             the limit -- far more than NL.  This is the bug.
    --   noskip -- "s.y >= 3886": matches from the very first row, so there is no
    --             leading mismatch.  Control: BNL's limit pushdown works and it
    --             does not blow up, so BNL is no longer pathological vs NL.
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
        'select s.y from ce.s s join @R@ r on r.a = s.y where s.y >= 3886 order by s.y limit 1000')
)
select b.qid + r.qoff, b.descr || ' [' || r.rel || ']', replace(b.tmpl, '@R@', r.rel)
from base b, rels r
-- qid 203 (HJ-best control) is non-colocated only; see its comment above.
where not (b.qid = 203 and r.rel = 'ce.rC');

select qid, query from queries order by qid;

------------------------------------------------------------------------------
-- Plan validation: every (query, method) pair must have realized the intended
-- join method (method_ok) with s outer and r inner (leading_ok).  Expect no
-- rows -- if any appear, the hints failed and the cost/run-time comparison
-- below would be meaningless.
------------------------------------------------------------------------------
select qid, label, join_node_type, outer_alias, inner_alias, method_ok, leading_ok
from join_costs
where not (method_ok and leading_ok)
order by qid, label;

------------------------------------------------------------------------------
-- The full report is produced twice: once at the default BNL batch size
-- (1024, set explicitly so the golden does not depend on the cluster default)
-- and once at 512.  Only the BNL plan is affected by the batch size (the NL
-- hint pins its own size of 1; HJ/MJ ignore it), so the HJ/MJ/NL columns are
-- the same in both passes -- the BNL column is what moves.  At 512 each query
-- needs twice as many batches, which roughly doubles BNL's per-batch inner
-- work (and, for #30580, splits the single-batch point-probe into two).
------------------------------------------------------------------------------
set yb_bnl_batch_size = 1024;
select current_setting('yb_bnl_batch_size') as yb_bnl_batch_size;

------------------------------------------------------------------------------
-- #30580: BNL is costed well above HJ and MJ (cost_rank 3, below only NL) on
-- every unique / semi / anti case, yet it does the least work and is the
-- fastest plan (rt_rank 1).  The lone exception is "c inner", where the
-- non-unique index hides the column's uniqueness so BNL is costed normally.
--
-- These probe-bound plans are dominated by DocDB read requests (network round
-- trips), so rt_rank ranks by read_reqs (then rows_scanned).  BNL point-probes
-- ~100 keys in one batched request; MJ/HJ scan a wide range / all of the inner;
-- NL pays one request per outer row.  The ranking matches measured wall-clock.
-- A correct cost model would rank BNL near HJ/MJ by cost too.
------------------------------------------------------------------------------
with m as (
    select qid, descr, label, total_cost, actual_rows,
           read_reqs, rows_scanned, seeks, nexts,
           table_reads, index_reads,
           rank() over (partition by qid order by total_cost, startup_cost) cost_rank,
           rank() over (partition by qid order by read_reqs, rows_scanned)  rt_rank
    from join_costs
    where qid % 1000 between 101 and 199
)
select qid, descr,
       string_agg(label, ' ' order by cost_rank, label)                          cost_rank,
       string_agg(label, ' ' order by rt_rank, label)                            rt_rank,
       string_agg(cost_mask(total_cost), ' ' order by cost_rank, label)          total_cost,
       string_agg(round(read_reqs)::bigint::text, ' ' order by rt_rank, label)   read_reqs,
       string_agg(round(rows_scanned)::bigint::text, ' ' order by rt_rank, label) rows_scanned,
       string_agg(round(seeks)::bigint::text, ' ' order by rt_rank, label)       seeks,
       string_agg(round(nexts)::bigint::text, ' ' order by rt_rank, label)       nexts,
       string_agg(round(table_reads)::bigint::text, ' ' order by rt_rank, label) table_reads,
       string_agg(round(index_reads)::bigint::text, ' ' order by rt_rank, label) index_reads,
       max(actual_rows)                                                          actual_rows
from m
group by qid, descr
order by qid;

------------------------------------------------------------------------------
-- #30565: with leading empty batches (qid ...201) BNL's startup cost is
-- identical to the no-empty companion (qid ...202) and far below the merge
-- join's startup cost, even though BNL must grind through the empty batches
-- (visible as extra read requests / rows scanned vs the no-empty case).  The
-- startup cost should reflect that skip but does not -- so BNL looks the
-- cheapest to start (lowest startup_cost) while it is NOT the fastest plan
-- (rt_rank > 1; MJ does fewer round trips).  rt_rank ranks by read_reqs (then
-- rows_scanned), the round-trip cost that dominates these plans.
--
-- qid ...203 (hj-best) is the positive control: HJ genuinely wins (cheapest
-- and joint-fewest read requests), and the cost model ranks it first too --
-- cost_rank and rt_rank agree, both led by HJ.  It confirms the misorderings
-- above are specific to BNL, not noise in the harness.
------------------------------------------------------------------------------
with m as (
    select qid, descr, label, startup_cost, total_cost, actual_rows,
           read_reqs, rows_scanned, seeks, nexts,
           rank() over (partition by qid order by total_cost, startup_cost) cost_rank,
           rank() over (partition by qid order by read_reqs, rows_scanned)  rt_rank
    from join_costs
    where qid % 1000 between 201 and 299
)
select qid, descr,
       string_agg(label, ' ' order by cost_rank, label)                          cost_rank,
       string_agg(label, ' ' order by rt_rank, label)                            rt_rank,
       string_agg(cost_mask(startup_cost), ' ' order by cost_rank, label)        startup_cost,
       string_agg(cost_mask(total_cost), ' ' order by cost_rank, label)          total_cost,
       string_agg(round(read_reqs)::bigint::text, ' ' order by rt_rank, label)   read_reqs,
       string_agg(round(rows_scanned)::bigint::text, ' ' order by rt_rank, label) rows_scanned,
       string_agg(round(seeks)::bigint::text, ' ' order by rt_rank, label)       seeks,
       string_agg(round(nexts)::bigint::text, ' ' order by rt_rank, label)       nexts,
       max(actual_rows)                                                          actual_rows
from m
group by qid, descr
order by qid;

------------------------------------------------------------------------------
-- #30738: with LIMIT, the undercosted startup makes BNL the cheapest plan by
-- (limit-adjusted) total cost (cost_rank 1), although at run time it scans far
-- more rows than NL and is slower.  Here the limit makes the plans scan-bound,
-- not round-trip-bound (NL's many requests are cheap empty probes), so rt_rank
-- ranks by rows_scanned (then read_reqs): BNL's full-batch fallback scans ~10k
-- rows vs NL's few hundred, so BNL ranks worse than NL (rt_rank > NL's) despite
-- being costed the cheapest.  The "skip" rows (...301-304) have a 50-row
-- leading mismatch and show the bug; the "noskip" rows (...305-308) start
-- matching at the first row -- the control where BNL's limit pushdown works and
-- BNL no longer scans more than NL.
------------------------------------------------------------------------------
with m as (
    select qid, descr, label, startup_cost, total_cost, actual_rows,
           read_reqs, rows_scanned, seeks, nexts,
           table_reads, index_reads,
           rank() over (partition by qid order by total_cost, startup_cost) cost_rank,
           rank() over (partition by qid order by rows_scanned, read_reqs)  rt_rank
    from join_costs
    where qid % 1000 between 301 and 399
)
select qid, descr,
       string_agg(label, ' ' order by cost_rank, label)                          cost_rank,
       string_agg(label, ' ' order by rt_rank, label)                            rt_rank,
       string_agg(cost_mask(startup_cost), ' ' order by cost_rank, label)        startup_cost,
       string_agg(cost_mask(total_cost), ' ' order by cost_rank, label)          total_cost,
       string_agg(round(read_reqs)::bigint::text, ' ' order by rt_rank, label)   read_reqs,
       string_agg(round(rows_scanned)::bigint::text, ' ' order by rt_rank, label) rows_scanned,
       string_agg(round(seeks)::bigint::text, ' ' order by rt_rank, label)       seeks,
       string_agg(round(nexts)::bigint::text, ' ' order by rt_rank, label)       nexts,
       string_agg(round(table_reads)::bigint::text, ' ' order by rt_rank, label) table_reads,
       string_agg(round(index_reads)::bigint::text, ' ' order by rt_rank, label) index_reads,
       max(actual_rows)                                                          actual_rows
from m
group by qid, descr
order by qid;

------------------------------------------------------------------------------
-- Second pass: identical report at yb_bnl_batch_size = 512.  HJ/MJ/NL are
-- unaffected (same numbers); compare the BNL column against the 1024 pass.
------------------------------------------------------------------------------
set yb_bnl_batch_size = 512;
select current_setting('yb_bnl_batch_size') as yb_bnl_batch_size;

-- #30580 at batch size 512 (see the 1024 pass above for the explanation).
with m as (
    select qid, descr, label, total_cost, actual_rows,
           read_reqs, rows_scanned, seeks, nexts,
           table_reads, index_reads,
           rank() over (partition by qid order by total_cost, startup_cost) cost_rank,
           rank() over (partition by qid order by read_reqs, rows_scanned)  rt_rank
    from join_costs
    where qid % 1000 between 101 and 199
)
select qid, descr,
       string_agg(label, ' ' order by cost_rank, label)                          cost_rank,
       string_agg(label, ' ' order by rt_rank, label)                            rt_rank,
       string_agg(cost_mask(total_cost), ' ' order by cost_rank, label)          total_cost,
       string_agg(round(read_reqs)::bigint::text, ' ' order by rt_rank, label)   read_reqs,
       string_agg(round(rows_scanned)::bigint::text, ' ' order by rt_rank, label) rows_scanned,
       string_agg(round(seeks)::bigint::text, ' ' order by rt_rank, label)       seeks,
       string_agg(round(nexts)::bigint::text, ' ' order by rt_rank, label)       nexts,
       string_agg(round(table_reads)::bigint::text, ' ' order by rt_rank, label) table_reads,
       string_agg(round(index_reads)::bigint::text, ' ' order by rt_rank, label) index_reads,
       max(actual_rows)                                                          actual_rows
from m
group by qid, descr
order by qid;

-- #30565 at batch size 512.
with m as (
    select qid, descr, label, startup_cost, total_cost, actual_rows,
           read_reqs, rows_scanned, seeks, nexts,
           rank() over (partition by qid order by total_cost, startup_cost) cost_rank,
           rank() over (partition by qid order by read_reqs, rows_scanned)  rt_rank
    from join_costs
    where qid % 1000 between 201 and 299
)
select qid, descr,
       string_agg(label, ' ' order by cost_rank, label)                          cost_rank,
       string_agg(label, ' ' order by rt_rank, label)                            rt_rank,
       string_agg(cost_mask(startup_cost), ' ' order by cost_rank, label)        startup_cost,
       string_agg(cost_mask(total_cost), ' ' order by cost_rank, label)          total_cost,
       string_agg(round(read_reqs)::bigint::text, ' ' order by rt_rank, label)   read_reqs,
       string_agg(round(rows_scanned)::bigint::text, ' ' order by rt_rank, label) rows_scanned,
       string_agg(round(seeks)::bigint::text, ' ' order by rt_rank, label)       seeks,
       string_agg(round(nexts)::bigint::text, ' ' order by rt_rank, label)       nexts,
       max(actual_rows)                                                          actual_rows
from m
group by qid, descr
order by qid;

-- #30738 at batch size 512.
with m as (
    select qid, descr, label, startup_cost, total_cost, actual_rows,
           read_reqs, rows_scanned, seeks, nexts,
           table_reads, index_reads,
           rank() over (partition by qid order by total_cost, startup_cost) cost_rank,
           rank() over (partition by qid order by rows_scanned, read_reqs)  rt_rank
    from join_costs
    where qid % 1000 between 301 and 399
)
select qid, descr,
       string_agg(label, ' ' order by cost_rank, label)                          cost_rank,
       string_agg(label, ' ' order by rt_rank, label)                            rt_rank,
       string_agg(cost_mask(startup_cost), ' ' order by cost_rank, label)        startup_cost,
       string_agg(cost_mask(total_cost), ' ' order by cost_rank, label)          total_cost,
       string_agg(round(read_reqs)::bigint::text, ' ' order by rt_rank, label)   read_reqs,
       string_agg(round(rows_scanned)::bigint::text, ' ' order by rt_rank, label) rows_scanned,
       string_agg(round(seeks)::bigint::text, ' ' order by rt_rank, label)       seeks,
       string_agg(round(nexts)::bigint::text, ' ' order by rt_rank, label)       nexts,
       string_agg(round(table_reads)::bigint::text, ' ' order by rt_rank, label) table_reads,
       string_agg(round(index_reads)::bigint::text, ' ' order by rt_rank, label) index_reads,
       max(actual_rows)                                                          actual_rows
from m
group by qid, descr
order by qid;

reset yb_bnl_batch_size;
