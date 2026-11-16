-- Test the planner's per-batch estimates for the inner index scan of a YB
-- Batched Nested Loop (BNL)
-- DEPENDENCY: yb.orig.bnl_cost_parity_setup
\c bnl_cost_parity_test

delete from bnl_inner_queries;

-- The row estimate of a batched inner scan is the batch size times the rows
-- of one key, once for each batched key column: eqsel multiplies the key's
-- equality selectivity by the batch size, so every key of a batch counts as
-- present, and two batched columns each contribute the factor.  The data
-- here meets both assumptions, so the row estimates hold and the request,
-- seek and next verdicts stand on their own: every outer key has inner rows,
-- the outer keys are unique, and the two-column key's leading column has
-- fewer values than any batch.  Where the assumptions fail, the join's
-- ranking shows it: yb.orig.bnl_cost_parity's 5xx section joins keys most of
-- which have no inner rows, and its 6xx section a two-column key both of
-- whose columns have more values than a batch (#19549).
--
-- QID categories (last digit: batch size 1 = 1024, 2 = 128, 3 = 16):
--   1xxxx: one-column key, inner ce.r (+1000 for the colocated ce.rC): a with
--          10 rows per value, the unique primary key pk and the unique
--          secondary index on b
--   2xxxx: two-column key (g, h), inner ce.r3 (+1000 for the colocated ce.r3C,
--          +2000 for the hash-keyed ce.r3H)
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
-- g, h (2xxxx): the seek and next estimate takes each key column's IN list on
-- its own: for h, the smaller of the batch and the 772 values of h per value
-- of g, two nexts each, then times the 16 values of g, as if every pair of
-- the two lists were probed: 12345 seeks for a batch of 1024 pairs, 2048 for
-- 128, 256 for 16, and twice as many nexts.  The scan seeks once per run of
-- consecutive pairs, 16 times per batch in key order on the range key and
-- about once per pair or two in shuffled order and on the hash key, and takes
-- one to three nexts per pair.
select qid, descr, batches,
       "Row Estimates", "Roundtrip Estimates", "Seek Estimates", "Next Estimates"
from check_bnl_inner_estimates
where 'failed' in ("Row Estimates", "Roundtrip Estimates", "Seek Estimates",
                   "Next Estimates")
order by qid;
