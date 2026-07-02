--
-- YB tests for SKIP LOCKED with parallel read-ahead batching.
--
\set ECHO queries

\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/explain_filters.sql'
\i :filename

CREATE FUNCTION yb_lockrows_max_read_aheads(query text) RETURNS SETOF text
LANGUAGE plpgsql AS $$
BEGIN
    -- DEBUG is required to emit Max Read Ahead; only that field is printed below.
    PERFORM set_config('yb_explain_hide_non_deterministic_fields', 'off', true);
    RETURN QUERY
        SELECT ln
        FROM explain_filter('EXPLAIN (ANALYZE, DEBUG, DIST, COSTS OFF, SUMMARY OFF) ' || query) ln
        WHERE ln LIKE '%Max Read Ahead%';
    PERFORM set_config('yb_explain_hide_non_deterministic_fields', 'on', true);
END;
$$;

-- ASC primary key: deterministic key ordering in lock output.
CREATE TABLE t_asc (k INT, v INT, PRIMARY KEY (k ASC));
INSERT INTO t_asc SELECT i, i FROM generate_series(1, 15) AS i;

-- Multi-column primary key (hash + range).
CREATE TABLE t_multi (k1 INT, k2 INT, k3 INT, v INT, PRIMARY KEY(k1, k2, k3));
INSERT INTO t_multi SELECT x, x+1, x+2, x+3 FROM generate_series(1, 13, 4) AS s(x);

-- Table with a secondary index.
CREATE TABLE t_with_idx (k INT, v INT, PRIMARY KEY (k ASC));
CREATE INDEX t_with_idx_v ON t_with_idx (v ASC);
INSERT INTO t_with_idx SELECT i, i*2 FROM generate_series(1, 10) AS i;

-- Partitioned table.
CREATE TABLE t_part (a INT PRIMARY KEY) PARTITION BY RANGE (a);
CREATE TABLE t_part_lo PARTITION OF t_part FOR VALUES FROM (1) TO (6);
CREATE TABLE t_part_hi PARTITION OF t_part FOR VALUES FROM (6) TO (11);
INSERT INTO t_part SELECT i FROM generate_series(1, 10) AS i;

-- yb_lock_status is inspected to validate that locks are acquired on the correct rows.
-- Default yb_locks_min_txn_age (1s) hides locks from the fresh transactions used here.
-- Use a sufficiently long sleep so that the locks are visible in yb_lock_status particularly in sanitizer builds.
\set lock_status_for_rows 'DO $$ BEGIN PERFORM pg_sleep(1.5); END $$; SELECT l.locktype, l.relation::regclass::text AS relation, l.mode, l.hash_cols, l.range_cols FROM yb_lock_status(null, null) l WHERE l.locktype = \'row\' ORDER BY l.relation::regclass::text, l.hash_cols NULLS FIRST, l.range_cols NULLS FIRST'
-- Extract only the deterministic Max Read Ahead lines from EXPLAIN DEBUG (batching enabled when present).
\set max_read_ahead_for_q 'SELECT * FROM yb_lockrows_max_read_aheads(:''q'')'

-- Template for the standard test pattern:
-- runs :q inside a transaction, inspects lock status then EXPLAINs and extracts Max Read Ahead, then rolls back the transaction.
\set txn_with_q 'BEGIN; :q; :lock_status_for_rows; EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF) :q; :max_read_ahead_for_q; ROLLBACK;'

-- -------------------------------------------------------------------------
-- Section 1: Scans with SKIP LOCKED
-- -------------------------------------------------------------------------

-- Seq scan, no filter, no LIMIT -- all 15 rows locked (baseline).
\set q 'SELECT k FROM t_asc ORDER BY v FOR UPDATE SKIP LOCKED'
:txn_with_q

-- Seq scan, ORDER BY + LIMIT 5 -- exactly 5 rows returned and locked.
\set q 'SELECT k FROM t_asc ORDER BY v LIMIT 5 FOR UPDATE SKIP LOCKED'
:txn_with_q

