--
-- test distinct bitmap scans (distinct pushdown is not supported by bitmap scans)
--
SET yb_explain_hide_non_deterministic_fields = true;
SET yb_enable_bitmapscan = true;

CREATE TABLE test_distinct (r1 INT, r2 INT, r3 INT, v INT, PRIMARY KEY(r1 ASC, r2 ASC, r3 ASC)) SPLIT AT VALUES ((1, 1, 500));
INSERT INTO test_distinct (SELECT 1, i%3, i, i/3 FROM GENERATE_SERIES(1, 1000) AS i);
-- Add one more distinct value to catch bugs that arise only with more than one distinct value.
INSERT INTO test_distinct (SELECT 2, i%3, i, i/3 FROM GENERATE_SERIES(1, 1000) AS i);

SET yb_enable_distinct_pushdown = true;
SET enable_bitmapscan = false;

EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT DISTINCT r1 FROM test_distinct WHERE r1 < 2 ORDER BY r1;
SELECT DISTINCT r1 FROM test_distinct WHERE r1 < 2 ORDER BY r1;
EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 ORDER BY r1, r2;
SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 ORDER BY r1, r2;
EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 OR r3 < 2 ORDER BY r1, r2;
SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 OR r3 < 2 ORDER BY r1, r2;

RESET enable_bitmapscan;

/*+ BitmapScan(test_distinct) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT DISTINCT r1 FROM test_distinct WHERE r1 < 2 ORDER BY r1;
/*+ BitmapScan(test_distinct) */
SELECT DISTINCT r1 FROM test_distinct WHERE r1 < 2 ORDER BY r1;
/*+ BitmapScan(test_distinct) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 ORDER BY r1, r2;
/*+ BitmapScan(test_distinct) */
SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 ORDER BY r1, r2;
/*+ BitmapScan(test_distinct) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 OR r3 < 2 ORDER BY r1, r2;
/*+ BitmapScan(test_distinct) */
SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 OR r3 < 2 ORDER BY r1, r2;

SET yb_enable_distinct_pushdown TO false;
SET enable_bitmapscan = false;

EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT DISTINCT r1 FROM test_distinct WHERE r1 < 2 ORDER BY r1;
SELECT DISTINCT r1 FROM test_distinct WHERE r1 < 2 ORDER BY r1;
EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 ORDER BY r1, r2;
SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 ORDER BY r1, r2;

RESET enable_bitmapscan;

/*+ BitmapScan(test_distinct) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT DISTINCT r1 FROM test_distinct WHERE r1 < 2 ORDER BY r1;
/*+ BitmapScan(test_distinct) */
SELECT DISTINCT r1 FROM test_distinct WHERE r1 < 2 ORDER BY r1;
/*+ BitmapScan(test_distinct) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 ORDER BY r1, r2;
/*+ BitmapScan(test_distinct) */
SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 ORDER BY r1, r2;

RESET yb_enable_distinct_pushdown;
DROP TABLE test_distinct;

-- #32166 : SELECT DISTINCT returns wrong results over bitmap scan
-- This bug led to wrong results being returned in the case when DISTINCT keys
-- included all the hash keys of an index. In the test below, the column 'k' is
-- the hash key for the index 'test_distinct_hash_k' and the query selects
-- DISTINCT k. The result should be the 4 distinct values of k.
CREATE TABLE test_distinct_hash (id INT PRIMARY KEY, k INT);
INSERT INTO test_distinct_hash SELECT g, g % 4 FROM GENERATE_SERIES(1, 1000) g;
CREATE INDEX test_distinct_hash_k ON test_distinct_hash (k HASH);

SET yb_enable_distinct_pushdown = true;

-- Default plan and different hints to enforce bitmap scan. Result should be 4
-- distinct values of k in each case.
EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF) SELECT DISTINCT k FROM test_distinct_hash ORDER BY k;
/*+ Set(enable_seqscan off) Set(enable_indexscan off) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF) SELECT DISTINCT k FROM test_distinct_hash ORDER BY k;
/*+ BitmapScan(test_distinct_hash) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF) SELECT DISTINCT k FROM test_distinct_hash ORDER BY k;

SELECT DISTINCT k FROM test_distinct_hash ORDER BY k;
/*+ Set(enable_seqscan off) Set(enable_indexscan off) */ SELECT DISTINCT k FROM test_distinct_hash ORDER BY k;
/*+ BitmapScan(test_distinct_hash) */ SELECT DISTINCT k FROM test_distinct_hash ORDER BY k;

RESET yb_enable_distinct_pushdown;
DROP TABLE test_distinct_hash;
