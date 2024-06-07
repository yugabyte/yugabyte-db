--
-- YB Bitmap Scans (bitmap index scans + YB bitmap table scans)
--
SET yb_explain_hide_non_deterministic_fields = true;

--
-- -- test disabling bitmap scans --
-- for each combination of yb_enable_bitmapscan and enable_bitmapscan, try
--  1. a case where the planner chooses bitmap scans
--  2. a case where we tell the planner to use bitmap scans
--  3. a case where the alternative option (seq scan) is disabled
--
CREATE TABLE test_disable(a int, b int);
CREATE INDEX ON test_disable(a ASC);
CREATE INDEX ON test_disable(b ASC);

SET yb_enable_bitmapscan = true;
SET enable_bitmapscan = true;
EXPLAIN (COSTS OFF) SELECT * FROM test_disable WHERE a < 5 OR b < 5;
/*+ BitmapScan(test_disable) */
EXPLAIN (COSTS OFF) SELECT * FROM test_disable WHERE a < 5 OR b < 5;
/*+ Set(enable_seqscan false) */
EXPLAIN (COSTS OFF) SELECT * FROM test_disable WHERE a < 5 OR b < 5;

SET yb_enable_bitmapscan = true;
SET enable_bitmapscan = false;
EXPLAIN (COSTS OFF) SELECT * FROM test_disable WHERE a < 5 OR b < 5;
/*+ BitmapScan(test_disable) */
EXPLAIN (COSTS OFF) SELECT * FROM test_disable WHERE a < 5 OR b < 5;
/*+ Set(enable_seqscan false) */
EXPLAIN (COSTS OFF) SELECT * FROM test_disable WHERE a < 5 OR b < 5;

SET yb_enable_bitmapscan = false;
SET enable_bitmapscan = true;
EXPLAIN (COSTS OFF) SELECT * FROM test_disable WHERE a < 5 OR b < 5;
/*+ BitmapScan(test_disable) */
EXPLAIN (COSTS OFF) SELECT * FROM test_disable WHERE a < 5 OR b < 5;
/*+ Set(enable_seqscan false) */
EXPLAIN (COSTS OFF) SELECT * FROM test_disable WHERE a < 5 OR b < 5;

SET yb_enable_bitmapscan = false;
SET enable_bitmapscan = false;
EXPLAIN (COSTS OFF) SELECT * FROM test_disable WHERE a < 5 OR b < 5;
/*+ BitmapScan(test_disable) */
EXPLAIN (COSTS OFF) SELECT * FROM test_disable WHERE a < 5 OR b < 5;
/*+ Set(enable_seqscan false) */
EXPLAIN (COSTS OFF) SELECT * FROM test_disable WHERE a < 5 OR b < 5;

SET yb_enable_bitmapscan = true;
SET enable_bitmapscan = true;
-- tenk1 already has 4 ASC indexes: unique1, unique2, hundred, and (thousand, tenthous)
-- each query has an order by to make asserting results easier

/*+ BitmapScan(tenk1) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT unique1, unique2, hundred, thousand FROM tenk1 WHERE unique1 <= 1 ORDER BY unique1;

/*+ BitmapScan(tenk1) */
SELECT unique1, unique2, hundred, thousand FROM tenk1 WHERE unique1 <= 1 ORDER BY unique1;

/*+ BitmapScan(tenk1) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT unique1, unique2, hundred, thousand FROM tenk1 WHERE unique2 BETWEEN 4 and 6 ORDER BY unique1;

/*+ BitmapScan(tenk1) */
SELECT unique1, unique2, hundred, thousand FROM tenk1 WHERE unique2 BETWEEN 4 and 6 ORDER BY unique1;

/*+ BitmapScan(tenk1) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT unique1, unique2, hundred, thousand FROM tenk1 WHERE ((hundred IN (64, 66) AND thousand < 200 AND unique1 < 1000)) ORDER BY unique1;

/*+ BitmapScan(tenk1) */
SELECT unique1, unique2, hundred, thousand FROM tenk1 WHERE ((hundred IN (64, 66) AND thousand < 200 AND unique1 < 1000)) ORDER BY unique1;

/*+ BitmapScan(tenk1) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT unique1, unique2, hundred, thousand FROM tenk1 WHERE unique1 <= 1 OR (unique2 BETWEEN 4 and 6) OR ((hundred IN (64, 66) AND thousand < 200 AND unique1 < 1000)) ORDER BY unique1;

/*+ BitmapScan(tenk1) */
SELECT unique1, unique2, hundred, thousand FROM tenk1 WHERE unique1 <= 1 OR (unique2 BETWEEN 4 and 6) OR ((hundred IN (64, 66) AND thousand < 200 AND unique1 < 1000)) ORDER BY unique1;