-- Seq scan, WHERE clause with storage filter -- 5 rows pass storage filter and are locked.
\set q 'SELECT k FROM t_asc WHERE k < 6 ORDER BY v FOR UPDATE SKIP LOCKED'
:txn_with_q

-- Seq scan, WHERE clause with local filter -- 7 rows pass local filter; only those are locked.
\set q 'SELECT k FROM t_asc WHERE k::text LIKE \'%1%\' ORDER BY v FOR UPDATE SKIP LOCKED'
:txn_with_q

-- PK index scan, storage filter + LIMIT 3 -- exactly 3 rows returned and locked.
\set q 'SELECT k FROM t_asc WHERE k < 6 LIMIT 3 FOR UPDATE SKIP LOCKED'
:txn_with_q

-- Secondary index scan -- 5 rows locked.
\set q 'SELECT * FROM t_with_idx WHERE v > 10 ORDER BY k FOR UPDATE SKIP LOCKED'
:txn_with_q

-- Secondary index scan, local filter -- 5 rows locked.
\set q 'SELECT * FROM t_with_idx WHERE v::text LIKE \'%1%\' ORDER BY (k + v) FOR UPDATE SKIP LOCKED'
:txn_with_q

-- CTE with locking inside -- all CTE rows locked; outer ORDER BY k adds a Sort but no extra locks.
\set q 'WITH cte AS (SELECT k FROM t_asc FOR UPDATE SKIP LOCKED) SELECT k FROM cte ORDER BY k'
:txn_with_q

-- LIMIT outside MATERIALIZED CTE -- only 5 rows must be locked.
\set q 'WITH cte AS MATERIALIZED (SELECT k FROM t_asc FOR UPDATE SKIP LOCKED) SELECT k FROM cte LIMIT 5'
:txn_with_q

-- LIMIT outside NOT MATERIALIZED CTE -- planner ignores the materialization hint; only 5 rows must be locked.
\set q 'WITH cte AS NOT MATERIALIZED (SELECT k FROM t_asc FOR UPDATE SKIP LOCKED) SELECT k FROM cte LIMIT 5'
:txn_with_q

-- LIMIT inside the CTE -- exactly 5 rows locked.
\set q 'WITH cte AS (SELECT k FROM t_asc ORDER BY v LIMIT 5 FOR UPDATE SKIP LOCKED) SELECT k FROM cte'
:txn_with_q

-- SKIP LOCKED inside, LIMIT inside, ORDER BY outside -- exactly 5 rows locked.
\set q 'WITH cte AS (SELECT k FROM t_asc ORDER BY v LIMIT 5 FOR UPDATE SKIP LOCKED) SELECT k FROM cte ORDER BY k'
:txn_with_q

-- LIMIT inside CTE, ORDER BY + SKIP LOCKED outside.
-- FOR UPDATE here is a no-op as Postgres does not apply the locking clause to a CTE scan.
\set q 'WITH cte AS (SELECT k FROM t_asc LIMIT 5) SELECT k FROM cte ORDER BY k FOR UPDATE SKIP LOCKED'
:txn_with_q

-- SKIP LOCKED inside, LIMIT + SKIP LOCKED outside. SKIP LOCKED outside is a no-op.
-- Inner CTE locks all 15 rows and returns 5 rows. This is expected behavior.
\set q 'WITH cte AS (SELECT k FROM t_asc FOR UPDATE SKIP LOCKED) SELECT k FROM cte ORDER BY k LIMIT 5 FOR UPDATE SKIP LOCKED'
:txn_with_q

-- Two LIMITs: LIMIT 10 inside CTE bounds LockRows; outer LIMIT 3 trims the already-locked rows.
\set q 'WITH cte AS (SELECT k FROM t_asc ORDER BY v LIMIT 10 FOR UPDATE SKIP LOCKED) SELECT k FROM cte ORDER BY k LIMIT 3'
:txn_with_q

