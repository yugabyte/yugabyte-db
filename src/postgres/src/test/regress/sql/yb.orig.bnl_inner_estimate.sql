-- Test the planner's per-batch estimates for the inner index scan of a YB
-- Batched Nested Loop (BNL)
-- DEPENDENCY: yb.orig.bnl_cost_parity_setup
\c bnl_cost_parity_test

delete from bnl_inner_queries;

-- The row estimate of a batched inner scan is the batch size times the rows
-- of one key: eqsel multiplies the key's equality selectivity by the batch
-- size, so every key of a batch counts as present, and the batched columns of
-- a multi-column key share one batch size, at least one row per key.  The
-- data here meets those assumptions, so the row estimates hold and the
-- request, seek and next verdicts stand on their own: every outer key has
-- inner rows and the outer keys are unique.  Where presence fails, only a
-- join's ranking checks the estimate: yb.orig.bnl_cost_parity's 5xx and 6xx
-- sections join keys most of which have no inner rows.
--
-- QID categories (last digit: batch size 1 = 1024, 2 = 128, 3 = 16):
--   1xxxx: one-column key, inner ce.r (+1000 for the colocated ce.rC): a with
--          10 rows per value, the unique primary key pk and the unique
--          secondary index on b
--   2xxxx: two-column key (g, h) whose g follows from h, inner ce.r3 (+1000
--          for the colocated ce.r3C, +2000 for the hash-keyed ce.r3H)
--   3xxxx: two-column key (g, h) over independent columns, each with more
--          values than a batch of 128, inner ce.r2 (+1000 for the colocated
--          ce.r2C, +2000 for the hash-keyed ce.r2H)
-- Each outer is 1024 rows, so every batch is full, in the shuffled order of
-- z or in key order.
insert into bnl_inner_queries
with rels(rel, qidrel) as (
    values ('ce.r', 10000), ('ce.rC', 11000)
),
keys(qidkey, descr, query) as (
    values
    -- r.a has 10 rows per value over 3886..5120.  4291 is the 1024th smallest
    -- z among the outer rows whose y lies in that range, so z <= 4291 with
    -- y >= 3886 is 1024 present keys in shuffled order, and x in 3886..4909
    -- the same number of keys in key order.  ...30 adds the inner filter of
    -- yb.orig.bnl_cost_parity's 5xx rows.
    (10, 'a  shuffled present',
        'select s.id, r.d from ce.s s join @R@ r on r.a = s.y where s.z <= 4291 and s.y >= 3886'),
    (20, 'a  sorted 3886..4909',
        'select s.id, r.d from ce.s s join @R@ r on r.a = s.y where s.x between 3886 and 4909'),
    (30, 'a  shuffled present e<=600',
        'select s.id, r.d from ce.s s join @R@ r on r.a = s.y and r.e <= 600 where s.z <= 4291 and s.y >= 3886'),
    -- The unique primary key over 1..12345 and the unique secondary index on
    -- b, equal to pk: every outer key has its row.
    (40, 'pk shuffled z<=1024',
        'select s.id, r.d from ce.s s join @R@ r on r.pk = s.y where s.z <= 1024'),
    (50, 'pk sorted x<=1024',
        'select s.id, r.d from ce.s s join @R@ r on r.pk = s.y where s.x <= 1024'),
    (60, 'b  shuffled z<=1024',
        'select s.id, r.d from ce.s s join @R@ r on r.b = s.x where s.z <= 1024')
),
sizes(qidbs, bs) as (
    values (1, 1024), (2, 128), (3, 16)
)
select r.qidrel + k.qidkey + b.qidbs, b.bs, k.descr || ' [' || r.rel || ']',
       replace(k.query, '@R@', r.rel)
from rels r, keys k, sizes b;

insert into bnl_inner_queries
with rels(rel, qidrel) as (
    values ('ce.r3', 20000), ('ce.r3C', 21000), ('ce.r3H', 22000)
),
keys(qidkey, descr, query) as (
    values
    (10, 'g,h shuffled z<=1024',
        'select s.id, r.d from ce.s3 s join @R@ r on r.g = s.g and r.h = s.h where s.z <= 1024'),
    (20, 'g,h sorted id<=1024',
        'select s.id, r.d from ce.s3 s join @R@ r on r.g = s.g and r.h = s.h where s.id <= 1024')
),
sizes(qidbs, bs) as (
    values (1, 1024), (2, 128), (3, 16)
)
select r.qidrel + k.qidkey + b.qidbs, b.bs, k.descr || ' [' || r.rel || ']',
       replace(k.query, '@R@', r.rel)
