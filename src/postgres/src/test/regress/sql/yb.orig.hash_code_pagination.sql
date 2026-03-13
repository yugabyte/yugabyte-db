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
-- Fix 1: Pathkey recognition — no Sort when yb_hash_code = const + ORDER BY
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
-- Fix 2+3: ROW comparison pushdown — (h, r1) > (?, ?) should become an
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

-- Clean up.
DROP TABLE hk_pagination;
DROP TABLE hk_multi;
