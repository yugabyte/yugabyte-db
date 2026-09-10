--
-- Test ROW(yb_hash_code(hash_cols), ...) as Index Cond on hash indexes.
-- When yb_hash_code is the leading element and subsequent elements form
-- a prefix of the index key, the planner should recognize the ROW
-- comparison as an index condition.
-- See https://github.com/yugabyte/yugabyte-db/issues/30524
--

-- Single hash column, two range columns.
CREATE TABLE hc_rc_t1(
    h int,
    r1 int,
    r2 int,
    v text,
    PRIMARY KEY(h HASH, r1 ASC, r2 ASC)
);
INSERT INTO hc_rc_t1
    SELECT i % 5, i, i * 10, 'val' || i FROM generate_series(1, 50) i;

-------------------------------------------------------------------
-- Test 1: Full key ROW comparison with leading yb_hash_code.
-- Should show ROW(yb_hash_code...) as Index Cond, not Filter.
-------------------------------------------------------------------
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
SELECT * FROM hc_rc_t1
    WHERE (yb_hash_code(h), h, r1, r2) > (yb_hash_code(1), 1, 10, 100)
    LIMIT 5;

-------------------------------------------------------------------
-- Test 2: Partial key ROW comparison with leading yb_hash_code.
-- Should still be an Index Cond because it forms an index-key prefix.
-------------------------------------------------------------------
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
SELECT * FROM hc_rc_t1
    WHERE (yb_hash_code(h), h, r1) > (yb_hash_code(1), 1, 10)
    LIMIT 5;

-------------------------------------------------------------------
-- Test 3: Commuted ROW comparison with leading yb_hash_code.
-- Should be commuted into an Index Cond with index keys on the left.
-------------------------------------------------------------------
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
SELECT * FROM hc_rc_t1
    WHERE (yb_hash_code(3), 3, 30, 300) > (yb_hash_code(h), h, r1, r2)
    LIMIT 5;

-------------------------------------------------------------------
-- Test 4: Without leading yb_hash_code.
-- Should NOT be our Index Cond path (Seq Scan or Filter).
-------------------------------------------------------------------
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
SELECT * FROM hc_rc_t1
    WHERE (h, r1, r2) > (1, 10, 100)
    LIMIT 5;

-------------------------------------------------------------------
-- Test 5: ROW comparison with upper bound on yb_hash_code.
-- Both conditions should be Index Cond.
-------------------------------------------------------------------
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
SELECT * FROM hc_rc_t1
    WHERE (yb_hash_code(h), h, r1, r2) > (yb_hash_code(1), 1, 10, 100)
      AND yb_hash_code(h) < 50000
    LIMIT 5;

-------------------------------------------------------------------
-- Test 6: yb_hash_code arity mismatch (single arg on 2-col hash key).
-- Should NOT match our path.
-------------------------------------------------------------------
CREATE TABLE hc_rc_t2(
    h1 int,
    h2 int,
    r int,
    v text,
    PRIMARY KEY((h1, h2) HASH, r ASC)
);
INSERT INTO hc_rc_t2
    SELECT i % 3, i % 5, i, 'val' || i FROM generate_series(1, 30) i;

EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
SELECT * FROM hc_rc_t2
    WHERE (yb_hash_code(h1), h1, h2, r) > (yb_hash_code(1), 1, 2, 7)
    LIMIT 10;

-------------------------------------------------------------------
-- Test 7: Current non-goal: lossy prefix mismatch.
-- Should NOT match yet, because h2 is skipped before r.
-------------------------------------------------------------------
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
SELECT * FROM hc_rc_t2
    WHERE (yb_hash_code(h1, h2), h1, r) > (yb_hash_code(1, 2), 1, 7)
    LIMIT 10;

-------------------------------------------------------------------
-- Test 8: Less-than direction.
-- Should be Index Cond.
-------------------------------------------------------------------
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
SELECT * FROM hc_rc_t1
    WHERE (yb_hash_code(h), h, r1, r2) < (yb_hash_code(3), 3, 30, 300)
    LIMIT 5;

-------------------------------------------------------------------
-- Test 9: Mixed-direction range columns.
-- The row condition is still useful, with Postgres recheck preserving
-- correctness when DocDB can only use the safe prefix.
-------------------------------------------------------------------
CREATE TABLE hc_rc_t3(
    h int,
    r1 int,
    r2 int,
    v text,
    PRIMARY KEY(h HASH, r1 ASC, r2 DESC)
);
INSERT INTO hc_rc_t3
    SELECT i % 5, i, i * 10, 'val' || i FROM generate_series(1, 50) i;
INSERT INTO hc_rc_t3 VALUES
    (1, 10, 50, 'below'),
    (1, 10, 100, 'equal'),
    (1, 10, 150, 'above');

EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
SELECT * FROM hc_rc_t3
    WHERE (yb_hash_code(h), h, r1, r2) > (yb_hash_code(1), 1, 10, 100);

-------------------------------------------------------------------
-- Test 10: Verify execution correctness.
-- Pin to a known hash bucket and verify the ROW comparison filters
-- correctly within it.
-------------------------------------------------------------------
SELECT h, r1, r2 FROM hc_rc_t1
    WHERE yb_hash_code(h) = yb_hash_code(1)
      AND (yb_hash_code(h), h, r1, r2) > (yb_hash_code(1), 1, 6, 60)
    ORDER BY h, r1, r2;

-- Clean up.
DROP TABLE hc_rc_t1;
DROP TABLE hc_rc_t2;
DROP TABLE hc_rc_t3;