-- ORDER BY inside CTE (no locking), outer ORDER BY + LIMIT + SKIP LOCKED. SKIP LOCKED outside is a no-op.
\set q 'WITH cte AS (SELECT k FROM t_asc ORDER BY v) SELECT k FROM cte ORDER BY k LIMIT 5 FOR UPDATE SKIP LOCKED'
:txn_with_q

-- Nested CTEs: inner CTE locks all rows; second CTE filters by k < 6. This is expected behavior as the inner CTE is fully materialized.
\set q 'WITH locked AS (SELECT k FROM t_asc FOR UPDATE SKIP LOCKED), filtered AS (SELECT k FROM locked WHERE k < 6) SELECT k FROM filtered ORDER BY k'
:txn_with_q

-- SKIP LOCKED inside a subquery.
\set q 'SELECT k FROM (SELECT k FROM t_asc FOR UPDATE SKIP LOCKED) sub WHERE k < 6 ORDER BY k'
:txn_with_q

-- SKIP LOCKED + LIMIT inside a subquery.
\set q 'SELECT k FROM (SELECT k FROM t_asc FOR UPDATE SKIP LOCKED LIMIT 3) sub WHERE k < 6 ORDER BY k'
:txn_with_q

-- SKIP LOCKED outside a subquery -- the planner pushes a LockRows node inside the subquery.
-- The ORDER BY prevents the planner from optimizing the subquery away.
\set q 'SELECT k FROM (SELECT k FROM t_asc ORDER BY v) sub FOR UPDATE SKIP LOCKED'
:txn_with_q

-- Partitioned table -- rows locked across both partitions.
\set q 'SELECT * FROM t_part ORDER BY a FOR UPDATE SKIP LOCKED'
:txn_with_q

-- OFFSET 3 + LIMIT 5 -- offset rows also locked per spec; 8 rows total locked.
\set q 'SELECT k FROM t_asc ORDER BY v OFFSET 3 LIMIT 5 FOR UPDATE SKIP LOCKED'
:txn_with_q

-- Two LIMITs; LockRows is child of the inner (larger) LIMIT 10; outer LIMIT 3 trims only.
\set q 'SELECT k FROM (SELECT k FROM t_asc ORDER BY v LIMIT 10 FOR UPDATE SKIP LOCKED) sub LIMIT 3'
:txn_with_q

-- Two LIMITs; LockRows is child of the outer (smaller) LIMIT 3.
\set q 'SELECT k FROM (SELECT k FROM t_asc ORDER BY v LIMIT 10) sub LIMIT 3 FOR UPDATE SKIP LOCKED'
:txn_with_q

-- Sort node between LIMIT and LockRows.
\set q 'SELECT k FROM (SELECT k FROM t_asc FOR UPDATE SKIP LOCKED) sub ORDER BY k LIMIT 5'
:txn_with_q

-- -------------------------------------------------------------------------
-- Section 2: Joins with SKIP LOCKED
-- -------------------------------------------------------------------------
--
-- t_a: 9 rows (id 1-9).
--   id 9 has no children in t_b.
--   grp: 1 for ids 1-4, 2 for ids 5-9 - simple equality pushable to DocDB.
--   tag: 'item-N' - cast+LIKE on this forces local (Postgres) evaluation.
-- t_b: 24 rows, 3 children per parent for ids 1-8.
--   parent_id: (cid-1)/3 + 1.
--   val: cid * 10 - range filter on val is pushable to DocDB.
--   flag: 'odd'/'even' based on cid parity - local filter via LIKE.
-- ord mirrors id/cid but is not indexed; ORDER BY ord forces a Sort node.

CREATE TABLE t_a (id INT, grp INT, tag TEXT, ord INT, PRIMARY KEY (id ASC));
INSERT INTO t_a (SELECT i, CASE WHEN i <= 4 THEN 1 ELSE 2 END, 'item-' || i, i FROM generate_series(1, 8) AS i);
INSERT INTO t_a VALUES (9, 2, 'item-9', 9); -- no children (left join test)

CREATE TABLE t_b (cid INT, parent_id INT, val INT, flag TEXT, ord INT, PRIMARY KEY (cid ASC));
INSERT INTO t_b (SELECT i, (i - 1) / 3 + 1, i * 10, CASE WHEN i % 2 = 0 THEN 'even' ELSE 'odd' END, i FROM generate_series(1, 24) AS i);

