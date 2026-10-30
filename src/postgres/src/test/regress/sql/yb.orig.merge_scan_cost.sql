--
-- See yb_merge_scan_schedule for details about the test.  Merge scan's cost
-- is its index scan's cost plus a per-row charge for the k-way merge
-- (#29078).  This test pins the plans that charge decides.
--
-- The planner builds a merge scan path whenever its order could be used: a
-- join clause on the sort column could feed a merge join, and a GROUP BY or
-- DISTINCT on it could feed a sorted aggregate or Unique.  Whether the order
-- is used is decided later, and a Hash Join, hash aggregate or hashed
-- DISTINCT never uses it.  Before #29078, the merge path could still win
-- the base relation for those consumers, and the query then ran a merge
-- scan for nothing.  With the charge, a consumer that does not use the
-- order gets the same plan as with merge scan disabled, and a consumer that
-- does use it still takes the merge.
--

\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/merge_scan_setup.sql'
\i :filename

-- These tests check plan shape only, so plain EXPLAIN is enough.
\set explain 'EXPLAIN (COSTS OFF)'

SET yb_enable_derived_saops = true;

-- bkt_tbl's three streams keep the per-row charge under the 2% floor at this
-- row count, so the charge does not show there.  Sixteen streams put it above
-- the floor.  Same rows and key shape as bkt_tbl.
CREATE TABLE bkt16 (
    n int GENERATED ALWAYS AS ((r1 + r2 * 10 + r3 * 100 + r4 * 1000 + r5 * 10000)::int) STORED,
    r5 float8,
    r3 int,
    r1 int2,
    r2 int8,
    r4 numeric,
    bkt int GENERATED ALWAYS AS (yb_hash_code(r3, r2, r4, r5, r1) % 16) STORED,
    PRIMARY KEY (bkt ASC, r1, r2, r3, r4, r5));
INSERT INTO bkt16 (r1, r2, r3, r4, r5) SELECT r1, r2, r3, r4, r5 FROM r5n;
ANALYZE bkt16;

-- The same rows range partitioned over a bucketized key.  This mirrors the
-- case in #33378, where a Hash Join over the partitions read them through
-- merge scans.
CREATE TABLE pbkt16 (
    n int GENERATED ALWAYS AS ((r1 + r2 * 10 + r3 * 100 + r4 * 1000 + r5 * 10000)::int) STORED,
    r5 float8,
    r3 int,
    r1 int2,
    r2 int8,
    r4 numeric,
    bkt int GENERATED ALWAYS AS (yb_hash_code(r3, r2, r4, r5, r1) % 16) STORED,
    PRIMARY KEY (bkt ASC, r1, r2, r3, r4, r5))
PARTITION BY RANGE (r5);
CREATE TABLE pbkt16_1 PARTITION OF pbkt16 FOR VALUES FROM (0) TO (3);
CREATE TABLE pbkt16_2 PARTITION OF pbkt16 FOR VALUES FROM (3) TO (6);
CREATE TABLE pbkt16_3 PARTITION OF pbkt16 FOR VALUES FROM (6) TO (10);
INSERT INTO pbkt16 (r1, r2, r3, r4, r5) SELECT r1, r2, r3, r4, r5 FROM r5n;
ANALYZE pbkt16_1, pbkt16_2, pbkt16_3, pbkt16;

--
-- Consumers that do not use the order.  Merge scan should not be used: the
-- plan with merge scan enabled should be the plan with it disabled.
--

-- Hash Join with bkt16 on the outer side.  The merge path on bkt16 is a
-- candidate for that side and should lose to the Seq Scan.
\set query ':explain :Q SELECT count(*) FROM bkt16 b JOIN r5n r ON b.r1 = r.r1;'
\set Q1 '/*+HashJoin(b r) Leading((b r)) Set(yb_max_merge_scan_streams 0)*/'
\set Q2 '/*+HashJoin(b r) Leading((b r)) Set(yb_max_merge_scan_streams 64)*/'
\i :run_query

-- Hash Join with bkt16 under the Hash.  The merge path is a candidate for
-- the hashed side and should lose there too.
\set Q1 '/*+HashJoin(b r) Leading((r b)) Set(yb_max_merge_scan_streams 0)*/'
\set Q2 '/*+HashJoin(b r) Leading((r b)) Set(yb_max_merge_scan_streams 64)*/'
\i :run_query
\set Q1 :off
\set Q2 :on

-- Unhinted, the join is a batched nested loop.  Its outer side needs no
-- order, so the merge path on bkt16 should lose to the Seq Scan there as
-- well.
\i :run_query

-- Self join on the sort column.  Both sides have a merge path as a
-- candidate and both should take the Seq Scan.
\set query ':explain :Q SELECT count(*) FROM bkt16 a JOIN bkt16 b ON a.r1 = b.r1;'
\set Q1 '/*+HashJoin(a b) Set(yb_max_merge_scan_streams 0)*/'
\set Q2 '/*+HashJoin(a b) Set(yb_max_merge_scan_streams 64)*/'
\i :run_query

-- Semi join.  The merge path on bkt16 is a candidate under the Hash and
-- should lose to the Seq Scan.  Before #29078 it won.
\set query ':explain :Q SELECT count(*) FROM r5n r WHERE EXISTS (SELECT 1 FROM bkt16 b WHERE b.r1 = r.r1);'
\set Q1 '/*+HashJoin(r b) Set(yb_max_merge_scan_streams 0)*/'
\set Q2 '/*+HashJoin(r b) Set(yb_max_merge_scan_streams 64)*/'
\i :run_query
\set Q1 :off
\set Q2 :on

-- Anti join, unhinted.  Same candidate, same outcome.
\set query ':explain :Q SELECT count(*) FROM r5n r WHERE NOT EXISTS (SELECT 1 FROM bkt16 b WHERE b.r1 = r.r1 AND b.r2 = r.r2);'
\i :run_query

-- DISTINCT.  The distinct pathkeys make the merge path a candidate, as
-- Unique over it would need no sort.  A hash aggregate over the plain
-- distinct index scan is cheaper and should win.  Before #29078 the Unique
-- over the merge won.
\set query ':explain :Q SELECT DISTINCT r1 FROM bkt16;'
\i :run_query
\set query ':explain :Q SELECT DISTINCT r1, r2 FROM bkt16;'
\i :run_query

-- GROUP BY, likewise: a hash aggregate over the Seq Scan should win, where
-- before #29078 a GroupAggregate over the merge did.
\set query ':explain :Q SELECT r1, count(*) FROM bkt16 GROUP BY r1;'
\i :run_query

-- Partitioned: the Hash Join from #33378 over the partitions, and DISTINCT
-- over them.  Every partition should take the Seq Scan under the join and
-- the plain distinct index scan under the DISTINCT.  Before #29078, the
-- largest partition took the merge under the join, and every partition did
-- under the DISTINCT.  Parallel workers are off for the join because the
-- rows each worker takes vary from run to run.
\set query ':explain :Q SELECT count(*) FROM pbkt16 p JOIN r5n r ON p.r1 = r.r1;'
\set Q1 '/*+HashJoin(p r) Leading((p r)) Set(max_parallel_workers_per_gather 0) Set(yb_max_merge_scan_streams 0)*/'
\set Q2 '/*+HashJoin(p r) Leading((p r)) Set(max_parallel_workers_per_gather 0) Set(yb_max_merge_scan_streams 64)*/'
\i :run_query
\set Q1 :off
\set Q2 :on
\set query ':explain :Q SELECT DISTINCT r1 FROM pbkt16;'
\i :run_query

--
-- Consumers that use the order.  Merge scan should be used, as it was
-- before #29078: the charge prices the merge, it does not disable it.
--

-- ORDER BY with and without LIMIT.  The merge supplies the order and
-- should be used.
\set R1 'LIMIT 5'
\set R2
\set query ':explain :Q SELECT r1, r2 FROM bkt16 ORDER BY r1, r2, r3 :R;'
\i :run_query
\unset R1
\unset R2

-- Merge Join on the same query the Hash Join above ran.  It consumes the
-- order, so the merge should be used here.
\set query ':explain :Q SELECT count(*) FROM bkt16 b JOIN r5n r ON b.r1 = r.r1;'
\set Q1 '/*+MergeJoin(b r) Leading((b r)) Set(yb_max_merge_scan_streams 0)*/'
\set Q2 '/*+MergeJoin(b r) Leading((b r)) Set(yb_max_merge_scan_streams 64)*/'
\i :run_query
\set Q1 :off
\set Q2 :on

-- min, which the planner turns into ORDER BY LIMIT 1.  The merge should be
-- used.
\set query ':explain :Q SELECT min(r1) FROM bkt16;'
\i :run_query

-- MergeAppend over the partitions.  Each partition should take the merge.
\set query ':explain :Q SELECT r1, r2 FROM pbkt16 ORDER BY r1, r2, r3 LIMIT 5;'
\i :run_query

DROP TABLE bkt16, pbkt16;
