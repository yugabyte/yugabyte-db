--
-- YB tests for lock batching
--

-- disable printing of all non-deterministic fields in EXPLAIN
SET yb_explain_hide_non_deterministic_fields = true;

-- Enable and set lock batching size
SET yb_explicit_row_locking_batch_size = 1024;

CREATE TABLE yb_locks_t(k INT PRIMARY KEY);
INSERT INTO yb_locks_t SELECT i FROM generate_series(1, 15) AS i;

CREATE TABLE yb_locks_t2(k1 INT, k2 INT, k3 INT, v INT, PRIMARY KEY(k1, k2, k3));
INSERT INTO yb_locks_t2 SELECT x, x + 1, x + 2, x + 3 FROM generate_series(1, 13, 4) AS s(x);

CREATE TABLE yb_locks_tasc(k INT, PRIMARY KEY (k ASC));
INSERT INTO yb_locks_tasc SELECT i FROM generate_series(1, 3) AS i;

-- Test plain (unlocked case).
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_t WHERE k = 5;
SELECT * FROM yb_locks_t WHERE k = 5;

-- Test single-RPC select+lock (no LockRows node).
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_t WHERE k = 5 FOR UPDATE;
SELECT * FROM yb_locks_t WHERE k = 5 FOR UPDATE;

-- Test other types of locking.
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_t WHERE k = 5 FOR SHARE;
SELECT * FROM yb_locks_t WHERE k = 5 FOR SHARE;

EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_t WHERE k = 5 FOR NO KEY UPDATE;
SELECT * FROM yb_locks_t WHERE k = 5 FOR NO KEY UPDATE;

EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_t WHERE k = 5 FOR KEY SHARE;
SELECT * FROM yb_locks_t WHERE k = 5 FOR KEY SHARE;

-- Test LockRows node (more RPCs), and scan is unlocked.
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_t FOR UPDATE;
SELECT * FROM yb_locks_t FOR UPDATE;

-- Test NOWAIT (should batch)
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_t FOR UPDATE NOWAIT;
SELECT * FROM yb_locks_t FOR UPDATE NOWAIT;