CREATE INDEX t_b_parent_idx ON t_b (parent_id ASC);
CREATE INDEX t_b_val_idx    ON t_b (val ASC);

-- Inner join, FOR UPDATE both tables, full scan without LIMIT.
-- Duplicate locking: each t_a row is locked N times for N join output rows (GH-32024).
\set q 'SELECT t_a.id, t_b.cid FROM t_a JOIN t_b ON t_a.id = t_b.parent_id ORDER BY t_a.id, t_b.cid FOR UPDATE SKIP LOCKED'
:txn_with_q

-- Inner join, FOR UPDATE OF t_a only without LIMIT; t_a locked, t_b read but unlocked.
-- Duplicate locking: each t_a row is locked N times for N join output rows (GH-32024).
\set q 'SELECT t_a.id, t_b.cid FROM t_a JOIN t_b ON t_a.id = t_b.parent_id ORDER BY t_a.id, t_b.cid FOR UPDATE OF t_a SKIP LOCKED'
:txn_with_q

-- Left outer join; t_a locked, t_b untouched.
\set q 'SELECT t_a.id, t_b.cid FROM t_a LEFT JOIN t_b ON t_a.id = t_b.parent_id WHERE t_a.id = 9 ORDER BY t_a.ord FOR UPDATE OF t_a SKIP LOCKED'
:txn_with_q

-- Inner join, LIMIT 5 with Sort node under LockRows.
-- Duplicate locking: t_a id=1 locked 3x, id=2 locked 2x for the 5 output rows (GH-32024).
\set q 'SELECT t_a.id, t_b.cid FROM t_a JOIN t_b ON t_a.id = t_b.parent_id ORDER BY t_a.id, t_b.cid LIMIT 5 FOR UPDATE SKIP LOCKED'
:txn_with_q

-- Self-join, LIMIT 5 -- only 'x' alias instances locked.
\set q 'SELECT x.id AS x_id, y.id AS y_id FROM t_a x JOIN t_a y ON x.id = y.id ORDER BY x.ord LIMIT 5 FOR UPDATE OF x SKIP LOCKED'
:txn_with_q

-- LIMIT inside a subquery, then outer join.
\set q 'SELECT sub.id, t_b.cid FROM (SELECT * FROM t_a ORDER BY ord LIMIT 3 FOR UPDATE SKIP LOCKED) sub JOIN t_b ON sub.id = t_b.parent_id ORDER BY sub.ord, t_b.cid'
:txn_with_q

-- Storage filter on left + LIMIT 5 -- only grp=1 rows appear in lock set.
-- Duplicate locking: each t_a row is locked N times for N join output rows (GH-32024).
\set q 'SELECT t_a.id, t_b.cid FROM t_a JOIN t_b ON t_a.id = t_b.parent_id WHERE t_a.grp = 1 ORDER BY t_a.id, t_b.cid LIMIT 5 FOR UPDATE SKIP LOCKED'
:txn_with_q

-- Storage filter on right + LIMIT 5.
-- Duplicate locking: each t_a row is locked N times for N join output rows (GH-32024).
\set q 'SELECT t_a.id, t_b.cid FROM t_a JOIN t_b ON t_a.id = t_b.parent_id WHERE t_b.val < 70 ORDER BY t_a.id, t_b.cid LIMIT 5 FOR UPDATE SKIP LOCKED'
:txn_with_q

-- Storage filters on both sides + LIMIT 5.
-- Duplicate locking: each t_a row is locked N times for N join output rows (GH-32024).
\set q 'SELECT t_a.id, t_b.cid FROM t_a JOIN t_b ON t_a.id = t_b.parent_id WHERE t_a.grp = 1 AND t_b.val < 70 ORDER BY t_a.id, t_b.cid LIMIT 5 FOR UPDATE SKIP LOCKED'
:txn_with_q

