--
-- YB Bitmap Scans (bitmap index scans + YB bitmap table scans)
--
SET yb_explain_hide_non_deterministic_fields = true;
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
/*+ BitmapScan(pk) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT * FROM pk WHERE k = 123 OR a = 123;
/*+ BitmapScan(pk) */
SELECT * FROM pk WHERE k = 123 OR a = 123;

/*+ BitmapScan(pk) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT * FROM pk WHERE k IN (123, 124) OR a IN (122, 123) ORDER BY k;
/*+ BitmapScan(pk) */
SELECT * FROM pk WHERE k IN (123, 124) OR a IN (122, 123) ORDER BY k;

/*+ BitmapScan(pk) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT * FROM pk WHERE k = 123 OR k = 124 OR a = 122 OR a = 123 ORDER BY k;
/*+ BitmapScan(pk) */
SELECT * FROM pk WHERE k = 123 OR k = 124 OR a = 122 OR a = 123 ORDER BY k;

-- test non-existent results
/*+ BitmapScan(pk) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT COUNT(*) FROM pk WHERE k = 2000 OR a < 0;
/*+ BitmapScan(pk) */
SELECT COUNT(*) FROM pk WHERE k = 2000 OR a < 0;

--
-- test indexes on multiple columns / indexes with additional columns
--
CREATE TABLE multi (a INT, b INT, c INT, h INT, PRIMARY KEY (a ASC, b ASC));
CREATE INDEX ON multi (c ASC) INCLUDE (a);
CREATE INDEX ON multi (h HASH) INCLUDE (a);
CREATE INDEX ON multi (b ASC, c ASC);
INSERT INTO multi SELECT i, i * 2, i * 3, i * 4 FROM generate_series(1, 1000) i;

/*+ BitmapScan(multi) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT * FROM multi WHERE a < 2 OR b > 1997 ORDER BY a;
/*+ BitmapScan(multi) */
SELECT * FROM multi WHERE a < 2 OR b > 1997 ORDER BY a;

/*+ BitmapScan(multi) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT * FROM multi WHERE c BETWEEN 10 AND 15 AND a < 30 ORDER BY a;
/*+ BitmapScan(multi) */
SELECT * FROM multi WHERE c BETWEEN 10 AND 15 AND a < 30 ORDER BY a;

/*+ BitmapScan(multi) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT * FROM multi WHERE a < 2 OR b > 1997 OR c BETWEEN 10 AND 15 OR h = 8 ORDER BY a;
/*+ BitmapScan(multi) */
SELECT * FROM multi WHERE a < 2 OR b > 1997 OR c BETWEEN 10 AND 15 OR h = 8  ORDER BY a;

-- try some slightly complex nested logical operands queries
/*+ BitmapScan(multi) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT * FROM multi WHERE a < 2 OR (b > 1797 AND (c BETWEEN 2709 AND 2712 OR c = 2997)) ORDER BY a;
/*+ BitmapScan(multi) */
SELECT * FROM multi WHERE a < 2 OR (b > 1797 AND (c BETWEEN 2709 AND 2712 OR c = 2997)) ORDER BY a;

/*+ BitmapScan(multi) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT * FROM multi WHERE (a < 3 AND a % 2 = 0) OR (b IN (10, 270, 1800) AND (c < 20 OR c > 2500)) ORDER BY a;
/*+ BitmapScan(multi) */
SELECT * FROM multi WHERE (a < 3 AND a % 2 = 0) OR (b IN (10, 270, 1800) AND (c < 20 OR c > 2500)) ORDER BY a;

--
-- test exceeding work_mem
--
INSERT INTO multi SELECT i, i * 2, i * 3, i * 4 FROM generate_series(1001, 10000) i;

SET work_mem TO '4MB'; -- does not exceed work_mem

