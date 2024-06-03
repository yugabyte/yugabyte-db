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

CREATE TABLE simple (k INT PRIMARY KEY, ind_a INT, ind_b INT);
INSERT INTO simple SELECT i, i * 2, i * 3 FROM generate_series(1, 10) i;
CREATE INDEX ON simple(ind_a ASC);
CREATE INDEX ON simple(ind_b ASC);

/*+ BitmapScan(simple) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT * FROM simple WHERE k = 1;
/*+ BitmapScan(simple) */
SELECT * FROM simple WHERE k = 1;

/*+ BitmapScan(simple) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT * FROM simple WHERE ind_a < 5 ORDER BY k;
/*+ BitmapScan(simple) */
SELECT * FROM simple WHERE ind_a < 5 ORDER BY k;

/*+ BitmapScan(simple) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT * FROM simple WHERE ind_a BETWEEN 2 AND 6 OR ind_b > 25 ORDER BY k;
/*+ BitmapScan(simple) */
SELECT * FROM simple WHERE ind_a BETWEEN 2 AND 6 OR ind_b > 25 ORDER BY k;

--
-- test UPDATE
--

-- use Bitmap Scan to update some rows
/*+ BitmapScan(simple) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
UPDATE simple SET ind_a = NULL WHERE ind_a < 10 OR ind_b > 25;

/*+ BitmapScan(simple) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT ind_a, ind_b FROM simple WHERE ind_a < 10 OR ind_a IS NULL OR ind_b < 15 ORDER BY k;
/*+ BitmapScan(simple) */
SELECT ind_a, ind_b FROM simple WHERE ind_a < 10 OR ind_a IS NULL OR ind_b < 15 ORDER BY k;

SET yb_pushdown_is_not_null = false;

/*+ BitmapScan(simple) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT ind_a, ind_b FROM simple WHERE ind_a < 10 OR ind_a IS NULL OR ind_b < 15 ORDER BY k;
/*+ BitmapScan(simple) */
SELECT ind_a, ind_b FROM simple WHERE ind_a < 10 OR ind_a IS NULL OR ind_b < 15 ORDER BY k;

RESET yb_pushdown_is_not_null;

-- use Bitmap Scan to delete rows and validate their deletion
/*+ BitmapScan(simple) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
DELETE FROM simple WHERE ind_a IS NULL OR ind_b < 15;

/*+ BitmapScan(simple) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT ind_a, ind_b FROM simple WHERE ind_a IS NULL OR ind_b < 15 ORDER BY k;
/*+ BitmapScan(simple) */
SELECT ind_a, ind_b FROM simple WHERE ind_a IS NULL OR ind_b < 15 ORDER BY k;