-- Test SKIP LOCKED (shouldn't batch)
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_t FOR UPDATE SKIP LOCKED;
SELECT * FROM yb_locks_t FOR UPDATE SKIP LOCKED;

-- Test different values of yb_explicit_row_locking_batch_size
-- Disabled
SET yb_explicit_row_locking_batch_size = 1;
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_t FOR UPDATE;
SELECT * FROM yb_locks_t FOR UPDATE;

-- Invalid value
SET yb_explicit_row_locking_batch_size = 0;

-- Value greater than write buffer and maximum number of rows read in an RPC
SET yb_explicit_row_locking_batch_size = 10000;
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_t FOR UPDATE;
SELECT * FROM yb_locks_t FOR UPDATE;

-- Revert back to original recommended value
SET yb_explicit_row_locking_batch_size = 1024;

-- Test with multi-column primary key.
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_t2 WHERE k1 = 1 AND k2 = 2 AND k3 = 3 FOR UPDATE;
SELECT * FROM yb_locks_t2 WHERE k1 = 1 AND k2 = 2 AND k3 = 3 FOR UPDATE;

-- Test with partial column set for primary key (should use LockRows).
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_t2 WHERE k1 = 1 AND k2 = 2 FOR UPDATE;
SELECT * FROM yb_locks_t2 WHERE k1 = 1 AND k2 = 2 FOR UPDATE;

-- Test LockRows node is used for join.
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_t2, yb_locks_t WHERE yb_locks_t2.k1 = yb_locks_t.k FOR UPDATE;
SELECT * FROM yb_locks_t2, yb_locks_t WHERE yb_locks_t2.k1 = yb_locks_t.k FOR UPDATE;

-- Test simple join with top-level locking
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_t JOIN yb_locks_t2 ON yb_locks_t.k = yb_locks_t2.k1 WHERE yb_locks_t.k > 0 AND yb_locks_t.k <= 5 FOR UPDATE;
SELECT * FROM yb_locks_t JOIN yb_locks_t2 ON yb_locks_t.k = yb_locks_t2.k1 WHERE yb_locks_t.k > 0 AND yb_locks_t.k <= 5 FOR UPDATE;

-- Test join with leaf-level locking (sub-query)
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_t JOIN (SELECT * FROM yb_locks_t2 FOR UPDATE) AS z ON yb_locks_t.k = z.k1 WHERE yb_locks_t.k > 0 AND yb_locks_t.k <= 5;
SELECT * FROM yb_locks_t JOIN (SELECT * FROM yb_locks_t2 FOR UPDATE) AS z ON yb_locks_t.k = z.k1 WHERE yb_locks_t.k > 0 AND yb_locks_t.k <= 5;

-- Test when limit returns less than filtered query
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_t WHERE yb_locks_t.k < 6 FOR UPDATE LIMIT 3;
SELECT * FROM yb_locks_t WHERE yb_locks_t.k < 6 FOR UPDATE LIMIT 3;

-- Test limit with a Sort plan node between LockRows and Scan node
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_t ORDER BY k LIMIT 13 FOR UPDATE;
SELECT * FROM yb_locks_t ORDER BY k LIMIT 13 FOR UPDATE;

-- Test when multiple limit nodes
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM (SELECT * FROM yb_locks_t LIMIT 13 FOR UPDATE) AS y JOIN (SELECT * FROM yb_locks_t2 LIMIT 3 FOR UPDATE) AS z ON y.k = z.k1;
SELECT * FROM (SELECT * FROM yb_locks_t LIMIT 13 FOR UPDATE) AS y JOIN (SELECT * FROM yb_locks_t2 LIMIT 3 FOR UPDATE) AS z ON y.k = z.k1;

-- Test when FOR UPDATE is supposed to acquire less locks than the limit (1 row on yb_locks_t, but all limited rows on yb_locks_t2)
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM (SELECT * FROM yb_locks_t ORDER BY k LIMIT 10 FOR UPDATE) AS y JOIN (SELECT * FROM yb_locks_t2 ORDER BY k1 LIMIT 3 FOR UPDATE) AS z ON y.k = z.k1 LIMIT 1;
SELECT * FROM (SELECT * FROM yb_locks_t ORDER BY k LIMIT 10 FOR UPDATE) AS y JOIN (SELECT * FROM yb_locks_t2 ORDER BY k1 LIMIT 3 FOR UPDATE) AS z ON y.k = z.k1 LIMIT 1;

-- Test with CTE subquery locking
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
WITH cte AS (SELECT * FROM yb_locks_t FOR UPDATE) SELECT * FROM cte;
WITH cte AS (SELECT * FROM yb_locks_t FOR UPDATE) SELECT * FROM cte;

-- Test with multiple CTE subquery locking
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
WITH cte_yb_locks_t AS (SELECT * FROM yb_locks_t FOR UPDATE), cte_yb_locks_t2 AS (SELECT * FROM yb_locks_t2 FOR UPDATE) SELECT * FROM cte_yb_locks_t JOIN cte_yb_locks_t2 ON cte_yb_locks_t.k = cte_yb_locks_t2.k1 LIMIT 2;
WITH cte_yb_locks_t AS (SELECT * FROM yb_locks_t FOR UPDATE), cte_yb_locks_t2 AS (SELECT * FROM yb_locks_t2 FOR UPDATE) SELECT * FROM cte_yb_locks_t JOIN cte_yb_locks_t2 ON cte_yb_locks_t.k = cte_yb_locks_t2.k1 LIMIT 2;

-- Test top level limit with multiple CTE subquery locking
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
WITH cte_yb_locks_t AS (SELECT * FROM yb_locks_t FOR UPDATE), cte_yb_locks_t2 AS (SELECT * FROM yb_locks_t2 FOR UPDATE) SELECT * FROM cte_yb_locks_t JOIN cte_yb_locks_t2 ON cte_yb_locks_t.k = cte_yb_locks_t2.k1 LIMIT 2;
WITH cte_yb_locks_t AS (SELECT * FROM yb_locks_t FOR UPDATE), cte_yb_locks_t2 AS (SELECT * FROM yb_locks_t2 FOR UPDATE) SELECT * FROM cte_yb_locks_t JOIN cte_yb_locks_t2 ON cte_yb_locks_t.k = cte_yb_locks_t2.k1 LIMIT 2;

-- Test LockRows node is used with ASC table when YB Sequential Scan is used.
/*+ SeqScan(yb_locks_tasc) */ EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_tasc WHERE k = 1 FOR UPDATE;
/*+ SeqScan(yb_locks_tasc) */ SELECT * FROM yb_locks_tasc WHERE k = 1 FOR UPDATE;

-- In isolation level SERIALIZABLE, all locks are done during scans.
BEGIN ISOLATION LEVEL SERIALIZABLE;

-- Test same locking as for REPEATABLE READ (default isolation).
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_t WHERE k = 5 FOR UPDATE;
SELECT * FROM yb_locks_t WHERE k = 5 FOR UPDATE;

-- Test no LockRows node for sequential scan.
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_t FOR UPDATE;
SELECT * FROM yb_locks_t FOR UPDATE;

-- Test no LockRows node for join.
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_t2, yb_locks_t WHERE yb_locks_t2.k1 = yb_locks_t.k FOR UPDATE;
SELECT * FROM yb_locks_t2, yb_locks_t WHERE yb_locks_t2.k1 = yb_locks_t.k FOR UPDATE;

-- Test locking, and no LockRows node, when using an ASC table and YB Sequential Scan.
-- (No WHERE clause.)
/*+ SeqScan(yb_locks_tasc) */ EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_tasc FOR UPDATE;
/*+ SeqScan(yb_locks_tasc) */ SELECT * FROM yb_locks_tasc FOR UPDATE;

-- For an ASC table, should lock inline, with no LockRows node.
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_tasc ORDER BY k FOR UPDATE;
SELECT * FROM yb_locks_tasc ORDER BY k FOR UPDATE;

COMMIT;

-- Test with single-RPC select+lock turned off.
SET yb_lock_pk_single_rpc TO OFF;

EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_t WHERE k = 5 FOR UPDATE;
SELECT * FROM yb_locks_t WHERE k = 5 FOR UPDATE;

-- Test that with the yb_lock_pk_single_rpc off, SERIALIZABLE still locks during the scan
-- (no LockRows).
BEGIN ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_t WHERE k = 5 FOR UPDATE;
SELECT * FROM yb_locks_t WHERE k = 5 FOR UPDATE;
COMMIT;

SET yb_lock_pk_single_rpc TO ON;

CREATE INDEX ON yb_locks_t2 (v);

-- Test with an index. We use a LockRows node for an index.
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_t2 WHERE v = 4 FOR UPDATE;
SELECT * FROM yb_locks_t2 WHERE v = 4 FOR UPDATE;

-- Test only the indexed column.
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT v FROM yb_locks_t2 WHERE v = 4 FOR UPDATE;
SELECT v FROM yb_locks_t2 WHERE v = 4 FOR UPDATE;

-- Isolation level SERIALIZABLE still locks with the scan though (no LockRows).
BEGIN ISOLATION LEVEL SERIALIZABLE;

EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_locks_t2 WHERE v = 4 FOR UPDATE;
SELECT * FROM yb_locks_t2 WHERE v = 4 FOR UPDATE;

COMMIT;

-- Test partitions.
CREATE TABLE yb_locks_partition(a char PRIMARY KEY) PARTITION BY LIST (a);
CREATE TABLE yb_locks_partition_default PARTITION OF yb_locks_partition DEFAULT;
CREATE TABLE yb_locks_partition_a PARTITION OF yb_locks_partition FOR VALUES IN ('a');

EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF) SELECT * FROM yb_locks_partition WHERE a = 'a' FOR UPDATE;
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF) SELECT * FROM yb_locks_partition WHERE a = 'b' FOR UPDATE;