/*+ Set(enable_bitmapscan false) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT unique1, unique2, hundred, thousand FROM tenk1 WHERE unique1 <= 1 OR (unique2 BETWEEN 4 and 6) OR ((hundred IN (64, 66) AND thousand < 200 AND unique1 < 1000)) ORDER BY unique1;

/*+ Set(enable_bitmapscan false) */
SELECT unique1, unique2, hundred, thousand FROM tenk1 WHERE unique1 <= 1 OR (unique2 BETWEEN 4 and 6) OR ((hundred IN (64, 66) AND thousand < 200 AND unique1 < 1000)) ORDER BY unique1;

-- test respecting row limits
SET yb_fetch_row_limit = 5;
/*+ BitmapScan(tenk1) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT * FROM tenk1 WHERE thousand < 4 OR thousand >= 998;

--
-- test respecting size limits
--
SET yb_fetch_row_limit = 0;
SET yb_fetch_size_limit = '135kB';
/*+ BitmapScan(tenk1) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT * FROM tenk1 WHERE thousand < 500 OR hundred >= 75;
RESET yb_fetch_row_limit;
RESET yb_fetch_size_limit;

--
-- test exceeding work_mem
--
SET work_mem TO '4MB';
/*+ BitmapScan(tenk1) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT unique1, unique2, hundred, thousand FROM tenk1 WHERE unique1 < 6000 OR unique2 < 1000;

SET work_mem TO '100kB';
/*+ BitmapScan(tenk1) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT unique1, unique2, hundred, thousand FROM tenk1 WHERE unique1 < 6000 OR unique2 < 1000;

-- verify that remote filters still apply to sequential scan when we've exceeded work_mem.
/*+ BitmapScan(tenk1) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT unique1, unique2, hundred, thousand FROM tenk1 WHERE ((unique1 BETWEEN 1000 AND 8000) OR unique2 < 1000) AND twothousand = 0;

-- verify we still do the right thing when pushdown is disabled.
/*+ BitmapScan(tenk1) Set(yb_enable_expression_pushdown false) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT unique1, unique2, hundred, thousand FROM tenk1 WHERE ((unique1 BETWEEN 1000 AND 8000) OR unique2 < 1000) AND twothousand = 0;

SET work_mem TO '4GB';
/*+ BitmapScan(tenk1) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT unique1, unique2, hundred, thousand FROM tenk1 WHERE unique1 < 6000 or unique2 < 1000;
RESET work_mem;

--
-- test recheck condition and UPDATE
--
CREATE TABLE tenk3 AS (SELECT * FROM tenk1);
CREATE INDEX NONCONCURRENTLY tenk3_unique1 ON tenk3 (unique1 ASC);
CREATE INDEX NONCONCURRENTLY tenk3_unique2 ON tenk3 (unique2 ASC);

-- use Bitmap Scan to update some rows
/*+ BitmapScan(tenk3) */ EXPLAIN (ANALYZE, COSTS OFF)
UPDATE tenk3 SET unique2 = NULL WHERE unique2 < 100 OR unique1 < 10;

/*+ BitmapScan(tenk3) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT unique1, unique2 FROM tenk3 WHERE unique1 < 100 or unique2 IS NULL;

SET yb_pushdown_is_not_null = false;

/*+ BitmapScan(tenk3) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT unique1, unique2 FROM tenk3 WHERE unique1 < 100 or unique2 IS NULL;

RESET yb_pushdown_is_not_null;

-- use Bitmap Scan to delete rows and validate their deletion
/*+ BitmapScan(tenk3) */ EXPLAIN (ANALYZE, COSTS OFF)
DELETE FROM tenk3 WHERE unique2 IS NULL OR unique1 < 1000;

/*+ BitmapScan(tenk3) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT unique1, unique2 FROM tenk3 WHERE unique1 < 100 or unique2 IS NULL;

--
-- test cases where we could skip fetching the table rows (TODO: #22044)
--
-- this query does not need a recheck, so we don't need to fetch the rows for the COUNT(*)
/*+ BitmapScan(tenk1) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT COUNT(*) FROM tenk1 WHERE unique1 < 2000 OR unique2 < 2000;
/*+ BitmapScan(tenk1) */
SELECT COUNT(*) FROM tenk1 WHERE unique1 < 2000 OR unique2 < 2000;