--
-- test cases where we could skip fetching the table rows (TODO: #22044)
--
CREATE TABLE test_skip (a INT PRIMARY KEY, b INT);
CREATE INDEX ON test_skip(b ASC);
INSERT INTO test_skip SELECT i, i FROM generate_series(1, 10) i;

-- this query does not need a recheck, so we don't need to fetch the rows for the COUNT(*)
/*+ BitmapScan(test_skip) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT COUNT(*) FROM test_skip WHERE a = 1 OR b < 10;
/*+ BitmapScan(test_skip) */
SELECT COUNT(*) FROM test_skip WHERE a = 1 OR b < 10;

-- when we require the rows, notice that the YB Bitmap Table Scan sends a table read request
/*+ BitmapScan(test_skip) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT * FROM test_skip WHERE a = 1 OR b < 10;

-- this query has a recheck condition, so we need to fetch the rows
/*+ BitmapScan(test_skip) Set(yb_enable_expression_pushdown false) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT COUNT(*) FROM test_skip WHERE a = 1 OR (b < 10 AND b % 2 = 0);
/*+ BitmapScan(test_skip) Set(yb_enable_expression_pushdown false) */
SELECT COUNT(*) FROM test_skip WHERE a = 1 OR (b < 10 AND b % 2 = 0);

-- when the expression can be pushed down, we don't need a recheck but we do
-- still need to send the request.
/*+ BitmapScan(test_skip) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT COUNT(*) FROM test_skip WHERE a = 1 OR (b < 10 AND b % 2 = 0);
/*+ BitmapScan(test_skip) */
SELECT COUNT(*) FROM test_skip WHERE a = 1 OR (b < 10 AND b % 2 = 0);

-- other aggregates may require the rows
/*+ BitmapScan(test_skip) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT SUM(a) FROM test_skip WHERE a = 1 OR (b < 10 AND b % 2 = 0);
/*+ BitmapScan(test_skip) */
SELECT SUM(a) FROM test_skip WHERE a = 1 OR (b < 10 AND b % 2 = 0);
/*+ BitmapScan(test_skip) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT MAX(a) FROM test_skip WHERE a = 1 OR (b < 10 AND b % 2 = 0);
/*+ BitmapScan(test_skip) */
SELECT MAX(a) FROM test_skip WHERE a = 1 OR (b < 10 AND b % 2 = 0);

-- when we don't need the actual value, we can avoid fetching
/*+ BitmapScan(test_skip) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT 1 FROM test_skip WHERE a = 1 OR (b < 10 AND b % 2 = 0);
/*+ BitmapScan(test_skip) */
SELECT 1 FROM test_skip WHERE a = 1 OR (b < 10 AND b % 2 = 0);
/*+ BitmapScan(test_skip) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT random() FROM test_skip WHERE a = 1 OR (b < 10 AND b % 2 = 0);

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

CREATE TABLE pk_colo (k INT PRIMARY KEY, a INT, v varchar);
CREATE INDEX ON pk_colo(a ASC);
CREATE INDEX ON pk_colo(v ASC);
INSERT INTO pk_colo SELECT i, i, i::text FROM generate_series(1, 1000) i;

/*+ BitmapScan(pk_colo) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT * FROM pk_colo WHERE k = 123 OR a = 123 OR v = '123';
/*+ BitmapScan(pk_colo) */
SELECT * FROM pk_colo WHERE k = 123 OR a = 123 OR v = '123';

/*+ BitmapScan(pk_colo) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT * FROM pk_colo WHERE (k < 10 AND k % 2 = 1) OR (a > 990 AND a % 2 = 1) OR (v LIKE '999%' AND v LIKE '%5') ORDER BY k;
/*+ BitmapScan(pk_colo) */
SELECT * FROM pk_colo WHERE (k < 10 AND k % 2 = 1) OR (a > 990 AND a % 2 = 1) OR (v LIKE '999%' AND v LIKE '%5') ORDER BY k;

/*+ BitmapScan(pk_colo) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT * FROM pk_colo WHERE k < 5 OR (a BETWEEN 7 AND 8) OR v < '11' ORDER BY k;
/*+ BitmapScan(pk_colo) */
SELECT * FROM pk_colo WHERE k < 5 OR (a BETWEEN 7 AND 8) OR v < '11' ORDER BY k;

/*+ BitmapScan(pk_colo) */ EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT * FROM pk_colo WHERE k IN (123, 124) OR a IN (122, 123) OR v IN ('122', '125') ORDER BY k;
/*+ BitmapScan(pk_colo) */
SELECT * FROM pk_colo WHERE k IN (123, 124) OR a IN (122, 123) OR v IN ('122', '125') ORDER BY k;

-- test count
/*+ BitmapScan(pk_colo) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT COUNT(*) FROM pk_colo WHERE k IN (123, 124) OR a IN (122, 123) OR v IN ('122', '125');
/*+ BitmapScan(pk_colo) */
SELECT COUNT(*) FROM pk_colo WHERE k IN (123, 124) OR a IN (122, 123) OR v IN ('122', '125');

-- test non-existent results
/*+ BitmapScan(pk_colo) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT COUNT(*) FROM pk_colo WHERE k = 2000 OR a < 0 OR v = 'nonexistent';
/*+ BitmapScan(pk_colo) */
SELECT COUNT(*) FROM pk_colo WHERE k = 2000 OR a < 0 OR v = 'nonexistent';

RESET yb_explain_hide_non_deterministic_fields;
RESET enable_bitmapscan;