-- Local filter on left + LIMIT 5.
-- Duplicate locking: each t_a row is locked N times for N join output rows (GH-32024).
\set q 'SELECT t_a.id, t_b.cid FROM t_a JOIN t_b ON t_a.id = t_b.parent_id WHERE t_a.id::text LIKE \'%1%\' ORDER BY t_a.id, t_b.cid LIMIT 5 FOR UPDATE SKIP LOCKED'
:txn_with_q

-- Local filter on right + LIMIT 5.
-- Duplicate locking: each t_a row is locked N times for N join output rows (GH-32024).
\set q 'SELECT t_a.id, t_b.cid FROM t_a JOIN t_b ON t_a.id = t_b.parent_id WHERE t_b.cid::text LIKE \'%1%\' ORDER BY t_a.id, t_b.cid LIMIT 5 FOR UPDATE SKIP LOCKED'
:txn_with_q

-- Cross-table local filter + LIMIT 5.
-- Duplicate locking: each t_a row is locked N times for N join output rows (GH-32024).
\set q 'SELECT t_a.id, t_b.cid FROM t_a JOIN t_b ON t_a.id = t_b.parent_id WHERE t_a.id + t_b.cid > 15 ORDER BY t_a.id, t_b.cid LIMIT 5 FOR UPDATE SKIP LOCKED'
:txn_with_q

-- Function Scan (unnest) joined to t_a -- only t_a rows are locked.
\set q 'SELECT t_a.id FROM t_a JOIN unnest(ARRAY[1, 2, 3, 4]) AS ids(id) ON t_a.id = ids.id ORDER BY t_a.ord FOR UPDATE OF t_a SKIP LOCKED'
:txn_with_q

-- Bitmap Table Scan.
-- Duplicate locking: t_a id=1 locked 3x for 3 join output rows (GH-32024).
SET yb_enable_bitmapscan = on;
\set q 'SELECT t_b.cid, t_a.id FROM t_b JOIN t_a ON t_b.parent_id = t_a.id WHERE t_b.parent_id = 1 OR t_b.val = 60 ORDER BY t_b.ord FOR UPDATE SKIP LOCKED'
:txn_with_q
RESET yb_enable_bitmapscan;

-- Join (many-to-one) between LIMIT and LockRows.
\set q 'SELECT sub.k, t_multi.v FROM (SELECT k FROM t_asc ORDER BY v FOR UPDATE SKIP LOCKED) sub JOIN t_multi ON sub.k = t_multi.k1 LIMIT 3'
:txn_with_q

-- -------------------------------------------------------------------------
-- Section 3: Multiple LockRows nodes locking different tables
-- -------------------------------------------------------------------------
-- t_a: 9 rows (id 1-9), 3 t_b children per parent for ids 1-8.
-- t_b: 24 rows (cid 1-24), parent_id = (cid-1)/3 + 1.

-- Two CTEs each with a LIMIT-bounded LockRows on a different table; inner join.
-- t_a: 3 locked (ids 1-3); t_b: 9 locked (cids 1-9); 9 rows returned.
\set q 'WITH locked_a AS (SELECT id FROM t_a ORDER BY ord LIMIT 3 FOR UPDATE SKIP LOCKED), locked_b AS (SELECT cid, parent_id FROM t_b ORDER BY ord LIMIT 9 FOR UPDATE SKIP LOCKED) SELECT locked_a.id, locked_b.cid FROM locked_a JOIN locked_b ON locked_a.id = locked_b.parent_id ORDER BY locked_a.id, locked_b.cid'
:txn_with_q

-- Same query with subqueries instead of CTEs; LockRows nodes visible inline.
-- t_a: 3 locked; t_b: 9 locked; 9 rows returned.
\set q 'SELECT sub_a.id, sub_b.cid FROM (SELECT id FROM t_a ORDER BY ord LIMIT 3 FOR UPDATE SKIP LOCKED) sub_a JOIN (SELECT cid, parent_id FROM t_b ORDER BY ord LIMIT 9 FOR UPDATE SKIP LOCKED) sub_b ON sub_a.id = sub_b.parent_id ORDER BY sub_a.id, sub_b.cid'
:txn_with_q

