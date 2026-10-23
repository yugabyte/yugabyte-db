-- Test the planner's per-batch estimates for the inner index scan of a YB
-- Batched Nested Loop (BNL)
-- DEPENDENCY: yb.orig.bnl_cost_parity_setup
\c bnl_cost_parity_test

delete from bnl_inner_queries;

-- QID categories (last digit: batch size 1 = 1024, 2 = 128, 3 = 16):
--   1xxxx: one-column key, inner ce.r (+1000 for the colocated ce.rC)
--   2xxxx: two-column key (g, h), inner ce.r2 (+1000 for the colocated ce.r2C,
--          +2000 for the hash-keyed ce.r2H)
-- Each outer is 1024 rows of ce.s or ce.s2, so every batch is full.  The
-- outer restrictions set the fraction of outer keys that have inner rows.
insert into bnl_inner_queries
with rels(rel, qidrel) as (
    values ('ce.r', 10000), ('ce.rC', 11000)
),
keys(qidkey, descr, query) as (
    values
    -- r.a has 10 rows per value over 3886..5120.  z is the shuffled key, so
    -- z <= 1024 spreads the outer over 1..5120, and 231 of its 1024 keys fall
    -- in r.a's range.  10 is the row of yb.orig.bnl_cost_parity's 5xx
    -- section, with that file's inner filter.
    (10, 'a  z<=1024 e<=600',
        'select s.id, r.d from ce.s s join @R@ r on r.a = s.y and r.e <= 600 where s.z <= 1024'),
    (20, 'a  z<=1024 (23%)',
        'select s.id, r.d from ce.s s join @R@ r on r.a = s.y where s.z <= 1024'),
    (30, 'a  x in 3374..4397 (50%)',
        'select s.id, r.d from ce.s s join @R@ r on r.a = s.y where s.x between 3374 and 4397'),
    (40, 'a  x in 3886..4909 (100%)',
        'select s.id, r.d from ce.s s join @R@ r on r.a = s.y where s.x between 3886 and 4909'),
    (50, 'a  x in 1..1024 (0%)',
        'select s.id, r.d from ce.s s join @R@ r on r.a = s.y where s.x between 1 and 1024'),
    -- The unique primary key over 1..12345: every key has its row.
    (60, 'pk z<=1024 (100%)',
        'select s.id, r.d from ce.s s join @R@ r on r.pk = s.y where s.z <= 1024')
),
sizes(qidbs, bs) as (
    values (1, 1024), (2, 128), (3, 16)
)
select r.qidrel + k.qidkey + b.qidbs, b.bs, k.descr || ' [' || r.rel || ']',
       replace(k.query, '@R@', r.rel)
from rels r, keys k, sizes b;

insert into bnl_inner_queries
with rels(rel, qidrel) as (
    values ('ce.r2', 20000), ('ce.r2C', 21000), ('ce.r2H', 22000)
),
keys(qidkey, descr, query) as (
    values
    (10, 'g,h z<=1024 (26%)',
        'select s.id, r.d from ce.s2 s join @R@ r on r.g = s.g and r.h = s.h where s.z <= 1024'),
    (20, 'g,h id in 769..1792 (50%)',
        'select s.id, r.d from ce.s2 s join @R@ r on r.g = s.g and r.h = s.h where s.id between 769 and 1792'),
    (30, 'g,h id in 1..1024 (100%)',
        'select s.id, r.d from ce.s2 s join @R@ r on r.g = s.g and r.h = s.h where s.id between 1 and 1024'),
    (40, 'g,h id in 2049..3072 (0%)',
        'select s.id, r.d from ce.s2 s join @R@ r on r.g = s.g and r.h = s.h where s.id between 2049 and 3072')
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
-- the tolerance.
--
-- One-column key (1xxxx).  The row estimate is the batch size times the rows
-- of one key, as if every key of the batch had rows: 10236 (10226 on ...50)
-- at batch size 1024, 1024 x 10 rows per value of r.a, whatever fraction of
-- the keys r.a has.  It is right when every key has its rows (...40) and on
-- the unique primary key (...60).  With 23% of the keys present (...20) it is
-- 4.4 times the 2310 rows the scan returns, with half of them (...30) 2
-- times, and with none (...50) the scan returns nothing.  ...10 is
-- yb.orig.bnl_cost_parity's 5xx row: 4907 estimated against 1131 on ce.rC.
-- Every row the scan returns joins one outer key, so the per-batch estimate
-- times the batches should be the join's own estimate, which counts the
-- fraction: on ...20 the join is estimated at 2469 rows (2310 measured).
-- Read requests, seeks and nexts follow the rows.  The requests fail where
-- the extra rows add pages: at batch size 1024, and at 128 on the
-- non-colocated ce.r except on ...30, whose estimate is one request over and
-- passes.  The seeks and nexts fail wherever the rows do.  On the dense keys
-- of ...40 the estimate charges an index seek per key that the scan reaches
-- with nexts, 10% over, inside the tolerance.  The primary key's nexts fail
-- at batch sizes 1024 and 128: two nexts are charged per key, while the scan
-- takes about one per key in a large batch (1056 for 1024 keys) and more in
-- a small one (36 for 16).
--
-- Two-column key (2xxxx), TODO(#19549): the batch factor applies once per key
-- column, so a batch of 16 pairs is estimated at 256 rows (16 x 16) against 16
-- when every pair has its row (...30), and a batch of 128 at the whole table,
-- 16384 rows (128 x 128) against 128.  At 1024 the estimate is the whole table
-- against 1024.  The one-column defect above comes on top: with 26% of the
-- pairs present (...10) a batch of 1024 returns 265 rows.  The read requests
-- (16 per batch against 1 at batch sizes 1024 and 128), seeks and nexts
-- follow; the hash key's nexts at 1024 with every pair present (22031) fall
-- inside the tolerance (3276 against 2898).
select qid, descr, batches,
       "Row Estimates", "Roundtrip Estimates", "Seek Estimates", "Next Estimates"
from check_bnl_inner_estimates
order by qid;
