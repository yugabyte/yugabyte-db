\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/parameterized_query.sql'
\i :filename
\set P1 ':explain'
\set P2
\set explain 'EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF)'

--
-- YB tests for lock batching
--

-- Enable and set lock batching size
SET yb_explicit_row_locking_batch_size = 1024;

CREATE TABLE yb_locks_t(k INT PRIMARY KEY);
INSERT INTO yb_locks_t SELECT i FROM generate_series(1, 15) AS i;

CREATE TABLE yb_locks_t2(k1 INT, k2 INT, k3 INT, v INT, PRIMARY KEY(k1, k2, k3));
INSERT INTO yb_locks_t2 SELECT x, x + 1, x + 2, x + 3 FROM generate_series(1, 13, 4) AS s(x);

CREATE TABLE yb_locks_tasc(k INT, PRIMARY KEY (k ASC));
INSERT INTO yb_locks_tasc SELECT i FROM generate_series(1, 3) AS i;

-- Test plain (unlocked case).
\set query ':P SELECT * FROM yb_locks_t WHERE k = 5;'
\i :iter_P2

-- Test single-RPC select+lock (no LockRows node).
\set query ':P SELECT * FROM yb_locks_t WHERE k = 5 FOR UPDATE;'
\i :iter_P2

-- Test other types of locking.
\set query ':P SELECT * FROM yb_locks_t WHERE k = 5 FOR SHARE;'
\i :iter_P2

\set query ':P SELECT * FROM yb_locks_t WHERE k = 5 FOR NO KEY UPDATE;'
\i :iter_P2

\set query ':P SELECT * FROM yb_locks_t WHERE k = 5 FOR KEY SHARE;'
\i :iter_P2

-- Test LockRows node (more RPCs), and scan is unlocked.
\set query ':P SELECT * FROM yb_locks_t FOR UPDATE;'
\i :iter_P2

-- Test NOWAIT (should batch)
\set query ':P SELECT * FROM yb_locks_t FOR UPDATE NOWAIT;'
\i :iter_P2

