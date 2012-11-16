LOAD 'pg_hint_plan';
SET pg_hint_plan.enable TO on;
SET pg_hint_plan.debug_print TO on;
SET client_min_messages TO LOG;
SET search_path TO public;

----
---- No. A-8-1 hint state output
----

PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 = 1;
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1;
DEALLOCATE p1;

PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 < $1;
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1);
DEALLOCATE p1;

-- No. A-8-1-9
-- No. A-8-1-10
/*+SeqScan(t1)*/
PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 = 1;
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1;
UPDATE pg_catalog.pg_class SET relpages = relpages WHERE relname = 't1';
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1;
DEALLOCATE p1;

/*+BitmapScan(t1)*/
PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 < $1;
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1);
UPDATE pg_catalog.pg_class SET relpages = relpages WHERE relname = 't1';
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1);
DEALLOCATE p1;

-- No. A-8-1-11
-- No. A-8-1-12
/*+SeqScan(t1)*/
PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 = 1;
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1;
UPDATE pg_catalog.pg_class SET relpages = relpages WHERE relname = 't1';
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1;
DEALLOCATE p1;

/*+BitmapScan(t1)*/
PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 < $1;
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1);
UPDATE pg_catalog.pg_class SET relpages = relpages WHERE relname = 't1';
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1);
DEALLOCATE p1;

-- No. A-8-1-13
-- No. A-8-1-14
PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 = 1;
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1;
UPDATE pg_catalog.pg_class SET relpages = relpages WHERE relname = 't1';
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1;
DEALLOCATE p1;

PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 < $1;
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1);
UPDATE pg_catalog.pg_class SET relpages = relpages WHERE relname = 't1';
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1);
DEALLOCATE p1;

