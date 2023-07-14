--
-- YB tests for locking
--

CREATE TABLE yb_locks_t (k int PRIMARY KEY);
INSERT INTO yb_locks_t VALUES (1),(2),(3),(4),(5);

CREATE TABLE yb_locks_t2 (k1 int, k2 int, k3 int, v int, PRIMARY KEY(k1, k2, k3));
INSERT INTO yb_locks_t2 VALUES (1,2,3,4),(5,6,7,8);

SET yb_lock_pk_single_rpc TO ON;

-- Test plain (unlocked case).
EXPLAIN (COSTS OFF)
SELECT * FROM yb_locks_t WHERE k=5;
SELECT * FROM yb_locks_t WHERE k=5;

-- Test single-RPC select+lock (no LockRows node).
EXPLAIN (COSTS OFF)
SELECT * FROM yb_locks_t WHERE k=5 FOR UPDATE;
SELECT * FROM yb_locks_t WHERE k=5 FOR UPDATE;

-- Test other types of locking.
EXPLAIN (COSTS OFF)
SELECT * FROM yb_locks_t WHERE k=5 FOR SHARE;
SELECT * FROM yb_locks_t WHERE k=5 FOR SHARE;

EXPLAIN (COSTS OFF)
SELECT * FROM yb_locks_t WHERE k=5 FOR NO KEY UPDATE;
SELECT * FROM yb_locks_t WHERE k=5 FOR NO KEY UPDATE;

EXPLAIN (COSTS OFF)
SELECT * FROM yb_locks_t WHERE k=5 FOR KEY SHARE;
SELECT * FROM yb_locks_t WHERE k=5 FOR KEY SHARE;

-- Test LockRows node (more RPCs), and scan is unlocked.
EXPLAIN (COSTS OFF)
SELECT * FROM yb_locks_t FOR UPDATE;
SELECT * FROM yb_locks_t FOR UPDATE;

-- Test with multi-column primary key.
EXPLAIN (COSTS OFF)
SELECT * FROM yb_locks_t2 WHERE k1=1 AND k2=2 AND k3=3 FOR UPDATE;
SELECT * FROM yb_locks_t2 WHERE k1=1 AND k2=2 AND k3=3 FOR UPDATE;

-- Test with partial column set for primary key (should use LockRows).
EXPLAIN (COSTS OFF)
SELECT * FROM yb_locks_t2 WHERE k1=1 AND k2=2 FOR UPDATE;
SELECT * FROM yb_locks_t2 WHERE k1=1 AND k2=2 FOR UPDATE;

-- Test LockRows node is used for join.
EXPLAIN (COSTS OFF)
SELECT * FROM yb_locks_t2, yb_locks_t WHERE yb_locks_t2.k1 = yb_locks_t.k FOR UPDATE;
SELECT * FROM yb_locks_t2, yb_locks_t WHERE yb_locks_t2.k1 = yb_locks_t.k FOR UPDATE;

-- In isolation level SERIALIZABLE, all locks are done during scans.
BEGIN ISOLATION LEVEL SERIALIZABLE;

-- Test same locking as for REPEATABLE READ (default isolation).
EXPLAIN (COSTS OFF)
SELECT * FROM yb_locks_t WHERE k=5 FOR UPDATE;
SELECT * FROM yb_locks_t WHERE k=5 FOR UPDATE;

-- Test no LockRows node for sequential scan.
EXPLAIN (COSTS OFF)
SELECT * FROM yb_locks_t FOR UPDATE;
SELECT * FROM yb_locks_t FOR UPDATE;

-- Test no LockRows node for join.
EXPLAIN (COSTS OFF)
SELECT * FROM yb_locks_t2, yb_locks_t WHERE yb_locks_t2.k1 = yb_locks_t.k FOR UPDATE;
SELECT * FROM yb_locks_t2, yb_locks_t WHERE yb_locks_t2.k1 = yb_locks_t.k FOR UPDATE;

COMMIT;

-- Test with single-RPC select+lock turned off.
SET yb_lock_pk_single_rpc TO OFF;

EXPLAIN (COSTS OFF)
SELECT * FROM yb_locks_t WHERE k=5 FOR UPDATE;
SELECT * FROM yb_locks_t WHERE k=5 FOR UPDATE;

-- Test that with the yb_lock_pk_single_rpc off, SERIALIZABLE still locks during the scan
-- (no LockRows).
BEGIN ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (COSTS OFF)
SELECT * FROM yb_locks_t WHERE k=5 FOR UPDATE;
SELECT * FROM yb_locks_t WHERE k=5 FOR UPDATE;
COMMIT;

SET yb_lock_pk_single_rpc TO ON;

CREATE INDEX ON yb_locks_t2 (v);

-- Test with an index. We use a LockRows node for an index.
EXPLAIN (COSTS OFF)
SELECT * FROM yb_locks_t2 WHERE v=4 FOR UPDATE;
SELECT * FROM yb_locks_t2 WHERE v=4 FOR UPDATE;

-- Test only the indexed column.
EXPLAIN (COSTS OFF)
SELECT v FROM yb_locks_t2 WHERE v=4 FOR UPDATE;
SELECT v FROM yb_locks_t2 WHERE v=4 FOR UPDATE;

-- Isolation level SERIALIZABLE still locks with the scan though (no LockRows).
BEGIN ISOLATION LEVEL SERIALIZABLE;

EXPLAIN (COSTS OFF)
SELECT * FROM yb_locks_t2 WHERE v=4 FOR UPDATE;
SELECT * FROM yb_locks_t2 WHERE v=4 FOR UPDATE;

COMMIT;

-- Test partitions.
CREATE TABLE yb_locks_partition (a char PRIMARY KEY) PARTITION BY LIST (a);
CREATE TABLE yb_locks_partition_default PARTITION OF yb_locks_partition DEFAULT;
CREATE TABLE yb_locks_partition_a PARTITION OF yb_locks_partition FOR VALUES IN ('a');

EXPLAIN (COSTS OFF) SELECT * FROM yb_locks_partition WHERE a = 'a' FOR UPDATE;
EXPLAIN (COSTS OFF) SELECT * FROM yb_locks_partition WHERE a = 'b' FOR UPDATE;

BEGIN ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (COSTS OFF) SELECT * FROM yb_locks_partition WHERE a = 'a' FOR UPDATE;
COMMIT;

-- Test JSON.
EXPLAIN (COSTS OFF, FORMAT JSON)
SELECT * FROM yb_locks_t WHERE k=5 FOR UPDATE;

DROP TABLE yb_locks_t, yb_locks_t2, yb_locks_partition;