-- Test SKIP LOCKED (shouldn't batch)
\set query ':P SELECT * FROM yb_locks_t FOR UPDATE SKIP LOCKED;'
\i :iter_P2

-- Test different values of yb_explicit_row_locking_batch_size
-- Disabled
SET yb_explicit_row_locking_batch_size = 1;
\set query ':P SELECT * FROM yb_locks_t FOR UPDATE;'
\i :iter_P2

-- Invalid value
SET yb_explicit_row_locking_batch_size = 0;

-- Value greater than write buffer and maximum number of rows read in an RPC
SET yb_explicit_row_locking_batch_size = 10000;
\set query ':P SELECT * FROM yb_locks_t FOR UPDATE;'
\i :iter_P2

-- Revert back to original recommended value
SET yb_explicit_row_locking_batch_size = 1024;

-- Test with multi-column primary key.
\set query ':P SELECT * FROM yb_locks_t2 WHERE k1 = 1 AND k2 = 2 AND k3 = 3 FOR UPDATE;'
\i :iter_P2

-- Test with partial column set for primary key (should use LockRows).
\set query ':P SELECT * FROM yb_locks_t2 WHERE k1 = 1 AND k2 = 2 FOR UPDATE;'
\i :iter_P2

-- Test LockRows node is used for join.
\set query ':P SELECT * FROM yb_locks_t2, yb_locks_t WHERE yb_locks_t2.k1 = yb_locks_t.k FOR UPDATE;'
\i :iter_P2

-- Test simple join with top-level locking
\set query ':P SELECT * FROM yb_locks_t JOIN yb_locks_t2 ON yb_locks_t.k = yb_locks_t2.k1 WHERE yb_locks_t.k > 0 AND yb_locks_t.k <= 5 FOR UPDATE;'
\i :iter_P2

-- Test join with leaf-level locking (sub-query)
\set query ':P SELECT * FROM yb_locks_t JOIN (SELECT * FROM yb_locks_t2 FOR UPDATE) AS z ON yb_locks_t.k = z.k1 WHERE yb_locks_t.k > 0 AND yb_locks_t.k <= 5;'
\i :iter_P2

-- Test when limit returns less than filtered query
\set query ':P SELECT * FROM yb_locks_t WHERE yb_locks_t.k < 6 FOR UPDATE LIMIT 3;'
\i :iter_P2

-- Test limit with a Sort plan node between LockRows and Scan node
\set query ':P SELECT * FROM yb_locks_t ORDER BY k LIMIT 13 FOR UPDATE;'
\i :iter_P2

-- Test when multiple limit nodes
\set query ':P SELECT * FROM (SELECT * FROM yb_locks_t LIMIT 13 FOR UPDATE) AS y JOIN (SELECT * FROM yb_locks_t2 LIMIT 3 FOR UPDATE) AS z ON y.k = z.k1;'
\i :iter_P2

-- Test when FOR UPDATE is supposed to acquire less locks than the limit (1 row on yb_locks_t, but all limited rows on yb_locks_t2)
\set query ':P SELECT * FROM (SELECT * FROM yb_locks_t ORDER BY k LIMIT 10 FOR UPDATE) AS y JOIN (SELECT * FROM yb_locks_t2 ORDER BY k1 LIMIT 3 FOR UPDATE) AS z ON y.k = z.k1 LIMIT 1;'
\i :iter_P2

-- Test with CTE subquery locking
\set query ':P WITH cte AS (SELECT * FROM yb_locks_t FOR UPDATE) SELECT * FROM cte;'
\i :iter_P2

-- Test with multiple CTE subquery locking
\set query ':P WITH cte_yb_locks_t AS (SELECT * FROM yb_locks_t FOR UPDATE), cte_yb_locks_t2 AS (SELECT * FROM yb_locks_t2 FOR UPDATE) SELECT * FROM cte_yb_locks_t JOIN cte_yb_locks_t2 ON cte_yb_locks_t.k = cte_yb_locks_t2.k1 LIMIT 2;'
\i :iter_P2

-- Test top level limit with multiple CTE subquery locking
\set query ':P WITH cte_yb_locks_t AS (SELECT * FROM yb_locks_t FOR UPDATE), cte_yb_locks_t2 AS (SELECT * FROM yb_locks_t2 FOR UPDATE) SELECT * FROM cte_yb_locks_t JOIN cte_yb_locks_t2 ON cte_yb_locks_t.k = cte_yb_locks_t2.k1 LIMIT 2;'
\i :iter_P2

-- Test LockRows node is used with ASC table when YB Sequential Scan is used.
\set Q1 '/*+ SeqScan(yb_locks_tasc) */'
\set query ':P :Q1 SELECT * FROM yb_locks_tasc WHERE k = 1 FOR UPDATE;'
\i :iter_P2

-- In isolation level SERIALIZABLE, all locks are done during scans.
BEGIN ISOLATION LEVEL SERIALIZABLE;

-- Test same locking as for REPEATABLE READ (default isolation).
\set query ':P SELECT * FROM yb_locks_t WHERE k = 5 FOR UPDATE;'
\i :iter_P2

-- Test no LockRows node for sequential scan.
\set query ':P SELECT * FROM yb_locks_t FOR UPDATE;'
\i :iter_P2

-- Test no LockRows node for join.
\set query ':P SELECT * FROM yb_locks_t2, yb_locks_t WHERE yb_locks_t2.k1 = yb_locks_t.k FOR UPDATE;'
\i :iter_P2

-- Test locking, and no LockRows node, when using an ASC table and YB Sequential Scan.
-- (No WHERE clause.)
\set Q1 '/*+ SeqScan(yb_locks_tasc) */'
\set query ':P :Q1 SELECT * FROM yb_locks_tasc FOR UPDATE;'
\i :iter_P2

-- For an ASC table, should lock inline, with no LockRows node.
\set query ':P SELECT * FROM yb_locks_tasc ORDER BY k FOR UPDATE;'
\i :iter_P2

COMMIT;

-- Test with single-RPC select+lock turned off.
SET yb_lock_pk_single_rpc TO OFF;

\set query ':P SELECT * FROM yb_locks_t WHERE k = 5 FOR UPDATE;'
\i :iter_P2

-- Test that with the yb_lock_pk_single_rpc off, SERIALIZABLE still locks during the scan
-- (no LockRows).
BEGIN ISOLATION LEVEL SERIALIZABLE;
\set query ':P SELECT * FROM yb_locks_t WHERE k = 5 FOR UPDATE;'
\i :iter_P2
COMMIT;

SET yb_lock_pk_single_rpc TO ON;

CREATE INDEX ON yb_locks_t2 (v);

-- Test with an index. We use a LockRows node for an index.
\set query ':P SELECT * FROM yb_locks_t2 WHERE v = 4 FOR UPDATE;'
\i :iter_P2

-- Test only the indexed column.
\set query ':P SELECT v FROM yb_locks_t2 WHERE v = 4 FOR UPDATE;'
\i :iter_P2

-- Isolation level SERIALIZABLE still locks with the scan though (no LockRows).
BEGIN ISOLATION LEVEL SERIALIZABLE;

\set query ':P SELECT * FROM yb_locks_t2 WHERE v = 4 FOR UPDATE;'
\i :iter_P2

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
\set query 'EXECUTE yb_locks_plan_rr(1);'
:query
-- The $1 in the EXPLAIN output tells you it's a stored plan.
\set query ':explain EXECUTE yb_locks_plan_rr(1);'
:query

BEGIN ISOLATION LEVEL SERIALIZABLE;
-- The LockRows node has a "no-op" annotation.
\set query ':P EXECUTE yb_locks_plan_rr(1);'
\i :iter_P2
-- In JSON mode, the LockRows node has an "Executes" field set to false.
\set query 'EXECUTE yb_locks_plan_rr(1);'
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF, FORMAT JSON)
:query
COMMIT;

-- Test that prepared statements made in isolation level SERIALIZABLE, but for a PK, are
-- able to lock PK when run in RR and RC.
SET yb_lock_pk_single_rpc TO ON;
BEGIN ISOLATION LEVEL SERIALIZABLE;
PREPARE yb_locks_plan_ser(INT) AS SELECT * FROM yb_locks_t WHERE k = $1 FOR UPDATE;
\set query ':P EXECUTE yb_locks_plan_ser(1);'
\i :iter_P2
COMMIT;

\set query ':P EXECUTE yb_locks_plan_ser(1);'
\i :iter_P2

-- Test that prepared statements made in isolation level SERIALIZABLE, for a non-PK, have
-- a LockRows node that functions in RR and RC.
BEGIN ISOLATION LEVEL SERIALIZABLE;
PREPARE yb_locks_plan_ser_all(INT) AS SELECT * FROM yb_locks_t FOR UPDATE;
\set query ':P EXECUTE yb_locks_plan_ser_all(1);'
\i :iter_P2
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