from rels r, keys k, sizes b;

insert into bnl_inner_queries
with rels(rel, qidrel) as (
    values ('ce.r2', 30000), ('ce.r2C', 31000), ('ce.r2H', 32000)
),
keys(qidkey, descr, query) as (
    values
    -- The first 1024 ids of ce.s2 are 103 values of g by 10 of h, all on
    -- the grid.
    (20, 'g,h sorted id<=1024',
        'select s.id, r.d from ce.s2 s join @R@ r on r.g = s.g and r.h = s.h where s.id <= 1024')
),
sizes(qidbs, bs) as (
    values (1, 1024), (2, 128), (3, 16)
)
select r.qidrel + k.qidkey + b.qidbs, b.bs, k.descr || ' [' || r.rel || ']',
       replace(k.query, '@R@', r.rel)
from rels r, keys k, sizes b;

select * from bnl_inner_queries order by qid;

-- Plan validation: every query plans the batched inner scan it is meant to
-- (Index Cond lists the batch, a ROW for the two-column key).
select q.qid, n->>'Node Type' node_type, n->>'Index Name' index_name,
       n->>'Index Cond' index_cond
from bnl_inner_queries q, lateral bnl_inner_node(q.bs, q.query, false) n
order by q.qid;

-- Per-batch estimates of the inner scan against the measured per-batch
-- averages (see check_bnl_inner_estimates).  Rows and read requests are exact
-- counts; seeks and nexts move with the memtable/SST layout, by far less than
-- the tolerance.  Every row estimate and every request estimate passes.  The
-- report lists only the cases with a failing estimate: the a cases (...10 to
-- ...30) and the shuffled primary key in batches of 16 (...43) pass every
-- estimate and are left out.
--
-- a (...10 to ...30): 10 rows per key, so the scan reaches most rows with
-- nexts.  The estimate charges 8% to 10% more seeks and 2% to 5% more nexts
-- than the scan takes, inside the tolerance, in shuffled and in key order
-- alike.  The inner filter of ...30 halves the rows returned and changes
-- neither the rows scanned nor their estimate.
--
-- pk (...40, ...50): one row per key.  The estimate charges two nexts per
-- key, the nexts a seek saves (MAX_NEXTS_TO_AVOID_SEEK); the scan takes one
-- per key in key order, about one in a shuffled batch of 1024 and more than
-- two in a shuffled batch of 16, so the next estimate fails except there.
--
-- b (...60): the unique secondary index is charged two seeks and three nexts
-- per key where the primary key is charged one and two.  The scan takes three
-- quarters of the seeks in a batch of 1024, and one to two nexts per key
-- more than charged in the batches of 128 and 16.
--
-- g, h (2xxxx, 3xxxx): the batched columns share the batch, so the estimate
-- counts one target per pair of the batch, a seek and two nexts each as for
-- a one-column key: 1024 seeks for a batch of 1024, 128 for 128 and 16 for
-- 16, and twice as many nexts.  The scan seeks about once per pair where the
-- batch's pairs sit apart in the index: on the hash keys and in the shuffled
-- batches of 128 and 16 on ce.r3, where the seek estimate passes.  A
-- shuffled batch of 1024 on ce.r3 seeks about once per two pairs, and the
-- hash keys' batches of 1024 seek 800 to 870 times, inside the tolerance on
-- ce.r2H only.  In key order on a range key the scan walks each run of
-- consecutive pairs with nexts and seeks once per run: once per value of g,
-- 16 times per batch on ce.r3, and once per ten pairs on ce.r2, so the seek
-- estimate fails there except for ce.r3's batches of 16, whose 16 pairs each
-- start a run.  The scan takes one to three nexts per pair, and the next
-- estimate passes only for the shuffled batches of 1024 on ce.r3 and the
-- hash keys' batches of 16.
select qid, descr, batches,
       "Row Estimates", "Roundtrip Estimates", "Seek Estimates", "Next Estimates"
from check_bnl_inner_estimates
where 'failed' in ("Row Estimates", "Roundtrip Estimates", "Seek Estimates",
                   "Next Estimates")
order by qid;
