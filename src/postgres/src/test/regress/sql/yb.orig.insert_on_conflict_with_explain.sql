-- This file contains tests that use EXPLAIN in conjunction with INSERT ... ON CONFLICT.
-- The mapping between this test's output and outfiles is as follows:
--  > yb.orig.insert_on_conflict_with_explain.out: INSERT ... ON CONFLICT read batching disabled.
--  > yb.orig.insert_on_conflict_with_explain_1.out: INSERT ... ON CONFLICT read batching enabled, batch size = 1.
--  > yb.orig.insert_on_conflict_with_explain_2.out: INSERT ... ON CONFLICT read batching enabled, batch size = 2.
--  > yb.orig.insert_on_conflict_with_explain_3.out: INSERT ... ON CONFLICT read batching enabled, batch size > 2.

--- GH-28234: IndexOnlyScan support for INSERT ... ON CONFLICT ... DO NOTHING
CREATE TABLE t_ios (k INT PRIMARY KEY, v1 INT, v2 INT);
CREATE UNIQUE INDEX NONCONCURRENTLY t_ios_v1 ON t_ios (v1);
INSERT INTO t_ios (k, v1, v2) (SELECT i, 10 - i, i FROM generate_series(1, 10) AS i);

-- No conflict with either index.
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO t_ios VALUES (11, 11, 11), (12, 12, 12) ON CONFLICT DO NOTHING;
-- Conflict with exactly one index.
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO t_ios VALUES (1, 13, 100), (2, 14, 101) ON CONFLICT DO NOTHING;
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO t_ios VALUES (1, 13, 100), (2, 14, 101) ON CONFLICT (k) DO NOTHING;
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO t_ios VALUES (1, 13, 100), (2, 14, 101) ON CONFLICT (v1) DO NOTHING;
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO t_ios VALUES (13, 1, 100), (14, 2, 101) ON CONFLICT DO NOTHING;
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO t_ios VALUES (13, 1, 100), (14, 2, 101) ON CONFLICT (v1) DO NOTHING;
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO t_ios VALUES (13, 1, 100), (14, 2, 101) ON CONFLICT (k) DO NOTHING;
-- Conflict with different rows in both indexes.
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO t_ios VALUES (1, 1, 100), (2, 2, 101) ON CONFLICT DO NOTHING;

DROP INDEX t_ios_v1;
CREATE UNIQUE INDEX NONCONCURRENTLY t_ios_v1_v2 ON t_ios (v1, v2);

-- The IndexOnlyScan should also work with multi-column indexes.
-- No conflict with multi-column index.
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO t_ios VALUES (100, 5, 100), (101, 100, 5) ON CONFLICT (v1, v2) DO NOTHING;
-- Conflict with multi-column index.
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO t_ios VALUES (102, 3, 7) ON CONFLICT (v1, v2) DO NOTHING;
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO t_ios VALUES (102, 3, 7) ON CONFLICT DO NOTHING;
-- Conflict with non-arbiter index
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO t_ios VALUES (1, 1, 100) ON CONFLICT (v1, v2) DO NOTHING;
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO t_ios VALUES (1, 1, 100) ON CONFLICT DO NOTHING;

SELECT * FROM t_ios ORDER BY k;

-- IndexOnlyScan for INSERT ... ON CONFLICT ... DO NOTHING should also work
-- with partitioned tables.
CREATE TABLE t_ios_base (k INT PRIMARY KEY, v1 INT) PARTITION BY RANGE (k);
CREATE TABLE t_ios_p1 PARTITION OF t_ios_base FOR VALUES FROM (0) TO (10);
CREATE TABLE t_ios_p2 PARTITION OF t_ios_base FOR VALUES FROM (10) TO (20);

INSERT INTO t_ios_base VALUES (1, 1), (2, 2), (11, 11), (12, 12);

CREATE UNIQUE INDEX idx_ios_p1_v1 ON t_ios_p1 (k, v1);
-- Query should error out when not all partitions have an arbiter index.
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO t_ios_base VALUES (1, 1), (11, 11) ON CONFLICT (k, v1) DO NOTHING;
-- Query should also error out when the indexes on the partition are not part of the same heirarchy.
CREATE UNIQUE INDEX idx_ios_p2_v1 ON t_ios_p2 (k, v1);
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO t_ios_base VALUES (1, 1), (11, 11) ON CONFLICT (k, v1) DO NOTHING;
DROP INDEX idx_ios_p1_v1;
DROP INDEX idx_ios_p2_v1;
-- IndexOnlyScan should be performed when all partitions share an arbiter index that is part of the same heirarchy.
CREATE UNIQUE INDEX idx_ios_base_v1 ON t_ios_base (k, v1);
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO t_ios_base VALUES (1, 1), (11, 11) ON CONFLICT (k, v1) DO NOTHING;
