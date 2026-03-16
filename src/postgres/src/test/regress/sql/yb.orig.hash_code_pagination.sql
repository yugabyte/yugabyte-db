--
-- Test hash code pagination: yb_hash_code(hash_cols) = const should allow
-- the planner to recognize index ordering within a single hash bucket and
-- push down ROW comparison conditions for keyset pagination.
-- See https://github.com/yugabyte/yugabyte-db/issues/30524
--

CREATE TABLE hk_pagination(
    h int,
    r1 int,
    r2 int,
    v text,
    PRIMARY KEY(h HASH, r1 ASC, r2 ASC)
);
INSERT INTO hk_pagination SELECT i % 5, i, i * 10, 'val' || i FROM generate_series(1, 20) i;

--
-- Fix 1: Pathkey recognition -- no Sort when yb_hash_code = const + ORDER BY
-- matches the full index key order.
--

-- Should NOT have a Sort node (single hash bucket, index provides ordering).
EXPLAIN (COSTS OFF) SELECT * FROM hk_pagination
    WHERE yb_hash_code(h) = 1000
    ORDER BY h, r1, r2
    LIMIT 10;

-- Negative: range predicate on yb_hash_code should still Sort (multiple buckets).
EXPLAIN (COSTS OFF) SELECT * FROM hk_pagination
    WHERE yb_hash_code(h) < 1000
    ORDER BY h, r1, r2
    LIMIT 10;

-- Negative: yb_hash_code equality but ORDER BY doesn't match leading key.
EXPLAIN (COSTS OFF) SELECT * FROM hk_pagination
    WHERE yb_hash_code(h) = 1000
    ORDER BY r1, r2
    LIMIT 10;

--
-- Fix 2+3: ROW comparison pushdown -- (h, r1) > (?, ?) should become an
-- index bound (Index Cond), not a post-filter (Filter), when hash bucket
-- is pinned.
--

-- ROW comparison on (h, r1) with yb_hash_code equality: should be Index Cond.
EXPLAIN (COSTS OFF) SELECT * FROM hk_pagination
    WHERE yb_hash_code(h) = 1000
      AND (h, r1) > (2, 10)
    LIMIT 10;

-- Full keyset pagination pattern: hash_code equality + ROW comparison + ORDER BY + LIMIT.
EXPLAIN (COSTS OFF) SELECT * FROM hk_pagination
    WHERE yb_hash_code(h) = 1000
      AND (h, r1, r2) > (2, 10, 100)
    ORDER BY h, r1, r2
    LIMIT 10;

-- Negative: without yb_hash_code equality, ROW comparison on hash column
-- should NOT become Index Cond (cannot seek across buckets).
EXPLAIN (COSTS OFF) /*+IndexScan(hk_pagination)*/ SELECT * FROM hk_pagination
    WHERE yb_hash_code(h) < 1000
      AND (h, r1) > (2, 10)
    LIMIT 10;

--
-- Correctness: verify the queries return correct results.
--

-- Verify results for yb_hash_code equality + ORDER BY (no Sort).
SELECT * FROM hk_pagination
    WHERE yb_hash_code(h) = 1000
    ORDER BY h, r1, r2;

-- Verify ROW comparison pagination produces correct results.
-- First page:
SELECT * FROM hk_pagination
    WHERE yb_hash_code(h) = 1000
      AND (h, r1, r2) > (0, 0, 0)
    ORDER BY h, r1, r2
    LIMIT 5;

--
-- Multi-column hash key test.
--
CREATE TABLE hk_multi(
    h1 int,
    h2 int,
    r int,
    v text,
    PRIMARY KEY((h1, h2) HASH, r ASC)
);
INSERT INTO hk_multi SELECT i % 3, i % 7, i, 'val' || i FROM generate_series(1, 30) i;

-- yb_hash_code on multi-column hash key + ORDER BY.
EXPLAIN (COSTS OFF) SELECT * FROM hk_multi
    WHERE yb_hash_code(h1, h2) = 500
    ORDER BY h1, h2, r
    LIMIT 10;

-- Negative: arity mismatch (partial hash key spec) should still Sort.
EXPLAIN (COSTS OFF) SELECT * FROM hk_multi
    WHERE yb_hash_code(h1) = 500
    ORDER BY h1, h2, r
    LIMIT 10;

-- Multi-column hash + ROW comparison.
EXPLAIN (COSTS OFF) SELECT * FROM hk_multi
    WHERE yb_hash_code(h1, h2) = 500
      AND (h1, h2, r) > (1, 2, 5)
    ORDER BY h1, h2, r
    LIMIT 10;

-- Negative: multi-column hash + ROW comparison but arity mismatch on yb_hash_code.
EXPLAIN (COSTS OFF) /*+IndexScan(hk_multi)*/ SELECT * FROM hk_multi
    WHERE yb_hash_code(h1) = 500
      AND (h1, h2, r) > (1, 2, 5)
    ORDER BY h1, h2, r
    LIMIT 10;

