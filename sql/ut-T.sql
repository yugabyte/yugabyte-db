-- ut-T: tests for table hints
-- This test is focusing on hint retrieval from table

LOAD 'pg_hint_plan';
SET pg_hint_plan.enable_hint TO on;
SET pg_hint_plan.debug_print TO on;
SET client_min_messages TO LOG;
SET search_path TO public;

-- test for get_query_string
INSERT INTO hint_plan.hints VALUES(DEFAULT,'EXPLAIN SELECT * FROM t1 WHERE id = ?;', '', 'SeqScan(t1)');
INSERT INTO hint_plan.hints VALUES(DEFAULT,'PREPARE p1 AS SELECT * FROM t1 WHERE id = ?;', '', 'SeqScan(t1)');
INSERT INTO hint_plan.hints VALUES(DEFAULT,'EXPLAIN DECLARE c1 CURSOR FOR SELECT * FROM t1 WHERE id = ?;', '', 'SeqScan(t1)');
INSERT INTO hint_plan.hints VALUES(DEFAULT,'EXPLAIN CREATE TABLE ct1 AS SELECT * FROM t1 WHERE id = ?;', '', 'SeqScan(t1)');

PREPARE p1 AS SELECT * FROM t1 WHERE id = 100;

-- These queries uses IndexScan without hints
SET pg_hint_plan.enable_hint_table to off;
EXPLAIN SELECT * FROM t1 WHERE id = 100;
EXPLAIN DECLARE c1 CURSOR FOR SELECT * FROM t1 WHERE id = 100;
EXPLAIN CREATE TABLE ct1 AS SELECT * FROM t1 WHERE id = 100;

EXPLAIN EXECUTE p1;
DEALLOCATE p1;
PREPARE p1 AS SELECT * FROM t1 WHERE id = 100;
EXPLAIN CREATE TABLE ct1 AS EXECUTE p1;

DEALLOCATE p1;
PREPARE p1 AS SELECT * FROM t1 WHERE id = 100;

-- Forced to use SeqScan by table hints
SET pg_hint_plan.enable_hint_table to on;
EXPLAIN SELECT * FROM t1 WHERE id = 100;
EXPLAIN DECLARE c1 CURSOR FOR SELECT * FROM t1 WHERE id = 100;
EXPLAIN CREATE TABLE ct1 AS SELECT * FROM t1 WHERE id = 100;
EXPLAIN EXECUTE p1;
DEALLOCATE p1;
PREPARE p1 AS SELECT * FROM t1 WHERE id = 100;
EXPLAIN CREATE TABLE ct1 AS EXECUTE p1;

DEALLOCATE p1;

-- Check proper calling to generate_normalized_query
\;\;SELECT 1,2;

SET pg_hint_plan.enable_hint_table to off;
DELETE FROM hint_plan.hints;

