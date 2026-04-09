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

-- Multi-column hash key, one range column.
CREATE TABLE hc_rc_t2(
    h1 int,
    h2 int,
    r int,
    v text,
    PRIMARY KEY((h1, h2) HASH, r ASC)
);
INSERT INTO hc_rc_t2
    SELECT i % 3, i % 5, i, 'val' || i FROM generate_series(1, 30) i;

-------------------------------------------------------------------
-- Test 1: Full key ROW comparison with leading yb_hash_code.
-- Should be Index Cond.
-------------------------------------------------------------------
EXPLAIN (COSTS OFF)
SELECT * FROM hc_rc_t1
    WHERE (yb_hash_code(h), h, r1, r2) > (yb_hash_code(1), 1, 10, 100)
    LIMIT 5;

SELECT * FROM hc_rc_t1
    WHERE (yb_hash_code(h), h, r1, r2) > (yb_hash_code(1), 1, 10, 100)
    LIMIT 5;

-------------------------------------------------------------------
-- Test 2: Multi-column hash key with leading yb_hash_code.
-- Should be Index Cond.
-------------------------------------------------------------------
EXPLAIN (COSTS OFF)
SELECT * FROM hc_rc_t2
    WHERE (yb_hash_code(h1, h2), h1, h2, r) > (yb_hash_code(1, 2), 1, 2, 7)
    LIMIT 10;

SELECT * FROM hc_rc_t2
    WHERE (yb_hash_code(h1, h2), h1, h2, r) > (yb_hash_code(1, 2), 1, 2, 7)
    LIMIT 10;

-------------------------------------------------------------------
-- Test 3: Without leading yb_hash_code.
-- Should NOT be our Index Cond path (Seq Scan or Filter).
-------------------------------------------------------------------
EXPLAIN (COSTS OFF)
SELECT * FROM hc_rc_t1
    WHERE (h, r1, r2) > (1, 10, 100)
    LIMIT 5;

-------------------------------------------------------------------
-- Test 4: ROW comparison with upper bound on yb_hash_code.
-- Both conditions should be Index Cond.
-------------------------------------------------------------------
EXPLAIN (COSTS OFF)
SELECT * FROM hc_rc_t1
    WHERE (yb_hash_code(h), h, r1, r2) > (yb_hash_code(1), 1, 10, 100)
      AND yb_hash_code(h) < 50000
    LIMIT 5;

SELECT * FROM hc_rc_t1
    WHERE (yb_hash_code(h), h, r1, r2) > (yb_hash_code(1), 1, 10, 100)
      AND yb_hash_code(h) < 50000
    LIMIT 5;

-------------------------------------------------------------------
-- Test 5: yb_hash_code arity mismatch (single arg on 2-col hash key).
-- Should NOT match our path.
-------------------------------------------------------------------
EXPLAIN (COSTS OFF)
SELECT * FROM hc_rc_t2
    WHERE (yb_hash_code(h1), h1, h2, r) > (yb_hash_code(1), 1, 2, 7)
    LIMIT 10;

-------------------------------------------------------------------
-- Test 6: Skipped column (h2 missing from ROW).
-- Should NOT match our yb_match_rowcompare_to_index path
-- (matching_cols != list_length).
-------------------------------------------------------------------
EXPLAIN (COSTS OFF)
SELECT * FROM hc_rc_t2
    WHERE (yb_hash_code(h1, h2), h1, r) > (yb_hash_code(1, 2), 1, 7)
    LIMIT 10;

-------------------------------------------------------------------
-- Test 7: Less-than direction.
-------------------------------------------------------------------
EXPLAIN (COSTS OFF)
SELECT * FROM hc_rc_t1
    WHERE (yb_hash_code(h), h, r1, r2) < (yb_hash_code(3), 3, 30, 300)
    LIMIT 5;

SELECT * FROM hc_rc_t1
    WHERE (yb_hash_code(h), h, r1, r2) < (yb_hash_code(3), 3, 30, 300)
    LIMIT 5;

-------------------------------------------------------------------
-- Test 8: Verify row ordering across hash buckets.
-- The ROW comparison naturally orders by (hash_code, h, r1, r2),
-- so rows from different hash buckets should appear in hash_code order.
-------------------------------------------------------------------
SELECT yb_hash_code(h) as hc, h, r1, r2 FROM hc_rc_t1
    WHERE (yb_hash_code(h), h, r1, r2) > (0, 0, 0, 0)
    ORDER BY yb_hash_code(h), h, r1, r2
    LIMIT 15;

-- Clean up.
DROP TABLE hc_rc_t1;
DROP TABLE hc_rc_t2;