/*+ BitmapScan(multi) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT * FROM multi WHERE a < 5000 OR (b > 0 AND (c < 3000 OR c > 27000)) ORDER BY a;

SET work_mem TO '4GB'; -- does not exceed very large work_mem

/*+ BitmapScan(multi) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT * FROM multi WHERE a < 5000 OR (b > 0 AND (c < 3000 OR c > 27000)) ORDER BY a;

SET work_mem TO '100kB';
-- normal case
/*+ BitmapScan(multi) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT * FROM multi WHERE a < 5000 OR (b > 0 AND (c < 3000 OR c > 27000)) ORDER BY a;

-- verify remote filters apply to the table scan when we've exceeded work_mem
/*+ BitmapScan(multi) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT * FROM multi WHERE a < 5000 OR (b > 0 AND (c < 3000 OR c > 27000) AND b % 2 = 0) ORDER BY a;

-- verify local filters still apply when pushdown is disabled
/*+ BitmapScan(multi) Set(yb_enable_expression_pushdown false) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT * FROM multi WHERE a < 5000 OR (b > 0 AND (c < 3000 OR c > 27000) AND b % 2 = 0) ORDER BY a;

-- where the first bitmap index scan exceeds work_mem
/*+ BitmapScan(multi) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT * FROM multi WHERE a < 5000 OR (b > 0 AND (c < 3000 OR c > 27000)) ORDER BY a;

-- where the second bitmap index scan exceeds work_mem
/*+ BitmapScan(multi) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT * FROM multi WHERE a < 1000 OR (b > 0 AND (c < 15000 OR c > 27000)) ORDER BY a;

-- where the third bitmap index scan exceeds work_mem
/*+ BitmapScan(multi) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT * FROM multi WHERE a < 1000 OR (b > 0 AND (c < 3000 OR c > 15000)) ORDER BY a;

-- check aggregates
SET yb_enable_expression_pushdown = true;
/*+ BitmapScan(multi) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT COUNT(*) FROM multi WHERE a > 10;
/*+ BitmapScan(multi) */
SELECT COUNT(*) FROM multi WHERE a > 10;

SET yb_enable_expression_pushdown = false;
/*+ BitmapScan(multi) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT COUNT(*) FROM multi WHERE a > 10;
/*+ BitmapScan(multi) */
SELECT COUNT(*) FROM multi WHERE a > 10;

RESET yb_enable_expression_pushdown;
RESET work_mem;

--
-- test limits
--
CREATE TABLE test_limit (a INT, b INT, c INT);
CREATE INDEX ON test_limit (a ASC);
INSERT INTO test_limit SELECT i, i * 2, i * 3 FROM generate_series(1, 1000) i;
SET yb_fetch_row_limit = 100;
SET yb_fetch_size_limit = 0;

/*+ BitmapScan(test_limit) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT * FROM test_limit WHERE a < 200 LIMIT 10;

SET yb_fetch_row_limit = 0;
SET yb_fetch_size_limit = '1kB';

/*+ BitmapScan(test_limit) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT * FROM test_limit WHERE a < 200 LIMIT 10;

RESET yb_fetch_row_limit;
RESET yb_fetch_size_limit;

--
-- test remote pushdown
--

/*+ BitmapScan(multi multi_b_c_idx) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT * FROM multi WHERE (b < 10 AND b % 4 = 0) ORDER BY b;
/*+ BitmapScan(multi multi_b_c_idx) */
SELECT * FROM multi WHERE (b < 10 AND b % 4 = 0) ORDER BY b;

/*+ BitmapScan(multi multi_b_c_idx) Set(yb_enable_expression_pushdown false) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT * FROM multi WHERE (b < 10 AND b % 4 = 0) ORDER BY b;
/*+ BitmapScan(multi multi_b_c_idx) Set(yb_enable_expression_pushdown false) */
SELECT * FROM multi WHERE (b < 10 AND b % 4 = 0) ORDER BY b;

/*+ BitmapScan(multi) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT * FROM multi WHERE (a < 5 AND a % 2 = 0) OR (c <= 10 AND a % 3 = 0) ORDER BY a;
/*+ BitmapScan(multi) */
SELECT * FROM multi WHERE (a < 5 AND a % 2 = 0) OR (c <= 10 AND a % 3 = 0) ORDER BY a;