-- when we require the rows, notice that the YB Bitmap Table Scan sends a table read request
/*+ BitmapScan(tenk1) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT * FROM tenk1 WHERE unique1 < 2000 OR unique2 < 2000;

-- this query has a recheck condition, so we need to fetch the rows
/*+ BitmapScan(tenk1) Set(yb_enable_expression_pushdown false) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT COUNT(*) FROM tenk1 WHERE unique1 < 2000 OR unique2 < 2000 AND unique2 % 2 = 0;
/*+ BitmapScan(tenk1) Set(yb_enable_expression_pushdown false) */
SELECT COUNT(*) FROM tenk1 WHERE unique1 < 2000 OR unique2 < 2000 AND unique2 % 2 = 0;

-- when the expression can be pushed down, we don't need a recheck but we do
-- still need to send the request.
/*+ BitmapScan(tenk1) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT COUNT(*) FROM tenk1 WHERE unique1 < 2000 OR unique2 < 2000 AND unique2 % 2 = 0;
/*+ BitmapScan(tenk1) */
SELECT COUNT(*) FROM tenk1 WHERE unique1 < 2000 OR unique2 < 2000 AND unique2 % 2 = 0;

-- other aggregates may require the rows
/*+ BitmapScan(tenk1) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT SUM(unique1) FROM tenk1 WHERE unique1 < 2000 OR unique2 < 2000;
/*+ BitmapScan(tenk1) */
SELECT SUM(unique1) FROM tenk1 WHERE unique1 < 2000 OR unique2 < 2000;
/*+ BitmapScan(tenk1) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT MAX(unique1) FROM tenk1 WHERE unique1 < 2000 OR unique2 < 2000;
/*+ BitmapScan(tenk1) */
SELECT MAX(unique1) FROM tenk1 WHERE unique1 < 2000 OR unique2 < 2000;

-- when we don't need the actual value, we can avoid fetching
/*+ BitmapScan(tenk1) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT 1 FROM tenk1 WHERE unique1 < 5 OR unique2 < 5;
/*+ BitmapScan(tenk1) */
SELECT 1 FROM tenk1 WHERE unique1 < 5 OR unique2 < 5;
/*+ BitmapScan(tenk1) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT random() FROM tenk1 WHERE unique1 < 5 OR unique2 < 5;

--
-- test primary key queries
--
CREATE TABLE pk (k INT PRIMARY KEY, a INT);
CREATE INDEX ON pk(a ASC);
INSERT INTO pk SELECT i, i FROM generate_series(1, 1000) i;
/*+ BitmapScan(pk) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT * FROM pk WHERE k = 123 OR a = 123;
/*+ BitmapScan(pk) */
SELECT * FROM pk WHERE k = 123 OR a = 123;

/*+ BitmapScan(pk) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT * FROM pk WHERE k IN (123, 124) OR a IN (122, 123) ORDER BY k;
/*+ BitmapScan(pk) */
SELECT * FROM pk WHERE k IN (123, 124) OR a IN (122, 123) ORDER BY k;

/*+ BitmapScan(pk) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT * FROM pk WHERE k = 123 OR k = 124 OR a = 122 OR a = 123 ORDER BY k;
/*+ BitmapScan(pk) */
SELECT * FROM pk WHERE k = 123 OR k = 124 OR a = 122 OR a = 123 ORDER BY k;

-- test non-existent results
/*+ BitmapScan(pk) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT COUNT(*) FROM pk WHERE k = 2000 OR a < 0;
/*+ BitmapScan(pk) */
SELECT COUNT(*) FROM pk WHERE k = 2000 OR a < 0;

--
-- test system catalog queries (they are colocated)
--
/*+ BitmapScan(pg_authid) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM pg_authid WHERE rolname LIKE 'pg_%' OR rolname LIKE 'yb_%' ORDER BY rolname;
/*+ BitmapScan(pg_authid) */
SELECT * FROM pg_authid WHERE rolname LIKE 'pg_%' OR rolname LIKE 'yb_%' ORDER BY rolname;


/*+ BitmapScan(pg_roles) */ EXPLAIN (ANALYZE, COSTS OFF) SELECT spcname FROM pg_tablespace WHERE spcowner NOT IN (
    SELECT oid FROM pg_roles WHERE rolname = 'postgres' OR rolname LIKE 'pg_%' OR rolname LIKE 'yb_%');
/*+ BitmapScan(pg_roles) */ SELECT spcname FROM pg_tablespace WHERE spcowner NOT IN (
    SELECT oid FROM pg_roles WHERE rolname = 'postgres' OR rolname LIKE 'pg_%' OR rolname LIKE 'yb_%');

SET yb_enable_expression_pushdown = false;

