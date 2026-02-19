\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/explainrun_distinct_pushdown.sql'
\i :filename

-- Split at 1, ... to ensure that the value r1 = 1 is present in more than one tablet.
-- See #18101.
CREATE TABLE t(r1 INT, r2 INT, r3 INT, v INT, PRIMARY KEY(r1 ASC, r2 ASC, r3 ASC)) SPLIT AT VALUES ((1, 1, 500));
INSERT INTO t (SELECT 1, i%3, i, i/3 FROM GENERATE_SERIES(1, 1000) AS i);
-- Add one more distinct value to catch bugs that arise only with more than one distinct value.
INSERT INTO t (SELECT 2, i%3, i, i/3 FROM GENERATE_SERIES(1, 1000) AS i);

-- Test the flag.
SET yb_enable_distinct_pushdown TO off;

-- Do not pick Distinct Index Scan since the flag is off.
-- XXX: Results may not be consistent (no explicit ordering).
\set query 'SELECT DISTINCT r1 FROM t'
:explain1run1

-- Turn the flag back on.
SET yb_enable_distinct_pushdown TO on;

-- Pick Distinct Index Scan.
\set query 'SELECT DISTINCT r1 FROM t'
:explain1run1

-- Test a larger prefix.
\set query 'SELECT DISTINCT r1, r2 FROM t'
:explain1run1

-- Even though the index scan does not return distinct values of r2, using
--   a Distinct Index Scan can still help retrieve fewer rows from storage.
-- Observe that this behavior deviates from ORDER BY where partial sorts
--   are not useful but partial DISTINCT scans are still worth it.
\set query 'SELECT DISTINCT r2 FROM t'
:explain1run1

-- Limit clauses.
-- Limit goes after DISTINCT (this includes the Unique node).
\set query 'SELECT DISTINCT r1 FROM t LIMIT 2'
:explain1run1

-- Now, test other data types.

-- Test floating point data.
CREATE TABLE tr(r1 REAL, r2 REAL, PRIMARY KEY(r1 ASC, r2 ASC));
INSERT INTO tr (SELECT 0.5, i FROM GENERATE_SERIES(1, 1000) AS i);

\set query 'SELECT DISTINCT r1 FROM tr'
:explain1run1

DROP TABLE tr;

-- Test text data as well.
CREATE TABLE ts(r1 TEXT, r2 TEXT, v TEXT, PRIMARY KEY(r1 ASC, r2 ASC));
INSERT INTO ts (SELECT 'uniq', i::TEXT, 'value' FROM GENERATE_SERIES(1, 1000) AS i);

\set query 'SELECT DISTINCT r1 FROM ts'
:explain1run1

DROP TABLE ts;

-- Ensure that Distinct Index Scan is not generated for a non-LSM index.
-- Non-LSM indexes such as GIN do not necessarily support distinct index scan.
CREATE TABLE vectors (v tsvector, k SERIAL PRIMARY KEY);
INSERT INTO vectors SELECT to_tsvector('simple', 'filler') FROM generate_series(1, 10);

CREATE INDEX NONCONCURRENTLY igin ON vectors USING ybgin (v);

-- Avoid fetching primary key and fetch secondary key instead since
--   there is already an LSM index on the primary key and LSM supports distinct index scan.
\set query 'SELECT DISTINCT v FROM vectors'
:explain1run1

DROP INDEX igin;
DROP TABLE vectors;

-- Test distinct index scans in scenarios where user provides explicit ordering.

-- Start off easy with forward and backward scans.
\set query 'SELECT DISTINCT r1 FROM t ORDER BY r1'
:explain1run1
\set query 'SELECT DISTINCT r1 FROM t ORDER BY r1 DESC'
:explain1run1

-- Now, try a larger prefix.
\set query 'SELECT DISTINCT r1, r2 FROM t ORDER BY r1, r2'
:explain1run1
\set query 'SELECT DISTINCT r1, r2 FROM t ORDER BY r1 DESC, r2 DESC'
:explain1run1

-- Now, we try only a subset of the prefix.
-- Picking a Distinct Index Scan for such cases can still be useful since
--   the storage layer retrieves and returns fewer rows overall.
\set query 'SELECT DISTINCT r1, r2 FROM t ORDER BY r2'
:explain1run1

-- m in tm refers to mixed ordering.
-- Sort order does not matter when distinct-ifying the columns.
-- Hence, generate Distinct Index Scans even for keys with a mix of
--   ascending and descending orders in the LSM index.
CREATE TABLE tm(r1 INT, r2 INT, r3 INT, v INT, PRIMARY KEY(r1 DESC, r2 ASC, r3 ASC));
INSERT INTO tm (SELECT i%3, 2-i%3, i, i/3 FROM GENERATE_SERIES(1, 1000) AS i);

-- Test both forward and backwards scans.
\set query 'SELECT DISTINCT r1, r2 FROM tm'
:explain1run1
-- This is a backwards scan because of how the keys are ordered in the primary index.
\set query 'SELECT DISTINCT r1, r2 FROM tm ORDER BY r1, r2 DESC'
:explain1run1

DROP TABLE tm;

-- Aggregates.
-- Unless the aggregate is pushed down into distinct index scan,
-- cannot currently push down DISTINCT.
\set query 'SELECT DISTINCT r1, COUNT(r1) FROM t GROUP BY r1'
:explain1run1

-- Window Funcs.
-- Same reasoning applies to window funcs as well.
\set query 'SELECT DISTINCT r1, COUNT(r1) OVER (PARTITION BY r1) FROM t'
:explain1run1

SELECT DISTINCT r1 FROM t WHERE r1 = 1 AND r2 IN (0, 1);

-- DISTINCT ON query for regression purposes.
SELECT DISTINCT ON (r1) r1, r2 FROM t ORDER BY r1, r2;

DROP TABLE t;

-- #22822: Include columns of an index are not sorted.
CREATE TABLE kv(k INT, v1 INT, v2 INT);
CREATE INDEX kv_idx ON kv (k ASC) INCLUDE (v1, v2);
INSERT INTO kv (SELECT i%10, i%10, i%10 FROM GENERATE_SERIES(1, 100) AS i);

EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
SELECT DISTINCT k, v1, v2 FROM kv;

SELECT DISTINCT k, v1, v2 FROM kv ORDER BY k;

DROP TABLE kv;
-- Test #22923
CREATE TABLE demo(k INT, x INT, v INT, a INT, d DATE, PRIMARY KEY(k,x,v,d));

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF)
SELECT v FROM (SELECT DISTINCT v FROM demo WHERE k=1 AND x=1) foo;

DROP TABLE demo;