BEGIN ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF) SELECT * FROM yb_locks_partition WHERE a = 'a' FOR UPDATE;
COMMIT;

-- Test JSON.
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF, FORMAT JSON)
SELECT * FROM yb_locks_t WHERE k = 5 FOR UPDATE;

-- Test that prepared statements made in isolation level RR with a LockRows node do not
-- crash when executed in isolation level SERIALIZABLE.
SET yb_lock_pk_single_rpc TO OFF;
-- Store prepared plans right away.
SET yb_test_planner_custom_plan_threshold to 1;
PREPARE yb_locks_plan_rr(INT) AS SELECT * FROM yb_locks_t WHERE k = $1 FOR UPDATE;
EXECUTE yb_locks_plan_rr(1);
-- The $1 in the EXPLAIN output tells you it's a stored plan.
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
EXECUTE yb_locks_plan_rr(1);

BEGIN ISOLATION LEVEL SERIALIZABLE;
EXECUTE yb_locks_plan_rr(1);
-- The LockRows node has a "no-op" annotation.
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
EXECUTE yb_locks_plan_rr(1);
-- In JSON mode, the LockRows node has an "Executes" field set to false.
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF, FORMAT JSON)
EXECUTE yb_locks_plan_rr(1);
COMMIT;

-- Test that prepared statements made in isolation level SERIALIZABLE, but for a PK, are
-- able to lock PK when run in RR and RC.
SET yb_lock_pk_single_rpc TO ON;
BEGIN ISOLATION LEVEL SERIALIZABLE;
PREPARE yb_locks_plan_ser(INT) AS SELECT * FROM yb_locks_t WHERE k = $1 FOR UPDATE;
EXECUTE yb_locks_plan_ser(1);
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
EXECUTE yb_locks_plan_ser(1);
COMMIT;

