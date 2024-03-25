--
-- PARALLEL queries to colocated tables
--

CREATE DATABASE pctest colocation = true;
\c pctest

-- create tables
CREATE TABLE pctest1(k int primary key, a int, b int, c int, d text)
    WITH (colocation = true);
CREATE TABLE pctest2(k int primary key, a int, b int, c int, d text)
    WITH (colocation = true);
CREATE UNIQUE INDEX ON pctest1(a);
CREATE INDEX ON pctest1(c);
CREATE INDEX ON pctest2(b);
INSERT INTO pctest1
    SELECT i, 1000 - i, i/3, i%50, 'Value' || i::text FROM generate_series(1, 1000) i;
INSERT INTO pctest2
    SELECT i, 200 + i, i/5, i%10, 'Other value ' || i::text FROM generate_series(1, 200) i;

-- enable parallel query for YB tables
set yb_parallel_range_rows  to 1;
set yb_enable_base_scans_cost_model to true;

-- encourage use of parallel plans
set parallel_setup_cost=0;
set parallel_tuple_cost=0;
set enable_bitmapscan = false;

-- Parallel sequential scan
EXPLAIN (costs off)
SELECT * FROM pctest1 WHERE d LIKE 'Value_9';
SELECT * FROM pctest1 WHERE d LIKE 'Value_9';

-- with aggregates
EXPLAIN (costs off)
SELECT count(*) FROM pctest1 WHERE d LIKE 'Value_9';
SELECT count(*) FROM pctest1 WHERE d LIKE 'Value_9';

-- with sort
EXPLAIN (costs off)
SELECT * FROM pctest1 WHERE d LIKE 'Value_9' ORDER BY b DESC;
SELECT * FROM pctest1 WHERE d LIKE 'Value_9' ORDER BY b DESC;

-- with grouping
EXPLAIN (costs off)
SELECT b, count(*) FROM pctest1 WHERE d LIKE 'Value9%' GROUP BY b;
SELECT b, count(*) FROM pctest1 WHERE d LIKE 'Value9%' GROUP BY b;

-- Parallel index scan
EXPLAIN (costs off)
SELECT * FROM pctest1 WHERE k < 10;
SELECT * FROM pctest1 WHERE k < 10;

EXPLAIN (costs off)
SELECT * FROM pctest1 WHERE d LIKE 'Value_9' ORDER BY k;
SELECT * FROM pctest1 WHERE d LIKE 'Value_9' ORDER BY k;

--secondary index
EXPLAIN (costs off)
SELECT * FROM pctest1 WHERE c = 10;
SELECT * FROM pctest1 WHERE c = 10;

--secondary index
EXPLAIN (costs off)
SELECT * FROM pctest1 ORDER BY a LIMIT 10;
SELECT * FROM pctest1 ORDER BY a LIMIT 10;

-- with aggregates
EXPLAIN (costs off)
SELECT count(*) FROM pctest1 WHERE k > 123;
SELECT count(*) FROM pctest1 WHERE k > 123;

-- index only
EXPLAIN (costs off)
SELECT a FROM pctest1 WHERE a < 10;
SELECT a FROM pctest1 WHERE a < 10;

-- with grouping
EXPLAIN (costs off)
SELECT c, count(*) FROM pctest1 WHERE c > 40 GROUP BY c;
SELECT c, count(*) FROM pctest1 WHERE c > 40 GROUP BY c;

-- Joins
-- Nest loop
EXPLAIN (costs off)
SELECT pctest1.* FROM pctest1, pctest2
  WHERE pctest1.a = pctest2.b and pctest1.a % 10 = 0;
SELECT pctest1.* FROM pctest1, pctest2
  WHERE pctest1.a = pctest2.b and pctest1.a % 10 = 0;
EXPLAIN (costs off)
/*+YbBatchedNL(pctest1 pctest2)*/ SELECT pctest1.*, pctest2.k FROM pctest1, pctest2
  WHERE pctest1.c = 42 AND pctest1.k = pctest2.k ORDER BY pctest1.k;
/*+YbBatchedNL(pctest1 pctest2)*/ SELECT pctest1.*, pctest2.k FROM pctest1, pctest2
  WHERE pctest1.c = 42 AND pctest1.k = pctest2.k ORDER BY pctest1.k;

-- Hash join
EXPLAIN (costs off)
SELECT pctest1.k, pctest2.k FROM pctest1 JOIN pctest2 USING (a, c);
SELECT pctest1.k, pctest2.k FROM pctest1 JOIN pctest2 USING (a, c);

-- Merge join
set enable_hashjoin to false;
EXPLAIN (costs off)
SELECT pctest1.*, pctest2.k FROM pctest1, pctest2
  WHERE pctest1.c = 42 AND pctest1.k = pctest2.k ORDER BY pctest1.k;
