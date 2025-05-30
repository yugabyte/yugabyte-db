--
-- Test disabling YB Bitmap Scans
--
-- for each combination of yb_enable_bitmapscan and enable_bitmapscan, try
--  1. a case where the planner chooses bitmap scans
--  2. a case where we tell the planner to use bitmap scans
--  3. a case where the alternative option (seq scan) is disabled
--
CREATE TABLE test_disable(a int, b int);
CREATE INDEX ON test_disable(a ASC);
CREATE INDEX ON test_disable(b ASC);

CREATE TEMP TABLE tmp_test_disable(a int, b int);
CREATE INDEX ON tmp_test_disable(a ASC);
CREATE INDEX ON tmp_test_disable(b ASC);

SET yb_enable_bitmapscan = true;
SET enable_bitmapscan = true;
EXPLAIN (COSTS OFF) SELECT * FROM test_disable WHERE a < 5 OR b < 5;
/*+ BitmapScan(test_disable) */
EXPLAIN (COSTS OFF) SELECT * FROM test_disable WHERE a < 5 OR b < 5;
/*+ Set(enable_seqscan false) */
EXPLAIN (COSTS OFF) SELECT * FROM test_disable WHERE a < 5 OR b < 5;
EXPLAIN (COSTS OFF) SELECT * FROM tmp_test_disable WHERE a < 5 OR b < 5;
/*+ BitmapScan(tmp_test_disable) */
EXPLAIN (COSTS OFF) SELECT * FROM tmp_test_disable WHERE a < 5 OR b < 5;
/*+ Set(enable_seqscan false) */
EXPLAIN (COSTS OFF) SELECT * FROM tmp_test_disable WHERE a < 5 OR b < 5;

SET yb_enable_bitmapscan = true;
SET enable_bitmapscan = false;
EXPLAIN (COSTS OFF) SELECT * FROM test_disable WHERE a < 5 OR b < 5;
/*+ BitmapScan(test_disable) */
EXPLAIN (COSTS OFF) SELECT * FROM test_disable WHERE a < 5 OR b < 5;
EXPLAIN (COSTS OFF) SELECT * FROM tmp_test_disable WHERE a < 5 OR b < 5;
/*+ BitmapScan(tmp_test_disable) */
EXPLAIN (COSTS OFF) SELECT * FROM tmp_test_disable WHERE a < 5 OR b < 5;

SET yb_enable_bitmapscan = false;
SET enable_bitmapscan = true;
EXPLAIN (COSTS OFF) SELECT * FROM test_disable WHERE a < 5 OR b < 5;
/*+ BitmapScan(test_disable) */
EXPLAIN (COSTS OFF) SELECT * FROM test_disable WHERE a < 5 OR b < 5;
EXPLAIN (COSTS OFF) SELECT * FROM tmp_test_disable WHERE a < 5 OR b < 5;
/*+ BitmapScan(tmp_test_disable) */
EXPLAIN (COSTS OFF) SELECT * FROM tmp_test_disable WHERE a < 5 OR b < 5;
/*+ Set(enable_seqscan false) */
EXPLAIN (COSTS OFF) SELECT * FROM tmp_test_disable WHERE a < 5 OR b < 5;

SET yb_enable_bitmapscan = false;
SET enable_bitmapscan = false;
EXPLAIN (COSTS OFF) SELECT * FROM test_disable WHERE a < 5 OR b < 5;
/*+ BitmapScan(test_disable) */
EXPLAIN (COSTS OFF) SELECT * FROM test_disable WHERE a < 5 OR b < 5;
EXPLAIN (COSTS OFF) SELECT * FROM tmp_test_disable WHERE a < 5 OR b < 5;
/*+ BitmapScan(tmp_test_disable) */
EXPLAIN (COSTS OFF) SELECT * FROM tmp_test_disable WHERE a < 5 OR b < 5;

RESET enable_bitmapscan;
RESET yb_enable_bitmapscan;
