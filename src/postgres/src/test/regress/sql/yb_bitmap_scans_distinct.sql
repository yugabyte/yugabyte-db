--
-- test distinct bitmap scans (distinct pushdown is not supported by bitmap scans)
--
SET yb_explain_hide_non_deterministic_fields = true;

CREATE TABLE test_distinct (r1 INT, r2 INT, r3 INT, v INT, PRIMARY KEY(r1 ASC, r2 ASC, r3 ASC)) SPLIT AT VALUES ((1, 1, 500));
INSERT INTO test_distinct (SELECT 1, i%3, i, i/3 FROM GENERATE_SERIES(1, 1000) AS i);
-- Add one more distinct value to catch bugs that arise only with more than one distinct value.
INSERT INTO test_distinct (SELECT 2, i%3, i, i/3 FROM GENERATE_SERIES(1, 1000) AS i);

SET yb_enable_distinct_pushdown = true;
SET enable_bitmapscan = false;

EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT DISTINCT r1 FROM test_distinct WHERE r1 < 2 ORDER BY r1;
SELECT DISTINCT r1 FROM test_distinct WHERE r1 < 2 ORDER BY r1;
EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 ORDER BY r1, r2;
SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 ORDER BY r1, r2;
EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 OR r3 < 2 ORDER BY r1, r2;
SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 OR r3 < 2 ORDER BY r1, r2;

RESET enable_bitmapscan;

/*+ BitmapScan(test_distinct) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
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

EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT DISTINCT r1 FROM test_distinct WHERE r1 < 2 ORDER BY r1;
SELECT DISTINCT r1 FROM test_distinct WHERE r1 < 2 ORDER BY r1;
EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 ORDER BY r1, r2;
SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 ORDER BY r1, r2;

RESET enable_bitmapscan;

/*+ BitmapScan(test_distinct) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT DISTINCT r1 FROM test_distinct WHERE r1 < 2 ORDER BY r1;
/*+ BitmapScan(test_distinct) */
SELECT DISTINCT r1 FROM test_distinct WHERE r1 < 2 ORDER BY r1;
/*+ BitmapScan(test_distinct) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 ORDER BY r1, r2;
/*+ BitmapScan(test_distinct) */
SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 ORDER BY r1, r2;

RESET yb_enable_distinct_pushdown;
DROP TABLE test_distinct;
