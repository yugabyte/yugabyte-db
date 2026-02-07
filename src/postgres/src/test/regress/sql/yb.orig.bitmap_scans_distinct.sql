--
-- test distinct bitmap scans (distinct pushdown is not supported by bitmap scans)
--
\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/explainrun.sql'
\i :filename
\set explain 'EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)'

SET yb_enable_bitmapscan = true;

CREATE TABLE test_distinct (r1 INT, r2 INT, r3 INT, v INT, PRIMARY KEY(r1 ASC, r2 ASC, r3 ASC)) SPLIT AT VALUES ((1, 1, 500));
INSERT INTO test_distinct (SELECT 1, i%3, i, i/3 FROM GENERATE_SERIES(1, 1000) AS i);
-- Add one more distinct value to catch bugs that arise only with more than one distinct value.
INSERT INTO test_distinct (SELECT 2, i%3, i, i/3 FROM GENERATE_SERIES(1, 1000) AS i);

SET yb_enable_distinct_pushdown = true;
SET enable_bitmapscan = false;

\set query 'SELECT DISTINCT r1 FROM test_distinct WHERE r1 < 2 ORDER BY r1'
:explain1run1
\set query 'SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 ORDER BY r1, r2'
:explain1run1
\set query 'SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 OR r3 < 2 ORDER BY r1, r2'
:explain1run1

RESET enable_bitmapscan;

\set hint1 '/*+ BitmapScan(test_distinct) */'
\set query 'SELECT DISTINCT r1 FROM test_distinct WHERE r1 < 2 ORDER BY r1'
:explain1run1
\set query 'SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 ORDER BY r1, r2'
:explain1run1
\set query 'SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 OR r3 < 2 ORDER BY r1, r2'
:explain1run1

SET yb_enable_distinct_pushdown TO false;
SET enable_bitmapscan = false;

\set hint1
\set query 'SELECT DISTINCT r1 FROM test_distinct WHERE r1 < 2 ORDER BY r1'
:explain1run1
\set query 'SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 ORDER BY r1, r2'
:explain1run1

RESET enable_bitmapscan;

\set hint1 '/*+ BitmapScan(test_distinct) */'
\set query 'SELECT DISTINCT r1 FROM test_distinct WHERE r1 < 2 ORDER BY r1'
:explain1run1
\set query 'SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 ORDER BY r1, r2'
:explain1run1

RESET yb_enable_distinct_pushdown;
DROP TABLE test_distinct;
