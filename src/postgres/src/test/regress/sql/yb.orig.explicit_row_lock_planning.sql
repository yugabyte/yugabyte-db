\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/explainrun.sql'
\i :filename
\set explain 'EXPLAIN (COSTS OFF)'

--
-- YB tests for locking
--

CREATE TABLE yb_locks_t (k int PRIMARY KEY);
INSERT INTO yb_locks_t VALUES (1),(2),(3),(4),(5);

CREATE TABLE yb_locks_t2 (k1 int, k2 int, k3 int, v int, PRIMARY KEY(k1, k2, k3));
INSERT INTO yb_locks_t2 VALUES (1,2,3,4),(5,6,7,8);

CREATE TABLE yb_locks_tasc (k int, PRIMARY KEY (k ASC));
INSERT INTO yb_locks_tasc VALUES (1),(2),(3);

SET yb_lock_pk_single_rpc TO ON;

-- Test plain (unlocked case).
\set query 'SELECT * FROM yb_locks_t WHERE k=5'
:explain1run1

-- Test single-RPC select+lock (no LockRows node).
\set query 'SELECT * FROM yb_locks_t WHERE k=5 FOR UPDATE'
:explain1run1

-- Test other types of locking.
\set query 'SELECT * FROM yb_locks_t WHERE k=5 FOR SHARE'
:explain1run1

\set query 'SELECT * FROM yb_locks_t WHERE k=5 FOR NO KEY UPDATE'
:explain1run1

\set query 'SELECT * FROM yb_locks_t WHERE k=5 FOR KEY SHARE'
:explain1run1

-- Test LockRows node (more RPCs), and scan is unlocked.
\set query 'SELECT * FROM yb_locks_t FOR UPDATE'
:explain1run1

-- Test with multi-column primary key.
\set query 'SELECT * FROM yb_locks_t2 WHERE k1=1 AND k2=2 AND k3=3 FOR UPDATE'
:explain1run1

-- Test with partial column set for primary key (should use LockRows).
\set query 'SELECT * FROM yb_locks_t2 WHERE k1=1 AND k2=2 FOR UPDATE'
:explain1run1

-- Test LockRows node is used for join.
\set query 'SELECT * FROM yb_locks_t2, yb_locks_t WHERE yb_locks_t2.k1 = yb_locks_t.k FOR UPDATE'
:explain1run1

-- Test LockRows node is used with ASC table when YB Sequential Scan is used.
\set hint1 '/*+ SeqScan(yb_locks_tasc) */'
\set query 'SELECT * FROM yb_locks_tasc WHERE k=1 FOR UPDATE'
:explain1run1
\set hint1 ''

-- In isolation level SERIALIZABLE, all locks are done during scans.
BEGIN ISOLATION LEVEL SERIALIZABLE;

-- Test same locking as for REPEATABLE READ (default isolation).
\set query 'SELECT * FROM yb_locks_t WHERE k=5 FOR UPDATE'
:explain1run1

-- Test no LockRows node for sequential scan.
\set query 'SELECT * FROM yb_locks_t FOR UPDATE'
:explain1run1

-- Test no LockRows node for join.
\set query 'SELECT * FROM yb_locks_t2, yb_locks_t WHERE yb_locks_t2.k1 = yb_locks_t.k FOR UPDATE'
:explain1run1

-- Test locking, and no LockRows node, when using an ASC table and YB Sequential Scan.
-- (No WHERE clause.)
\set hint1 '/*+ SeqScan(yb_locks_tasc) */'
\set query 'SELECT * FROM yb_locks_tasc FOR UPDATE'
:explain1run1
\set hint1 ''

-- For an ASC table, should lock inline, with no LockRows node.
\set query 'SELECT * FROM yb_locks_tasc ORDER BY k FOR UPDATE'
:explain1run1

COMMIT;

-- Test with single-RPC select+lock turned off.
SET yb_lock_pk_single_rpc TO OFF;

\set query 'SELECT * FROM yb_locks_t WHERE k=5 FOR UPDATE'
:explain1run1

-- Test that with the yb_lock_pk_single_rpc off, SERIALIZABLE still locks during the scan
-- (no LockRows).
BEGIN ISOLATION LEVEL SERIALIZABLE;
\set query 'SELECT * FROM yb_locks_t WHERE k=5 FOR UPDATE'
:explain1run1
COMMIT;

SET yb_lock_pk_single_rpc TO ON;

CREATE INDEX ON yb_locks_t2 (v);

-- Test with an index. We use a LockRows node for an index.
\set query 'SELECT * FROM yb_locks_t2 WHERE v=4 FOR UPDATE'
:explain1run1

-- Test only the indexed column.
\set query 'SELECT v FROM yb_locks_t2 WHERE v=4 FOR UPDATE'
:explain1run1

-- Isolation level SERIALIZABLE still locks with the scan though (no LockRows).
BEGIN ISOLATION LEVEL SERIALIZABLE;

\set query 'SELECT * FROM yb_locks_t2 WHERE v=4 FOR UPDATE'
:explain1run1

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

-- Test that prepared statements made in isolation level RR with a LockRows node do not
-- crash when executed in isolation level SERIALIZABLE.
SET yb_lock_pk_single_rpc TO OFF;
-- Store prepared plans right away.
SET yb_test_planner_custom_plan_threshold to 1;
PREPARE yb_locks_plan_rr (int) AS SELECT * FROM yb_locks_t WHERE k=$1 FOR UPDATE;
\set query 'EXECUTE yb_locks_plan_rr(1)'
:query;
-- The $1 in the EXPLAIN output tells you it's a stored plan.
:explain1

BEGIN ISOLATION LEVEL SERIALIZABLE;
-- The LockRows node has a "no-op" annotation.
:explain1run1
-- In JSON mode, the LockRows node has an "Executes" field set to false.
EXPLAIN (COSTS OFF, FORMAT JSON)
EXECUTE yb_locks_plan_rr(1);
COMMIT;

-- Test that prepared statements made in isolation level SERIALIZABLE, but for a PK, are
-- able to lock PK when run in RR and RC.
SET yb_lock_pk_single_rpc TO ON;
BEGIN ISOLATION LEVEL SERIALIZABLE;
PREPARE yb_locks_plan_ser (int) AS SELECT * FROM yb_locks_t WHERE k=$1 FOR UPDATE;
\set query 'EXECUTE yb_locks_plan_ser(1)'
:explain1run1
COMMIT;

\set query 'EXECUTE yb_locks_plan_ser(1)'
:explain1run1

-- Test that prepared statements made in isolation level SERIALIZABLE, for a non-PK, have
-- a LockRows node that functions in RR and RC.
BEGIN ISOLATION LEVEL SERIALIZABLE;
PREPARE yb_locks_plan_ser_all (int) AS SELECT * FROM yb_locks_t FOR UPDATE;
\set query 'EXECUTE yb_locks_plan_ser_all(1)'
:explain1run1
COMMIT;
\set query 'EXECUTE yb_locks_plan_ser_all(1)'
:explain1run1

-- Reset
SET yb_lock_pk_single_rpc TO DEFAULT;
SET yb_test_planner_custom_plan_threshold TO DEFAULT;
DROP TABLE yb_locks_t, yb_locks_t2, yb_locks_tasc, yb_locks_partition;