-- Two CTEs with different limits, cross join; lock counts completely independent.
-- t_a: 2 locked (ids 1-2); t_b: 4 locked (cids 1-4); 2x4 = 8 rows returned.
\set q 'WITH locked_a AS (SELECT id FROM t_a ORDER BY ord LIMIT 2 FOR UPDATE SKIP LOCKED), locked_b AS (SELECT cid FROM t_b ORDER BY ord LIMIT 4 FOR UPDATE SKIP LOCKED) SELECT locked_a.id, locked_b.cid FROM locked_a CROSS JOIN locked_b ORDER BY locked_a.id, locked_b.cid'
:txn_with_q

-- Inner limits bound each table independently; outer LIMIT 6 only trims the result.
-- Sort exhausts both subqueries before outer Limit applies.
-- t_a: 4 locked (id=4 not in output); t_b: 12 locked (cids 7-12 not in output); 6 rows returned.
\set q 'SELECT sub_a.id, sub_b.cid FROM (SELECT id FROM t_a ORDER BY ord LIMIT 4 FOR UPDATE SKIP LOCKED) sub_a JOIN (SELECT cid, parent_id FROM t_b ORDER BY ord LIMIT 12 FOR UPDATE SKIP LOCKED) sub_b ON sub_a.id = sub_b.parent_id ORDER BY sub_a.id, sub_b.cid LIMIT 6'
:txn_with_q

-- Section 3b: Asymmetric LockRows -- one join input has its own LockRows (higher
-- limit) and the join output also carries a LockRows (lower limit), locking a
-- different table.

-- Subquery LockRows bounds t_a to LIMIT 8 (inner); outer FOR UPDATE SKIP LOCKED with LIMIT 3.
-- t_a ids 1-8 locked by inner; outer locks t_b cids 1-3 only.
\set q 'SELECT sub.id, t_b.cid FROM (SELECT id FROM t_a ORDER BY ord LIMIT 8 FOR UPDATE SKIP LOCKED) sub JOIN t_b ON sub.id = t_b.parent_id ORDER BY sub.id, t_b.cid LIMIT 3 FOR UPDATE SKIP LOCKED'
:txn_with_q

-- Reversed: subquery LockRows bounds t_b to LIMIT 12 (inner); outer FOR UPDATE SKIP LOCKED with LIMIT 4.
-- t_b: cids 1-12 locked by inner; t_a: ids 1-2 locked by outer, id=1 locked 3x (GH-32024).
\set q 'SELECT t_a.id, sub.cid FROM t_a JOIN (SELECT cid, parent_id FROM t_b ORDER BY ord LIMIT 12 FOR UPDATE SKIP LOCKED) sub ON t_a.id = sub.parent_id ORDER BY t_a.id, sub.cid LIMIT 4 FOR UPDATE SKIP LOCKED'
:txn_with_q

-- CTE variant: CTE LockRows bounds t_a to LIMIT 6 (inner); outer FOR UPDATE SKIP LOCKED with LIMIT 3.
-- t_a: ids 1-6 locked (CTE); t_b: cids 1-3 locked (outer LockRows); CTE scan has no ctid so t_a not re-locked.
\set q 'WITH locked_a AS (SELECT id FROM t_a ORDER BY ord LIMIT 6 FOR UPDATE SKIP LOCKED) SELECT locked_a.id, t_b.cid FROM locked_a JOIN t_b ON locked_a.id = t_b.parent_id ORDER BY locked_a.id, t_b.cid LIMIT 3 FOR UPDATE SKIP LOCKED'
:txn_with_q