/*+ BitmapScan(multi) Set(yb_enable_expression_pushdown false) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
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

/*+ BitmapScan(test_false) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF) SELECT * FROM test_false WHERE (a <= 1 AND a = 2);
/*+ BitmapScan(test_false) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF) SELECT * FROM test_false WHERE (a = 1 AND a = 2) OR b = 0;

--
-- #21930: test recheck conditions on text columns
--
CREATE TABLE test_recheck_text(a TEXT);
INSERT INTO test_recheck_text(a) VALUES ('i');
CREATE INDEX ON test_recheck_text(a ASC);

/*+ BitmapScan(t) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT * FROM test_recheck_text AS t WHERE a = 'i' AND a < 'j';
/*+ BitmapScan(t) */
SELECT * FROM test_recheck_text AS t WHERE a = 'i' AND a < 'j';

--
-- test recheck index conditions
--
create table recheck_test (col int);
create index on recheck_test (col ASC);

insert into recheck_test select i from generate_series(1, 10) i;

explain (analyze, COSTS OFF, SUMMARY OFF) /*+ BitmapScan(t) */
SELECT * FROM recheck_test t WHERE t.col < 3 AND t.col IN (5, 6);
explain (analyze, COSTS OFF, SUMMARY OFF) /*+ BitmapScan(t) */
SELECT * FROM recheck_test t WHERE t.col IN (5, 6) AND t.col < 3;

explain (analyze, COSTS OFF, SUMMARY OFF) /*+ BitmapScan(t) */
SELECT * FROM recheck_test t WHERE t.col < 3 AND t.col = 5;
explain (analyze, COSTS OFF, SUMMARY OFF) /*+ BitmapScan(t) */
SELECT * FROM recheck_test t WHERE t.col = 5 AND t.col < 3;

--
-- #22065: test pushdowns on Subplans
--
CREATE TABLE text_pushdown_test (a text, b text);
CREATE INDEX ON text_pushdown_test (a ASC);
CREATE INDEX ON text_pushdown_test (b ASC);
INSERT INTO text_pushdown_test VALUES ('hi', 'hello');

/*+ BitmapScan(text_pushdown_test) */
SELECT * FROM text_pushdown_test WHERE a = 'hi' AND a <> (SELECT a FROM text_pushdown_test WHERE b = 'hello');
EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF) /*+ BitmapScan(text_pushdown_test) */
SELECT * FROM text_pushdown_test WHERE a = 'hi' AND a <> (SELECT a FROM text_pushdown_test WHERE b = 'hello');

--
-- #22622: test local recheck of condition for a column that is not a target
--
create table local_recheck_test (k1 character(10), k2 character(10));
insert into local_recheck_test values ('a', 'a');
create index on local_recheck_test(k1 ASC);
create index on local_recheck_test(k2 ASC);

/*+ IndexScan(t) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
select k1 from local_recheck_test t where k2 like 'a%' AND k2 IN ('a', 'b', 'c');

-- the second condition is necessary to force a recheck
-- the first condition is rechecked locally, so k2 must be fetched.
/*+ BitmapScan(t) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
select k1 from local_recheck_test t where k2 like 'a%' AND k2 IN ('a', 'b', 'c');

--
-- test pk of different types
--
CREATE TABLE pk_int (k_int INT PRIMARY KEY, v_text varchar);
CREATE INDEX ON pk_int(v_text ASC);
INSERT INTO pk_int SELECT i, i::text FROM generate_series(1, 100) i;

CREATE TABLE pk_text (k_text varchar PRIMARY KEY, v_int INT);
CREATE INDEX ON pk_text(v_int ASC);
INSERT INTO pk_text SELECT i::text, i FROM generate_series(1, 100) i;

/*+ BitmapScan(pk_int) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT * FROM pk_int WHERE k_int = 12 OR v_text = '12';
/*+ BitmapScan(pk_int) */
SELECT * FROM pk_int WHERE k_int = 12 OR v_text = '12';

/*+ BitmapScan(pk_text) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT * FROM pk_text WHERE v_int = 12 OR k_text = '12';
/*+ BitmapScan(pk_text) */
SELECT * FROM pk_text WHERE v_int = 12 OR k_text = '12';