/*+ BitmapScan(pg_roles) */ EXPLAIN (ANALYZE, COSTS OFF) SELECT spcname FROM pg_tablespace WHERE spcowner NOT IN (
    SELECT oid FROM pg_roles WHERE rolname = 'postgres' OR rolname LIKE 'pg_%' OR rolname LIKE 'yb_%');
/*+ BitmapScan(pg_roles) */ SELECT spcname FROM pg_tablespace WHERE spcowner NOT IN (
    SELECT oid FROM pg_roles WHERE rolname = 'postgres' OR rolname LIKE 'pg_%' OR rolname LIKE 'yb_%');

RESET yb_enable_expression_pushdown;

--
-- test indexes on multiple columns / indexes with additional columns
--
CREATE TABLE multi (a INT, b INT, c INT, h INT, PRIMARY KEY (a ASC, b ASC));
CREATE INDEX ON multi (c ASC) INCLUDE (a);
CREATE INDEX ON multi (h HASH) INCLUDE (a);
CREATE INDEX ON multi (b ASC, c ASC);
INSERT INTO multi SELECT i, i * 2, i * 3, i * 4 FROM generate_series(1, 1000) i;

/*+ BitmapScan(multi) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT * FROM multi WHERE a < 2 OR b > 1997 ORDER BY a;
/*+ BitmapScan(multi) */
SELECT * FROM multi WHERE a < 2 OR b > 1997 ORDER BY a;

/*+ BitmapScan(multi) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT * FROM multi WHERE c BETWEEN 10 AND 15 AND a < 30 ORDER BY a;
/*+ BitmapScan(multi) */
SELECT * FROM multi WHERE c BETWEEN 10 AND 15 AND a < 30 ORDER BY a;

/*+ BitmapScan(multi) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT * FROM multi WHERE a < 2 OR b > 1997 OR c BETWEEN 10 AND 15 OR h = 8 ORDER BY a;
/*+ BitmapScan(multi) */
SELECT * FROM multi WHERE a < 2 OR b > 1997 OR c BETWEEN 10 AND 15 OR h = 8  ORDER BY a;

-- try some slightly complex nested logical operands queries
/*+ BitmapScan(multi) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM multi WHERE a < 2 OR (b > 1797 AND (c BETWEEN 2709 AND 2712 OR c = 2997)) ORDER BY a;
/*+ BitmapScan(multi) */
SELECT * FROM multi WHERE a < 2 OR (b > 1797 AND (c BETWEEN 2709 AND 2712 OR c = 2997)) ORDER BY a;

/*+ BitmapScan(multi) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM multi WHERE (a < 3 AND a % 2 = 0) OR (b IN (10, 270, 1800) AND (c < 20 OR c > 2500)) ORDER BY a;
/*+ BitmapScan(multi) */
SELECT * FROM multi WHERE (a < 3 AND a % 2 = 0) OR (b IN (10, 270, 1800) AND (c < 20 OR c > 2500)) ORDER BY a;

--
-- test limits
--
CREATE TABLE test_limit (a INT, b INT, c INT);
CREATE INDEX ON test_limit (a ASC);
INSERT INTO test_limit SELECT i, i * 2, i * 3 FROM generate_series(1, 1000) i;
SET yb_fetch_row_limit = 100;
SET yb_fetch_size_limit = 0;

/*+ BitmapScan(test_limit) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT * FROM test_limit WHERE a < 200 LIMIT 10;

SET yb_fetch_row_limit = 0;
SET yb_fetch_size_limit = '1kB';

/*+ BitmapScan(test_limit) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM test_limit WHERE a < 200 LIMIT 10;

RESET yb_fetch_row_limit;
RESET yb_fetch_size_limit;

--
-- test remote pushdown
--

/*+ BitmapScan(multi multi_b_c_idx) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT * FROM multi WHERE (b < 10 AND b % 4 = 0) ORDER BY b;
/*+ BitmapScan(multi multi_b_c_idx) */
SELECT * FROM multi WHERE (b < 10 AND b % 4 = 0) ORDER BY b;

/*+ BitmapScan(multi multi_b_c_idx) Set(yb_enable_expression_pushdown false) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT * FROM multi WHERE (b < 10 AND b % 4 = 0) ORDER BY b;
/*+ BitmapScan(multi multi_b_c_idx) Set(yb_enable_expression_pushdown false) */
SELECT * FROM multi WHERE (b < 10 AND b % 4 = 0) ORDER BY b;