-- Three LockRows: sub_a (t_a LIMIT 6), sub_b (t_b LIMIT 9), outer (both LIMIT 3).
-- Outer re-locks t_a id=1 (3x GH-32024) and t_b cids 1-3 already locked by inner nodes.
\set q 'SELECT sub_a.id, sub_b.cid FROM (SELECT id FROM t_a ORDER BY ord LIMIT 6 FOR UPDATE SKIP LOCKED) sub_a JOIN (SELECT cid, parent_id FROM t_b ORDER BY ord LIMIT 9 FOR UPDATE SKIP LOCKED) sub_b ON sub_a.id = sub_b.parent_id ORDER BY sub_a.id, sub_b.cid LIMIT 3 FOR UPDATE SKIP LOCKED'
:txn_with_q

-- -------------------------------------------------------------------------
-- Section 4: Error cases -- illegal combinations
-- -------------------------------------------------------------------------

-- SKIP LOCKED is incompatible with WITH TIES.
SELECT k FROM t_asc ORDER BY k FETCH FIRST 5 ROWS WITH TIES FOR UPDATE SKIP LOCKED;

-- SKIP LOCKED is incompatible with DISTINCT.
SELECT DISTINCT k FROM t_asc FOR UPDATE SKIP LOCKED;

-- Locking is incompatible with GROUP BY.
SELECT k, count(*) FROM t_asc GROUP BY k FOR UPDATE SKIP LOCKED;

-- Locking is incompatible with aggregates.
SELECT k, count(*) FROM t_asc FOR UPDATE SKIP LOCKED;

-- Locking is incompatible with UNION.
SELECT k FROM t_asc
UNION
SELECT k FROM t_asc
FOR UPDATE SKIP LOCKED;

-- -------------------------------------------------------------------------
-- Section 5: Cursors with SKIP LOCKED
-- -------------------------------------------------------------------------
-- DECLARE alone -- no locks acquired.
BEGIN;
DECLARE cur CURSOR FOR SELECT * FROM t_asc FOR UPDATE SKIP LOCKED;
:lock_status_for_rows;
CLOSE cur;
ROLLBACK;

-- FETCH 1 -- returns 1 row and is expected to lock 1 row.
-- Overlocking: 3 extra rows (batch size = 4)locked beyond what was requested (GH-32001).
BEGIN;
DECLARE cur CURSOR FOR SELECT * FROM t_asc FOR UPDATE SKIP LOCKED;
FETCH 1 FROM cur;
:lock_status_for_rows;
CLOSE cur;
ROLLBACK;

-- FETCH 4 (batch size = 4) -- locks exactly 1 batch.
BEGIN;
DECLARE cur CURSOR FOR SELECT * FROM t_asc FOR UPDATE SKIP LOCKED;
FETCH 4 FROM cur;
:lock_status_for_rows;
CLOSE cur;
ROLLBACK;

-- FETCH 0 -- no rows returned, no locks acquired.
BEGIN;
DECLARE cur CURSOR FOR SELECT * FROM t_asc FOR UPDATE SKIP LOCKED;
FETCH 0 FROM cur;
:lock_status_for_rows;
CLOSE cur;
ROLLBACK;

-- FETCH ALL -- all rows returned and locked.
BEGIN;
DECLARE cur CURSOR FOR SELECT * FROM t_asc FOR UPDATE SKIP LOCKED;
FETCH ALL FROM cur;
:lock_status_for_rows;
CLOSE cur;
ROLLBACK;

-- -------------------------------------------------------------------------
-- Section 6: Partitioned tables with SKIP LOCKED
-- -------------------------------------------------------------------------
-- t_part: 10 rows (a=1-10). t_part_lo: a=1-5, t_part_hi: a=6-10.

-- LIMIT 3 stays within t_part_lo; 3 rows locked.
\set q 'SELECT * FROM t_part ORDER BY a LIMIT 3 FOR UPDATE SKIP LOCKED'
:txn_with_q

-- LIMIT 7 spans both partitions; 5 rows from t_part_lo + 2 from t_part_hi locked.
\set q 'SELECT * FROM t_part ORDER BY a LIMIT 7 FOR UPDATE SKIP LOCKED'
:txn_with_q

-- WHERE a < 6 prunes to t_part_lo; all 5 rows locked.
\set q 'SELECT * FROM t_part WHERE a < 6 ORDER BY a FOR UPDATE SKIP LOCKED'
:txn_with_q