SELECT pctest1.*, pctest2.k FROM pctest1, pctest2
  WHERE pctest1.c = 42 AND pctest1.k = pctest2.k ORDER BY pctest1.k;
reset enable_hashjoin;

-- Subquery
EXPLAIN (costs off)
SELECT x, d FROM
  (SELECT pctest1.* FROM pctest1, pctest2
     WHERE pctest1.k = pctest2.k AND pctest1.c = pctest2.c) ss RIGHT JOIN
  (values (15),(16),(17)) v(x) on ss.b = v.x ORDER BY x;
SELECT x, d FROM
  (SELECT pctest1.* FROM pctest1, pctest2
     WHERE pctest1.k = pctest2.k AND pctest1.c = pctest2.c) ss RIGHT JOIN
  (values (15),(16),(17)) v(x) on ss.b = v.x ORDER BY x;

EXPLAIN (costs off)
SELECT * FROM
  (SELECT pctest1.* FROM pctest1, pctest2
     WHERE pctest1.k = pctest2.k AND pctest1.c = pctest2.c) s1 JOIN
  (SELECT pctest2.* FROM pctest1, pctest2
     WHERE pctest1.k = pctest2.k AND pctest1.b = pctest2.b) s2 ON s1.b = s2.c;
SELECT * FROM
  (SELECT pctest1.* FROM pctest1, pctest2
     WHERE pctest1.k = pctest2.k AND pctest1.c = pctest2.c) s1 JOIN
  (SELECT pctest2.* FROM pctest1, pctest2
     WHERE pctest1.k = pctest2.k AND pctest1.b = pctest2.b) s2 ON s1.b = s2.c;

-- no parallelism
EXPLAIN (costs off)
SELECT * from pctest2
  WHERE b < (SELECT avg(b) / 20 FROM pctest1 WHERE c = pctest2.c);
SELECT * from pctest2
  WHERE b < (SELECT avg(b) / 20 FROM pctest1 WHERE c = pctest2.c);

-- passing parameters to workers
EXPLAIN (costs off)
SELECT * from pctest2
  WHERE c IN (SELECT b FROM pctest1 WHERE d LIKE 'Value_9')
    AND b < (SELECT avg(b) FROM pctest1 WHERE d LIKE 'Value_9');
SELECT * from pctest2
  WHERE c IN (SELECT b FROM pctest1 WHERE d LIKE 'Value_9')
    AND b < (SELECT avg(b) FROM pctest1 WHERE d LIKE 'Value_9');

-- test rescan cases
set enable_material = false;

EXPLAIN (costs off)
select * from
  (SELECT count(*) FROM pctest1 WHERE b > 10) ss
  right join (values (1),(2),(3)) v(x) on true;
select * from
  (SELECT count(*) FROM pctest1 WHERE b > 10) ss
  right join (values (1),(2),(3)) v(x) on true;

EXPLAIN (costs off)
select * from
  (SELECT count(*) FROM pctest1 WHERE c > 10) ss
  right join (values (1),(2),(3)) v(x) on true;
select * from
  (SELECT count(*) FROM pctest1 WHERE c > 10) ss
  right join (values (1),(2),(3)) v(x) on true;

reset enable_material;

-- Modify table (no parallelism)
EXPLAIN (costs off)
UPDATE pctest1 SET b = 0 WHERE d LIKE 'Value_9';
UPDATE pctest1 SET b = 0 WHERE d LIKE 'Value_9';
SELECT count(*) FROM pctest1 WHERE b = 0;

EXPLAIN (costs off)
DELETE FROM pctest1 WHERE d LIKE 'Value_8';
DELETE FROM pctest1 WHERE d LIKE 'Value_8';
SELECT count(*) FROM pctest1;

-- index scan with aggregates pushdown such that #atts being pushed down > #atts in relation
CREATE TABLE pctest3(k int primary key, a int unique) WITH (colocation = true);
INSERT INTO pctest3 SELECT i, i FROM generate_series(1, 1000) i;
EXPLAIN (costs off) SELECT count(*), max(k), min(k) FROM pctest3 WHERE k > 123;
SELECT count(*), max(k), min(k) FROM pctest3 WHERE k > 123;

-- index only scan with aggregates pushdown such that #atts being pushed down > #atts in relation
EXPLAIN (costs off) SELECT count(*), max(a), min(a) FROM pctest3 WHERE a > 123;
SELECT count(*), max(a), min(a) FROM pctest3 WHERE a > 123;

DROP TABLE pctest1;
DROP TABLE pctest2;
DROP TABLE pctest3;