/*+ BitmapScan(pk_int) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT * FROM pk_int WHERE (k_int < 10 AND k_int % 2 = 1) OR (v_text LIKE '999%' AND v_text LIKE '%5') ORDER BY k_int;
/*+ BitmapScan(pk_int) */
SELECT * FROM pk_int WHERE (k_int < 10 AND k_int % 2 = 1) OR (v_text LIKE '999%' AND v_text LIKE '%5') ORDER BY k_int;

/*+ BitmapScan(pk_text) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT * FROM pk_text WHERE (v_int < 10 AND v_int % 2 = 1) OR (k_text LIKE '999%' AND k_text LIKE '%5') ORDER BY v_int;
/*+ BitmapScan(pk_text) */
SELECT * FROM pk_text WHERE (v_int < 10 AND v_int % 2 = 1) OR (k_text LIKE '999%' AND k_text LIKE '%5') ORDER BY v_int;

/*+ BitmapScan(pk_int) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT * FROM pk_int WHERE k_int < 5 OR v_text < '11' ORDER BY k_int;
/*+ BitmapScan(pk_int) */
SELECT * FROM pk_int WHERE k_int < 5 OR v_text < '11' ORDER BY k_int;

/*+ BitmapScan(pk_text) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT * FROM pk_text WHERE v_int < 5 OR k_text < '11' ORDER BY v_int;
/*+ BitmapScan(pk_text) */
SELECT * FROM pk_text WHERE v_int < 5 OR k_text < '11' ORDER BY v_int;

/*+ BitmapScan(pk_int) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT * FROM pk_int WHERE k_int IN (12, 13) OR v_text IN ('11', '12') ORDER BY k_int;
/*+ BitmapScan(pk_int) */
SELECT * FROM pk_int WHERE k_int IN (12, 13) OR v_text IN ('11', '12') ORDER BY k_int;

/*+ BitmapScan(pk_text) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT * FROM pk_text WHERE v_int IN (12, 13) OR k_text IN ('11', '12') ORDER BY v_int;
/*+ BitmapScan(pk_text) */
SELECT * FROM pk_text WHERE v_int IN (12, 13) OR k_text IN ('11', '12') ORDER BY v_int;

-- test count
/*+ BitmapScan(pk_int) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT COUNT(*) FROM pk_int WHERE k_int IN (12, 13) OR v_text IN ('11', '12');
/*+ BitmapScan(pk_int) */
SELECT COUNT(*) FROM pk_int WHERE k_int IN (12, 13) OR v_text IN ('11', '12');

/*+ BitmapScan(pk_text) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT COUNT(*) FROM pk_text WHERE v_int IN (12, 13) OR k_text IN ('11', '12');
/*+ BitmapScan(pk_text) */
SELECT COUNT(*) FROM pk_text WHERE v_int IN (12, 13) OR k_text IN ('11', '12');

-- test non-existent results
/*+ BitmapScan(pk_int) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT COUNT(*) FROM pk_int WHERE k_int = 2000 OR v_text = 'nonexistent';
/*+ BitmapScan(pk_int) */
SELECT COUNT(*) FROM pk_int WHERE k_int = 2000 OR v_text = 'nonexistent';

/*+ BitmapScan(pk_text) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT COUNT(*) FROM pk_text WHERE v_int = 2000 OR k_text = 'nonexistent';
/*+ BitmapScan(pk_text) */
SELECT COUNT(*) FROM pk_text WHERE v_int = 2000 OR k_text = 'nonexistent';

--
-- #21793: test row compare expressions
--
CREATE TABLE test_crash_row_comp (a integer);
INSERT INTO test_crash_row_comp VALUES (1);
CREATE INDEX on test_crash_row_comp(a ASC);

/*+ BitmapScan(t) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT * FROM test_crash_row_comp AS t WHERE (a, a) <= (10, 1) OR a IS NULL;
/*+ BitmapScan(t) */
SELECT * FROM test_crash_row_comp AS t WHERE (a, a) <= (10, 1) OR a IS NULL;


RESET yb_explain_hide_non_deterministic_fields;
RESET enable_bitmapscan;