/*+ BitmapScan(multi) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT * FROM multi WHERE (a < 5 AND a % 2 = 0) OR (c <= 10 AND a % 3 = 0) ORDER BY a;
/*+ BitmapScan(multi) */
SELECT * FROM multi WHERE (a < 5 AND a % 2 = 0) OR (c <= 10 AND a % 3 = 0) ORDER BY a;

/*+ BitmapScan(multi) Set(yb_enable_expression_pushdown false) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT * FROM multi WHERE (a < 5 AND a % 2 = 0) OR (c <= 10 AND a % 3 = 0) ORDER BY a;
/*+ BitmapScan(multi) Set(yb_enable_expression_pushdown false) */
SELECT * FROM multi WHERE (a < 5 AND a % 2 = 0) OR (c <= 10 AND a % 3 = 0) ORDER BY a;

--
-- test unsatisfiable conditions
--
CREATE TABLE test_false (a INT, b INT);
CREATE INDEX ON test_false (a ASC);
CREATE INDEX ON test_false (b ASC);
INSERT INTO test_false VALUES (1, 1), (2, 2);

/*+ BitmapScan(test_false) */ EXPLAIN (ANALYZE, DIST, COSTS OFF) SELECT * FROM test_false WHERE (a <= 1 AND a = 2);
/*+ BitmapScan(test_false) */ EXPLAIN (ANALYZE, DIST, COSTS OFF) SELECT * FROM test_false WHERE (a = 1 AND a = 2) OR b = 0;

--
-- test recheck index conditions
--
create table recheck_test (col int);
create index on recheck_test (col ASC);

insert into recheck_test select i from generate_series(1, 10) i;

explain (analyze, costs off) /*+ BitmapScan(t) */
SELECT * FROM recheck_test t WHERE t.col < 3 AND t.col IN (5, 6);
explain (analyze, costs off) /*+ BitmapScan(t) */
SELECT * FROM recheck_test t WHERE t.col IN (5, 6) AND t.col < 3;

explain (analyze, costs off) /*+ BitmapScan(t) */
SELECT * FROM recheck_test t WHERE t.col < 3 AND t.col = 5;
explain (analyze, costs off) /*+ BitmapScan(t) */
SELECT * FROM recheck_test t WHERE t.col = 5 AND t.col < 3;

--
-- test colocated queries
--
CREATE DATABASE colo WITH colocation = true;
\c colo;

SET yb_explain_hide_non_deterministic_fields = true;
SET enable_bitmapscan = true;
SET yb_enable_bitmapscan = true;

CREATE TABLE pk_colo (k INT PRIMARY KEY, a INT);
CREATE INDEX ON pk_colo(a ASC);
INSERT INTO pk_colo SELECT i, i FROM generate_series(1, 1000) i;

/*+ BitmapScan(pk_colo) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT * FROM pk_colo WHERE k = 123 OR a = 123;
/*+ BitmapScan(pk_colo) */
SELECT * FROM pk_colo WHERE k = 123 OR a = 123;

/*+ BitmapScan(pk_colo) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT * FROM pk_colo WHERE (k < 10 AND k % 2 = 1) OR (a > 990 AND a % 2 = 1) ORDER BY k;
/*+ BitmapScan(pk_colo) */
SELECT * FROM pk_colo WHERE (k < 10 AND k % 2 = 1) OR (a > 990 AND a % 2 = 1) ORDER BY k;

/*+ BitmapScan(pk_colo) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT * FROM pk_colo WHERE k < 5 OR a BETWEEN 7 AND 8 ORDER BY k;
/*+ BitmapScan(pk_colo) */
SELECT * FROM pk_colo WHERE k < 5 OR a BETWEEN 7 AND 8 ORDER BY k;

/*+ BitmapScan(pk_colo) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT * FROM pk_colo WHERE k IN (123, 124) OR a IN (122, 123) ORDER BY k;
/*+ BitmapScan(pk_colo) */
SELECT * FROM pk_colo WHERE k IN (123, 124) OR a IN (122, 123) ORDER BY k;

-- test count
/*+ BitmapScan(pk_colo) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT COUNT(*) FROM pk_colo WHERE k IN (123, 124) OR a IN (122, 123);
/*+ BitmapScan(pk_colo) */
SELECT COUNT(*) FROM pk_colo WHERE k IN (123, 124) OR a IN (122, 123);

-- test non-existent results
/*+ BitmapScan(pk_colo) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT COUNT(*) FROM pk_colo WHERE k = 2000 OR a < 0;
/*+ BitmapScan(pk_colo) */
SELECT COUNT(*) FROM pk_colo WHERE k = 2000 OR a < 0;

RESET yb_explain_hide_non_deterministic_fields;
RESET enable_bitmapscan;