-- WHERE a < 6 + LIMIT 3; 3 rows from t_part_lo locked.
\set q 'SELECT * FROM t_part WHERE a < 6 ORDER BY a LIMIT 3 FOR UPDATE SKIP LOCKED'
:txn_with_q

-- Lock a leaf partition directly (no Append node); 5 rows locked from t_part_lo.
\set q 'SELECT * FROM t_part_lo ORDER BY a FOR UPDATE SKIP LOCKED'
:txn_with_q

-- OFFSET 3 LIMIT 4 spanning the partition boundary.
-- Offset rows (a=1-3) also locked per spec; 7 rows total locked.
\set q 'SELECT * FROM t_part ORDER BY a OFFSET 3 LIMIT 4 FOR UPDATE SKIP LOCKED'
:txn_with_q

-- Partitioned table joined to t_asc; 5 t_part rows locked, no duplicate locking (1:1 join).
\set q 'SELECT t_part.a FROM t_part JOIN t_asc ON t_part.a = t_asc.k ORDER BY t_part.a LIMIT 5 FOR UPDATE OF t_part SKIP LOCKED'
:txn_with_q

-- -------------------------------------------------------------------------
-- Section 7: DML with SKIP LOCKED
-- -------------------------------------------------------------------------
CREATE TABLE t_queue (id INT, status TEXT, ord INT, PRIMARY KEY (id ASC));
INSERT INTO t_queue SELECT i, 'pending', i FROM generate_series(1, 10) AS i;

-- Template for DML: run once to validate locks, then roll back. Plan shape and Max Read Ahead
-- each run in their own rolled-back transactions so EXPLAIN ANALYZE does not double-apply DML.
\set dml_q 'BEGIN ISOLATION LEVEL REPEATABLE READ; :q; :lock_status_for_rows; ROLLBACK; BEGIN ISOLATION LEVEL REPEATABLE READ; EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF) :q; ROLLBACK; BEGIN ISOLATION LEVEL REPEATABLE READ; :max_read_ahead_for_q; ROLLBACK;'

-- Baseline: verify lock count on the queue table (SELECT only).
\set q 'SELECT id, status FROM t_queue ORDER BY ord LIMIT 3 FOR UPDATE SKIP LOCKED'
:txn_with_q

-- Work-queue dequeue via CTE UPDATE; 3 rows locked and status set to processing.
\set q 'WITH next AS (SELECT id FROM t_queue ORDER BY id LIMIT 3 FOR UPDATE SKIP LOCKED) UPDATE t_queue SET status = \'processing\' WHERE id IN (SELECT id FROM next) RETURNING id, status'
:dml_q

-- Work-queue dequeue via CTE DELETE; 3 rows locked and deleted.
\set q 'WITH next AS (SELECT id FROM t_queue ORDER BY id LIMIT 3 FOR UPDATE SKIP LOCKED) DELETE FROM t_queue WHERE id IN (SELECT id FROM next) RETURNING id'
:dml_q

-- Inline-subquery UPDATE (no CTE); 5 rows locked and updated.
\set q 'UPDATE t_queue SET status = \'processing\' WHERE id IN (SELECT id FROM t_queue ORDER BY id LIMIT 5 FOR UPDATE SKIP LOCKED) RETURNING id, status'
:dml_q

-- DELETE from partitioned table via SKIP LOCKED; 3 rows locked and deleted from t_part_lo.
\set q 'WITH next AS (SELECT a FROM t_part ORDER BY a LIMIT 3 FOR UPDATE SKIP LOCKED) DELETE FROM t_part WHERE a IN (SELECT a FROM next) RETURNING a'
:dml_q

-- INSERT...SELECT with SKIP LOCKED; 5 source rows locked and copied with ord offset.
\set q 'INSERT INTO t_queue SELECT id + 100, status, ord FROM t_queue ORDER BY id LIMIT 5 FOR UPDATE SKIP LOCKED'
:dml_q

DROP TABLE t_asc, t_multi, t_with_idx, t_part, t_a, t_b, t_queue;
