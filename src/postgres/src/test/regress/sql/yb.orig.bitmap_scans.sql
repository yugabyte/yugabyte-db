--
-- YB Bitmap Scans (bitmap index scans + YB bitmap table scans)
--
\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/parameterized_query.sql'
\i :filename
\set P1 ':explain'
\set P2

SET yb_enable_bitmapscan = true;
SET enable_bitmapscan = true;

CREATE TABLE simple (k INT PRIMARY KEY, ind_a INT, ind_b INT);
INSERT INTO simple SELECT i, i * 2, i * 3 FROM generate_series(1, 10) i;
CREATE INDEX ON simple(ind_a ASC);
CREATE INDEX ON simple(ind_b ASC);

\set explain 'EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)'
\set Q1 '/*+ BitmapScan(simple) */'
\set query ':P :Q1 SELECT * FROM simple WHERE k = 1;'
\i :iter_P2

\set query ':P :Q1 SELECT * FROM simple WHERE ind_a < 5 ORDER BY k;'
\i :iter_P2

\set query ':P :Q1 SELECT * FROM simple WHERE ind_a BETWEEN 2 AND 6 OR ind_b > 25 ORDER BY k;'
\i :iter_P2

--
-- test cases where we could skip fetching the table rows (TODO: #22044)
--
-- this query does not need a recheck, so we don't need to fetch the rows for the COUNT(*)
\set explain 'EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)'
\set query ':P :Q1 SELECT COUNT(*) FROM simple WHERE ind_a = 2 OR ind_b < 10;'
\i :iter_P2

-- when we require the rows, notice that the YB Bitmap Table Scan sends a table read request
\set query ':explain :Q1 SELECT * FROM simple WHERE ind_a = 2 OR ind_b < 10;'
:query

-- this query has a recheck condition, so we need to fetch the rows
-- TODO(#29894): seems "test_skip" in the hint should be "simple".
/*+ BitmapScan(simple) Set(yb_enable_expression_pushdown false) */ EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)
SELECT COUNT(*) FROM simple WHERE ind_a = 2 OR (ind_b < 10 AND ind_b % 2 = 0);
/*+ BitmapScan(test_skip) Set(yb_enable_expression_pushdown false) */
SELECT COUNT(*) FROM simple WHERE ind_a = 2 OR (ind_b < 10 AND ind_b % 2 = 0);

-- when the expression can be pushed down, we don't need a recheck but we do
-- still need to send the request.
\set query ':P :Q1 SELECT COUNT(*) FROM simple WHERE ind_a = 2 OR (ind_b < 10 AND ind_b % 2 = 0);'
\i :iter_P2

-- other aggregates may require the rows
\set query ':P :Q1 SELECT SUM(ind_a) FROM simple WHERE ind_a = 2 OR (ind_b < 10 AND ind_b % 2 = 0);'
\i :iter_P2
\set query ':P :Q1 SELECT MAX(ind_a) FROM simple WHERE ind_a = 2 OR (ind_b < 10 AND ind_b % 2 = 0);'
\i :iter_P2

-- when we don't need the actual value, we can avoid fetching
\set query ':P :Q1 SELECT 1 FROM simple WHERE ind_a = 2 OR (ind_b < 10 AND ind_b % 2 = 0);'
\i :iter_P2
\set query ':explain :Q1 SELECT random() FROM simple WHERE ind_a = 2 OR (ind_b < 10 AND ind_b % 2 = 0);'
:query

--
-- test primary key queries
--
\set query ':P :Q1 SELECT * FROM simple WHERE k = 1 OR ind_a = 2;'
\i :iter_P2

\set query ':P :Q1 SELECT * FROM simple WHERE k IN (1, 2) OR ind_a IN (4, 6) ORDER BY k;'
\i :iter_P2

\set query ':P :Q1 SELECT * FROM simple WHERE k = 1 OR k = 2 OR ind_a = 4 OR ind_a = 6 ORDER BY k;'
\i :iter_P2

-- test non-existent results
\set query ':P :Q1 SELECT COUNT(*) FROM simple WHERE k = 2000 OR ind_a < 0;'
\i :iter_P2

--
-- test unsatisfiable conditions
--
SET yb_test_skip_binding_scan_keys = true;
\set query ':explain :Q1 SELECT * FROM simple WHERE (k <= 1 AND k = 2);'
:query
\set query ':explain :Q1 SELECT * FROM simple WHERE (k = 1 AND k = 2) OR ind_b = 0;'
:query
\set query ':explain :Q1 SELECT * FROM simple WHERE (ind_a <= 1 AND ind_a = 2);'
:query
\set query ':explain :Q1 SELECT * FROM simple WHERE (ind_a = 2 AND ind_a = 2) OR ind_b = 0;'
:query
RESET yb_test_skip_binding_scan_keys;

--
-- #21793: test row compare expressions
--
\set explain 'EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)'
\set query ':P :Q1 SELECT * FROM simple WHERE (ind_a, ind_a) <= (4, 1) OR ind_a IS NULL;'
\i :iter_P2

--
-- #22062: variable not found in subplan target list
--
EXPLAIN (COSTS OFF) /*+ BitmapScan(simple) */
SELECT * FROM simple AS tbl_1 JOIN simple AS tbl_2 ON tbl_1.ind_a = tbl_2.ind_a OR (tbl_1.ind_b = tbl_2.ind_a AND tbl_1.ind_a = 2);

--
-- #22065: test pushdowns on Subplans
--
\set Q1 '/*+ BitmapScan(simple_1) BitmapScan(simple) */'
\set query ':P :Q1 SELECT * FROM simple WHERE ind_a = 2 AND ind_a <> (SELECT ind_a FROM simple WHERE ind_b = 1);'
\i :iter_P2

--
-- test UPDATE
--

\set Q1 '/*+ BitmapScan(simple) */'
\set query ':explain :Q1 UPDATE simple SET ind_a = NULL WHERE ind_a < 10 OR ind_b > 25;'
:query

\set query ':P :Q1 SELECT ind_a, ind_b FROM simple WHERE ind_a < 10 OR ind_a IS NULL OR ind_b < 15 ORDER BY k;'
\i :iter_P2

-- NOTE: this GUC cannot be set using pg_hint_plan Set(...) because it is
-- currently used during execution, not just planning.
SET yb_test_skip_binding_scan_keys = true;

\i :iter_P2

RESET yb_test_skip_binding_scan_keys;

-- use Bitmap Scan to delete rows and validate their deletion
\set query ':explain :Q1 DELETE FROM simple WHERE ind_a IS NULL OR ind_b < 15;'
:query

\set query ':P :Q1 SELECT ind_a, ind_b FROM simple WHERE ind_a IS NULL OR ind_b < 15 ORDER BY k;'
\i :iter_P2


--
-- test indexes on multiple columns / indexes with additional columns
--
CREATE TABLE multi (a INT, b INT, c INT, h INT, PRIMARY KEY (a ASC, b ASC));
CREATE INDEX ON multi (c ASC) INCLUDE (a);
CREATE INDEX ON multi (h HASH) INCLUDE (a);
CREATE INDEX ON multi (b ASC, c ASC);
INSERT INTO multi SELECT i, i * 2, i * 3, i * 4 FROM generate_series(1, 1000) i;

\set explain 'EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)'
\set Q1 '/*+ BitmapScan(multi) */'
\set query ':P :Q1 SELECT * FROM multi WHERE a < 2 OR b > 1997 ORDER BY a;'
\i :iter_P2

\set query ':P :Q1 SELECT * FROM multi WHERE c BETWEEN 10 AND 15 AND a < 30 ORDER BY a;'
\i :iter_P2

\set query ':P :Q1 SELECT * FROM multi WHERE a < 2 OR b > 1997 OR c BETWEEN 10 AND 15 OR h = 8 ORDER BY a;'
\i :iter_P2

-- try some slightly complex nested logical operands queries
\set explain 'EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)'
\set query ':P :Q1 SELECT * FROM multi WHERE a < 2 OR (b > 1797 AND (c BETWEEN 2709 AND 2712 OR c = 2997)) ORDER BY a;'
\i :iter_P2

\set query ':P :Q1 SELECT * FROM multi WHERE (a < 3 AND a % 2 = 0) OR (b IN (10, 270, 1800) AND (c < 20 OR c > 2500)) ORDER BY a;'
\i :iter_P2

--
-- test limits
--
SET yb_fetch_row_limit = 100;
SET yb_fetch_size_limit = 0;

\set explain 'EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)'
\set query ':explain :Q1 SELECT * FROM multi WHERE a < 200 LIMIT 10;'
:query

SET yb_fetch_row_limit = 0;
SET yb_fetch_size_limit = '1kB';

\set explain 'EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)'
:query

RESET yb_fetch_row_limit;
RESET yb_fetch_size_limit;

--
-- test exceeding work_mem
--
INSERT INTO multi SELECT i, i * 2, i * 3, i * 4 FROM generate_series(1001, 10000) i;

SET work_mem TO '4MB'; -- does not exceed work_mem

\set query ':explain :Q1 SELECT * FROM multi WHERE a < 5000 OR (b > 0 AND (c < 3000 OR c > 27000)) ORDER BY a;'
:query

SET work_mem TO '4GB'; -- does not exceed very large work_mem

:query

SET work_mem TO '100kB';
-- normal case
:query

-- verify remote filters apply to the table scan when we've exceeded work_mem
-- hint2: verify local filters still apply when pushdown is disabled
\set Q2 '/*+ BitmapScan(multi) Set(yb_enable_expression_pushdown false) */'
\set query ':explain :Q SELECT * FROM multi WHERE a < 5000 OR (b > 0 AND (c < 3000 OR c > 27000) AND b % 2 = 0) ORDER BY a;'
\i :iter_Q2

-- where the first bitmap index scan exceeds work_mem
\set query ':explain :Q1 SELECT * FROM multi WHERE a < 5000 OR (b > 0 AND (c < 3000 OR c > 27000)) ORDER BY a;'
:query

-- where the second bitmap index scan exceeds work_mem
\set query ':explain :Q1 SELECT * FROM multi WHERE a < 1000 OR (b > 0 AND (c < 15000 OR c > 27000)) ORDER BY a;'
:query

-- where the third bitmap index scan exceeds work_mem
\set query ':explain :Q1 SELECT * FROM multi WHERE a < 1000 OR (b > 0 AND (c < 3000 OR c > 15000)) ORDER BY a;'
:query

-- check aggregate pushdown
SET yb_enable_expression_pushdown = true;
SET yb_enable_index_aggregate_pushdown = true;
\set query ':P :Q1 SELECT COUNT(*) FROM multi WHERE a > 10;'
\i :iter_P2

SET yb_enable_expression_pushdown = false;
SET yb_enable_index_aggregate_pushdown = true;
\set query ':P :Q1 SELECT COUNT(*) FROM multi WHERE a > 10;'
\i :iter_P2

SET yb_enable_expression_pushdown = false;
SET yb_enable_index_aggregate_pushdown = false;
\set query ':P :Q1 SELECT COUNT(*) FROM multi WHERE a > 10;'
\i :iter_P2

SET yb_enable_expression_pushdown = true;
SET yb_enable_index_aggregate_pushdown = false;
\set query ':P :Q1 SELECT COUNT(*) FROM multi WHERE a > 10;'
\i :iter_P2

RESET yb_enable_index_aggregate_pushdown;
RESET yb_enable_expression_pushdown;

RESET work_mem;

--
-- test remote pushdown
--

\set explain 'EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)'
\set Q1 '/*+ BitmapScan(multi multi_b_c_idx) */'
\set Q2 '/*+ BitmapScan(multi multi_b_c_idx) Set(yb_enable_expression_pushdown false) */'
\set query ':P :Q SELECT * FROM multi WHERE (b < 10 AND b % 4 = 0) ORDER BY b;'
\set Pnext :iter_Q2
\i :iter_P2

\set Q1 '/*+ BitmapScan(multi) */'
\set Q2 '/*+ BitmapScan(multi) Set(yb_enable_expression_pushdown false) */'
\set query ':P :Q SELECT * FROM multi WHERE (a < 5 AND a % 2 = 0) OR (c <= 10 AND a % 3 = 0) ORDER BY a;'
\i :iter_P2

--
-- test recheck index conditions
--
create table recheck_test (int4_col int4, text_col TEXT, bigint_col bigint);
create index on recheck_test (int4_col ASC);
create index on recheck_test (text_col ASC);
create index on recheck_test (bigint_col ASC);

insert into recheck_test VALUES
    (1, 'a', 1),
    (2, 'b', 2),
    (3, 'c', 3),
    (4, 'd', 4),
    (5, 'e', 5),
    (6, 'f', 6),
    (7, 'g', 7),
    (8, 'h', 2147483647),
    (9, 'i', 2147483648);

-- #21930: test recheck conditions on text columns
\set Q1 '/*+ BitmapScan(t) */'
SELECT $$
:P :Q1 SELECT text_col FROM recheck_test AS t WHERE text_col = 'i' AND text_col < 'j';
$$ AS query \gset
\set Pnext :iter_query
\i :iter_P2

-- PxR: explain/run x target (int4_col vs COUNT(*))
\set explain 'EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)'
\set R1 'int4_col'
\set R2 'COUNT(*)'
\set Pnext :iter_R2

\set query ':P :Q1 SELECT :R FROM recheck_test t WHERE int4_col < 3 AND int4_col IN (2, 5, 6);'
\i :iter_P2
\set query ':P :Q1 SELECT :R FROM recheck_test t WHERE int4_col IN (2, 5, 6) AND int4_col < 3;'
\i :iter_P2

\set query ':P :Q1 SELECT :R FROM recheck_test t WHERE int4_col < 3 AND int4_col = 5;'
\i :iter_P2
\set query ':P :Q1 SELECT :R FROM recheck_test t WHERE int4_col = 5 AND int4_col < 3;'
\i :iter_P2

-- PxQxR: explain/run x expression_pushdown x target
-- test where casting may cause us to require recheck
\set explain 'EXPLAIN (ANALYZE, TIMING OFF, COSTS OFF)'
\set Q1 '/*+ BitmapScan(t) */'
\set Q2 '/*+ BitmapScan(t) Set(yb_enable_expression_pushdown false) */'
\set Pnext :iter_Q2
\set Qnext :iter_R2

\set query ':P :Q SELECT :R FROM recheck_test t WHERE int4_col = 2147483647;'
\i :iter_P2
\set query ':P :Q SELECT :R FROM recheck_test t WHERE int4_col = 2147483648;'
\i :iter_P2

\set Q1 '/*+ NestLoop(s t) BitmapScan(t) Set(yb_bnl_batch_size 1) */'
\set Q2 '/*+ NestLoop(s t) BitmapScan(t) Set(yb_bnl_batch_size 1) Set(yb_enable_expression_pushdown false) */'
\set Qnext :iter_query
\set query ':explain :Q SELECT s.bigint_col, t.int4_col FROM recheck_test s JOIN recheck_test t ON s.bigint_col = t.int4_col;'
\i :iter_Q2

--
-- #22622: test local recheck of condition for a column that is not a target
--
\set explain 'EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)'
\set Q1 '/*+ IndexScan(t) */'
-- the second condition is necessary to force a recheck
-- the first condition is rechecked locally, so k2 must be fetched.
\set Q2 '/*+ BitmapScan(t) */'
SELECT $$
:explain :Q select int4_col from recheck_test t where text_col like 'a%' AND text_col IN ('a', 'b', 'c');
$$ AS query \gset
\i :iter_Q2

-- also check for aggregate pushdowns
SELECT $$
:P :Q select count(*) from recheck_test t where text_col like 'a%' AND text_col IN ('a', 'b', 'c');
$$ AS query \gset
\set Pnext :iter_Q2
\i :iter_P2

--
-- test pk of different types
--
CREATE TABLE pk_text (k_text varchar PRIMARY KEY, v_int INT);
CREATE INDEX ON pk_text(v_int ASC);
INSERT INTO pk_text SELECT i::text, i FROM generate_series(1, 100) i;

\set explain 'EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF)'
\set Q1 '/*+ BitmapScan(pk_text) */'
SELECT $$
:P :Q1 SELECT * FROM pk_text WHERE v_int = 12 OR k_text = '12';
$$ AS query \gset
\set Pnext :iter_query
\i :iter_P2

SELECT $$
:P :Q1 SELECT * FROM pk_text WHERE (v_int < 10 AND v_int % 2 = 1) OR (k_text LIKE '999%' AND k_text LIKE '%5') ORDER BY v_int;
$$ AS query \gset
\i :iter_P2

SELECT $$
:P :Q1 SELECT * FROM pk_text WHERE v_int < 5 OR k_text < '11' ORDER BY v_int;
$$ AS query \gset
\i :iter_P2

SELECT $$
:P :Q1 SELECT * FROM pk_text WHERE v_int IN (12, 13) OR k_text IN ('11', '12') ORDER BY v_int;
$$ AS query \gset
\i :iter_P2

-- test count
\set explain 'EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)'
SELECT $$
:P :Q1 SELECT COUNT(*) FROM pk_text WHERE v_int IN (12, 13) OR k_text IN ('11', '12');
$$ AS query \gset
\i :iter_P2

-- test non-existent results
SELECT $$
:P :Q1 SELECT COUNT(*) FROM pk_text WHERE v_int = 2000 OR k_text = 'nonexistent';
$$ AS query \gset
\i :iter_P2

--
-- BitmapAnd
-- These tests require CBO because the basic cost model does not properly cost
-- remote filters, so BitmapAnds were comparatively more expensive.
--
-- Hints can tell the planner to use a Bitmap Scan, but they cannot tell the
-- planner how to design the plan. The planner chooses for itself how it should
-- apply remote filters, Bitmap Ands, Bitmap Ors. For these queries, we are
-- interested in testing the behaviour of a BitmapAnd, not testing the planner's
-- ability to choose between a bitmap scan and a sequential scan. IF the planner
-- must choose a bitmap scan, what plan does it choose? Does that plan work?
--
SET yb_enable_base_scans_cost_model = true;
SET yb_fetch_size_limit = 0;
SET yb_fetch_row_limit = 0;

CREATE TABLE test_and (a INT, b INT, c INT);
CREATE INDEX ON test_and (a ASC);
CREATE INDEX ON test_and (b ASC);
CREATE INDEX ON test_and (c ASC);
INSERT INTO test_and SELECT i, j, k FROM generate_series(1, 50) i, generate_series(1, 52) j, generate_series(1, 56) k;
ALTER TABLE test_and ALTER COLUMN a SET STATISTICS 1000;
ALTER TABLE test_and ALTER COLUMN b SET STATISTICS 1000;
ALTER TABLE test_and ALTER COLUMN c SET STATISTICS 1000;
ANALYZE test_and;

\set explain 'EXPLAIN (ANALYZE, SUMMARY OFF, COSTS OFF)'
\set Q1 '/*+ BitmapScan(t) */'
\set query ':explain :Q1 SELECT * FROM test_and t WHERE a < 6 AND b < 6;'
:query
\set query ':explain :Q1 SELECT * FROM test_and t WHERE a < 6 AND c < 6;'
:query
\set query ':explain :Q1 SELECT * FROM test_and t WHERE b < 6 AND c < 6;'
:query
\set query ':explain :Q1 SELECT * FROM test_and t WHERE a < 6 AND b < 6 AND c < 7;'
:query

\set query ':explain :Q1 SELECT * FROM test_and t WHERE a < 10 AND b < 10;'
:query
\set query ':explain :Q1 SELECT * FROM test_and t WHERE a < 10 AND c < 10;'
:query
\set query ':explain :Q1 SELECT * FROM test_and t WHERE b < 10 AND c < 10;'
:query
\set query ':explain :Q1 SELECT * FROM test_and t WHERE a < 10 AND b < 10 AND c < 10;'
:query

-- complex nested queries
\set query ':explain :Q1 SELECT * FROM test_and t WHERE a < 5 AND (b < 3 OR b > 48);'
:query
\set query ':explain :Q1 SELECT * FROM test_and t WHERE (b < 3 AND a < 5) OR (b > 48 AND a < 5);'
:query
\set query ':explain :Q1 SELECT * FROM test_and t WHERE (b < 3 AND a < 5) OR (b > 48 AND a < 7);'
:query

RESET yb_fetch_size_limit;
RESET yb_fetch_row_limit;
RESET yb_enable_base_scans_cost_model;
RESET enable_bitmapscan;