--
-- Secondary index test.
--
CREATE INDEX hk_pagination_sec_idx ON hk_pagination (r1 HASH, r2 ASC);

-- yb_hash_code on secondary index hash column + ORDER BY should avoid Sort.
EXPLAIN (COSTS OFF) SELECT r1, r2 FROM hk_pagination
    WHERE yb_hash_code(r1) = 2000
    ORDER BY r1, r2;

-- ROW comparison on secondary index with yb_hash_code equality.
EXPLAIN (COSTS OFF) SELECT r1, r2 FROM hk_pagination
    WHERE yb_hash_code(r1) = 2000
      AND (r1, r2) > (5, 50)
    ORDER BY r1, r2
    LIMIT 10;

-- Negative: yb_hash_code on a column that doesn't match the secondary index's
-- hash column should NOT trigger the optimization for that index.
EXPLAIN (COSTS OFF) /*+IndexScan(hk_pagination hk_pagination_sec_idx)*/ SELECT r1, r2 FROM hk_pagination
    WHERE yb_hash_code(h) = 1000
    ORDER BY r1, r2
    LIMIT 10;

--
-- ROW comparison on range-only columns of a hash-sharded index.
-- The hash column is NOT in the ROW comparison, so pushdown must NOT
-- happen (EncodeRowKeyForBound needs all hash columns in the bound).
--
EXPLAIN (COSTS OFF) /*+IndexScan(hk_pagination hk_pagination_pkey)*/ SELECT * FROM hk_pagination
    WHERE yb_hash_code(h) = 1000
      AND (r1, r2) > (5, 50)
    ORDER BY h, r1, r2
    LIMIT 10;

--
-- NULL handling tests.
-- NULLs hash to a different bucket, so yb_hash_code(col) = const already
-- excludes them.  Verify queries work correctly with NULL data.
--
INSERT INTO hk_pagination VALUES (NULL, 100, 1000, 'null_h');
INSERT INTO hk_pagination VALUES (1, NULL, 1000, 'null_r1');
INSERT INTO hk_pagination VALUES (1, 100, NULL, 'null_r2');

-- Verify NULLs do not appear in a hash-code-pinned scan.
SELECT * FROM hk_pagination
    WHERE yb_hash_code(h) = yb_hash_code(1)
    ORDER BY h, r1, r2;

-- ROW comparison with NULLs present in the table.
SELECT * FROM hk_pagination
    WHERE yb_hash_code(h) = yb_hash_code(1)
      AND (h, r1, r2) > (1, 0, 0)
    ORDER BY h, r1, r2;

-- Secondary index with NULLs: secondary hash indexes allow NULL values.
-- yb_hash_code(col) works when col is NULL (function is non-strict).
CREATE INDEX hk_pagination_nullable_idx ON hk_pagination (h HASH, v ASC);
EXPLAIN (COSTS OFF) SELECT h, v FROM hk_pagination
    WHERE yb_hash_code(h) = yb_hash_code(1)
    ORDER BY h, v;

SELECT h, v FROM hk_pagination
    WHERE yb_hash_code(h) = yb_hash_code(1)
    ORDER BY h, v;

-- Secondary hash index with NULLs and ROW comparison pushdown.
-- Secondary hash indexes allow NULL values, and yb_hash_code(NULL::type)
-- returns a valid hash code, so NULL rows live in a real bucket.
-- The IS NOT NULL guard is skipped for hash columns (pggate rejects it),
-- but SQL-layer recheck excludes NULLs because NULL comparisons yield NULL.
CREATE TABLE hk_sec_null(
    a int,
    b int,
    v text
);
CREATE INDEX hk_sec_null_idx ON hk_sec_null (a HASH, b ASC);
INSERT INTO hk_sec_null VALUES (1, 10, 'a1b10');
INSERT INTO hk_sec_null VALUES (1, 20, 'a1b20');
INSERT INTO hk_sec_null VALUES (NULL, 5, 'null_a_b5');
INSERT INTO hk_sec_null VALUES (NULL, 15, 'null_a_b15');

-- ROW comparison on secondary hash index: NULLs in hash column must not appear.
EXPLAIN (COSTS OFF) SELECT a, b FROM hk_sec_null
    WHERE yb_hash_code(a) = yb_hash_code(1)
      AND (a, b) > (1, 10)
    ORDER BY a, b;

SELECT a, b FROM hk_sec_null
    WHERE yb_hash_code(a) = yb_hash_code(1)
      AND (a, b) > (1, 10)
    ORDER BY a, b;

-- Scan the bucket that contains NULL hash values.
-- (a, b) > (NULL, 0) yields NULL for every row, so 0 rows returned.
SELECT a, b FROM hk_sec_null
    WHERE yb_hash_code(a) = yb_hash_code(NULL::int)
      AND (a, b) > (NULL::int, 0)
    ORDER BY a, b;

DROP TABLE hk_sec_null;

-- Clean up.
DROP TABLE hk_pagination;
DROP TABLE hk_multi;