EXECUTE yb_locks_plan_ser(1);
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
EXECUTE yb_locks_plan_ser(1);

-- Test that prepared statements made in isolation level SERIALIZABLE, for a non-PK, have
-- a LockRows node that functions in RR and RC.
BEGIN ISOLATION LEVEL SERIALIZABLE;
PREPARE yb_locks_plan_ser_all(INT) AS SELECT * FROM yb_locks_t FOR UPDATE;
EXECUTE yb_locks_plan_ser_all(1);
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
EXECUTE yb_locks_plan_ser_all(1);
COMMIT;
EXECUTE yb_locks_plan_ser_all(1);
EXPLAIN (COSTS OFF)
EXECUTE yb_locks_plan_ser_all(1);

-- Test table with Postgres side filtering
CREATE TABLE yb_events (
    id SERIAL PRIMARY KEY,
    event_name TEXT,
    event_time TIMESTAMP WITH TIME ZONE
);
INSERT INTO yb_events(event_name, event_time)
SELECT 'Future Event ' || i, NOW() + (i || ' days')::interval
FROM generate_series(1, 10) AS s(i);
INSERT INTO yb_events(event_name, event_time)
SELECT 'Past Event ' || i, NOW() - (i || ' days')::interval
FROM generate_series(1, 10) AS s(i);

EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_events WHERE event_time > NOW();

EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)
SELECT * FROM yb_events WHERE event_time < NOW();

-- Reset
SET yb_lock_pk_single_rpc TO DEFAULT;
SET yb_test_planner_custom_plan_threshold TO DEFAULT;
DROP TABLE yb_locks_t, yb_locks_t2, yb_locks_tasc, yb_locks_partition, yb_events;
