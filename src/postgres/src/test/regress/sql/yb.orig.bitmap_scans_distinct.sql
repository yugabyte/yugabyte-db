--
-- test distinct bitmap scans (distinct pushdown is not supported by bitmap scans)
--
\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/parameterized_query.sql'
\i :filename
\set P1 ':explain'
\set P2
\set explain 'EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)'

SET yb_enable_bitmapscan = true;

CREATE TABLE test_distinct (r1 INT, r2 INT, r3 INT, v INT, PRIMARY KEY(r1 ASC, r2 ASC, r3 ASC)) SPLIT AT VALUES ((1, 1, 500));
INSERT INTO test_distinct (SELECT 1, i%3, i, i/3 FROM GENERATE_SERIES(1, 1000) AS i);
-- Add one more distinct value to catch bugs that arise only with more than one distinct value.
INSERT INTO test_distinct (SELECT 2, i%3, i, i/3 FROM GENERATE_SERIES(1, 1000) AS i);

SET yb_enable_distinct_pushdown = true;
SET enable_bitmapscan = false;

\set query ':P SELECT DISTINCT r1 FROM test_distinct WHERE r1 < 2 ORDER BY r1;'
\i :run_query
\set query ':P SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 ORDER BY r1, r2;'
\i :run_query
\set query ':P SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 OR r3 < 2 ORDER BY r1, r2;'
\i :run_query

RESET enable_bitmapscan;

\set Q1 '/*+ BitmapScan(test_distinct) */'
\set query ':P :Q1 SELECT DISTINCT r1 FROM test_distinct WHERE r1 < 2 ORDER BY r1;'
\i :run_query
\set query ':P :Q1 SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 ORDER BY r1, r2;'
\i :run_query
\set query ':P :Q1 SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 OR r3 < 2 ORDER BY r1, r2;'
\i :run_query

SET yb_enable_distinct_pushdown TO false;
SET enable_bitmapscan = false;

\set query ':P SELECT DISTINCT r1 FROM test_distinct WHERE r1 < 2 ORDER BY r1;'
\i :run_query
\set query ':P SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 ORDER BY r1, r2;'
\i :run_query

RESET enable_bitmapscan;

\set query ':P :Q1 SELECT DISTINCT r1 FROM test_distinct WHERE r1 < 2 ORDER BY r1;'
\i :run_query
\set query ':P :Q1 SELECT DISTINCT r1, r2 FROM test_distinct WHERE r1 < 2 OR r2 < 3 ORDER BY r1, r2;'
\i :run_query

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
\set Q1
\set Q2 '/*+ Set(enable_seqscan off) Set(enable_indexscan off) */'
\set Q3 '/*+ BitmapScan(test_distinct_hash) */'
\set query ':P :Q SELECT DISTINCT k FROM test_distinct_hash ORDER BY k;'
\i :run_query

RESET yb_enable_distinct_pushdown;
DROP TABLE test_distinct_hash;
