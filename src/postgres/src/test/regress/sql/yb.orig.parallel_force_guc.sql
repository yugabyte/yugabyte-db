--
-- Planner's plan choices with yb_test_force_parallel = 'force'
--

CREATE DATABASE pctest colocation = true;
\c pctest

set yb_enable_cbo = on;
set yb_test_force_parallel = force;

-- set smaller parallel interval to produce more ranges
set yb_parallel_range_size to 1024;

-- enable parallel query for YB tables
set yb_parallel_range_rows to 1;

-- parallel bitmap scan not supported yet
set enable_bitmapscan = false;

---
--- colocated tables
---
set yb_enable_parallel_scan_colocated = on;

CREATE TABLE pctest1(k int primary key, a int, b int, c int, d text)
    WITH (colocation = true);
CREATE TABLE pctest2(k int primary key, a int, b int, c int, d text)
    WITH (colocation = true);
CREATE UNIQUE INDEX ON pctest1(a);
CREATE INDEX ON pctest1(c);
CREATE INDEX ON pctest2(b);
INSERT INTO pctest1
    SELECT i, 400 - i, i/3, i%50, 'Value' || i::text FROM generate_series(1, 400) i;
INSERT INTO pctest2
    SELECT i, 100 + i, i/5, i%10, 'Other value ' || i::text FROM generate_series(1, 100) i;
ANALYZE pctest1, pctest2;

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
/*+
  Set(enable_mergejoin off) Set(enable_hashjoin off)
  Set(yb_bnl_batch_size 1) Set(enable_material off)
*/
EXPLAIN (costs off)
SELECT pctest1.* FROM pctest1, pctest2
  WHERE pctest1.a = pctest2.b and pctest1.a % 10 = 0;
/*+
  Set(enable_mergejoin off) Set(enable_hashjoin off)
  Set(yb_bnl_batch_size 1) Set(enable_material off)
*/
SELECT pctest1.* FROM pctest1, pctest2
  WHERE pctest1.a = pctest2.b and pctest1.a % 10 = 0;
EXPLAIN (costs off)
/*+YbBatchedNL(pctest1 pctest2)*/
SELECT pctest1.*, pctest2.k FROM pctest1, pctest2
  WHERE pctest1.c = 42 AND pctest1.k = pctest2.k ORDER BY pctest1.k;
/*+YbBatchedNL(pctest1 pctest2)*/
SELECT pctest1.*, pctest2.k FROM pctest1, pctest2
  WHERE pctest1.c = 42 AND pctest1.k = pctest2.k ORDER BY pctest1.k;

-- Hash join
set enable_mergejoin to false;
EXPLAIN (costs off)
SELECT pctest1.k, pctest2.k FROM pctest1 JOIN pctest2 USING (a, c)
  ORDER BY pctest1.k, pctest2.k;
SELECT pctest1.k, pctest2.k FROM pctest1 JOIN pctest2 USING (a, c)
  ORDER BY pctest1.k, pctest2.k;
reset enable_mergejoin;

-- Merge join
set enable_hashjoin to false;
set yb_bnl_batch_size = 1;
EXPLAIN (costs off)
SELECT pctest1.*, pctest2.k FROM pctest1, pctest2
  WHERE pctest1.c = 42 AND pctest1.k = pctest2.k ORDER BY pctest1.k;
SELECT pctest1.*, pctest2.k FROM pctest1, pctest2
  WHERE pctest1.c = 42 AND pctest1.k = pctest2.k ORDER BY pctest1.k;
reset enable_hashjoin;
reset yb_bnl_batch_size;

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
  (SELECT pctest4.* FROM pctest1 pctest3, pctest2 pctest4
     WHERE pctest3.k = pctest4.k AND pctest3.b = pctest4.b) s2 ON s1.b = s2.c;
SELECT * FROM
  (SELECT pctest1.* FROM pctest1, pctest2
     WHERE pctest1.k = pctest2.k AND pctest1.c = pctest2.c) s1 JOIN
  (SELECT pctest4.* FROM pctest1 pctest3, pctest2 pctest4
     WHERE pctest3.k = pctest4.k AND pctest3.b = pctest4.b) s2 ON s1.b = s2.c;

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
ANALYZE pctest3;
EXPLAIN (costs off) SELECT count(*), max(k), min(k) FROM pctest3 WHERE k > 123;
SELECT count(*), max(k), min(k) FROM pctest3 WHERE k > 123;


---
--- hash sharded tables
---
set yb_enable_parallel_scan_hash_sharded = on;

CREATE TABLE pctest_h1(k int, a int, b int, c int, d text, primary key(k hash))
    WITH (colocation = false);
CREATE TABLE pctest_h2(k int, a int, b int, c int, d text, primary key(k hash))
    WITH (colocation = false);
CREATE UNIQUE INDEX ON pctest_h1(a hash);
CREATE INDEX ON pctest_h1(c hash) split into 3 tablets;
CREATE INDEX ON pctest_h2(b hash) split into 3 tablets;
INSERT INTO pctest_h1
    SELECT i, 400 - i, i/3, i%50, 'Value' || i::text FROM generate_series(1, 400) i;
INSERT INTO pctest_h2
    SELECT i, 100 + i, i/5, i%10, 'Other value ' || i::text FROM generate_series(1, 100) i;
ANALYZE pctest_h1, pctest_h2;

-- Parallel sequential scan
EXPLAIN (costs off)
SELECT * FROM pctest_h1 WHERE d LIKE 'Value_9';
SELECT * FROM pctest_h1 WHERE d LIKE 'Value_9';

-- with aggregates
EXPLAIN (costs off)
SELECT count(*) FROM pctest_h1 WHERE d LIKE 'Value_9';
SELECT count(*) FROM pctest_h1 WHERE d LIKE 'Value_9';

-- with sort
EXPLAIN (costs off)
SELECT * FROM pctest_h1 WHERE d LIKE 'Value_9' ORDER BY b DESC;
SELECT * FROM pctest_h1 WHERE d LIKE 'Value_9' ORDER BY b DESC;

-- with grouping
EXPLAIN (costs off)
SELECT b, count(*) FROM pctest_h1 WHERE d LIKE 'Value9%' GROUP BY b;
SELECT b, count(*) FROM pctest_h1 WHERE d LIKE 'Value9%' GROUP BY b;

-- Parallel index scan
--secondary index
EXPLAIN (costs off)
SELECT * FROM pctest_h1 WHERE c = 10;
SELECT * FROM pctest_h1 WHERE c = 10;

-- with aggregates
EXPLAIN (costs off)
SELECT count(*) FROM pctest_h1 WHERE k > 123;
SELECT count(*) FROM pctest_h1 WHERE k > 123;

-- index only
EXPLAIN (costs off)
SELECT a FROM pctest_h1 WHERE a < 10;
SELECT a FROM pctest_h1 WHERE a < 10;

-- with grouping
EXPLAIN (costs off)
SELECT c, count(*) FROM pctest_h1 WHERE c > 40 GROUP BY c;
SELECT c, count(*) FROM pctest_h1 WHERE c > 40 GROUP BY c;

-- Subquery
EXPLAIN (costs off)
SELECT * FROM
  (SELECT pctest_h1.* FROM pctest_h1, pctest_h2
     WHERE pctest_h1.k = pctest_h2.k AND pctest_h1.c = pctest_h2.c) s1 JOIN
  (SELECT pctest_h4.* FROM pctest_h1 pctest_h3, pctest_h2 pctest_h4
     WHERE pctest_h3.k = pctest_h4.k AND pctest_h3.b = pctest_h4.b) s2 ON s1.b = s2.c
ORDER BY s1.k;
SELECT * FROM
  (SELECT pctest_h1.* FROM pctest_h1, pctest_h2
     WHERE pctest_h1.k = pctest_h2.k AND pctest_h1.c = pctest_h2.c) s1 JOIN
  (SELECT pctest_h4.* FROM pctest_h1 pctest_h3, pctest_h2 pctest_h4
     WHERE pctest_h3.k = pctest_h4.k AND pctest_h3.b = pctest_h4.b) s2 ON s1.b = s2.c
ORDER BY s1.k;

-- no parallelism
EXPLAIN (costs off)
SELECT * from pctest_h2
  WHERE b < (SELECT avg(b) / 20 FROM pctest_h1 WHERE c = pctest_h2.c);
SELECT * from pctest_h2
  WHERE b < (SELECT avg(b) / 20 FROM pctest_h1 WHERE c = pctest_h2.c);

-- passing parameters to workers
EXPLAIN (costs off)
SELECT * from pctest_h2
  WHERE c IN (SELECT b FROM pctest_h1 WHERE d LIKE 'Value_9')
    AND b < (SELECT avg(b) FROM pctest_h1 WHERE d LIKE 'Value_9');
SELECT * from pctest_h2
  WHERE c IN (SELECT b FROM pctest_h1 WHERE d LIKE 'Value_9')
    AND b < (SELECT avg(b) FROM pctest_h1 WHERE d LIKE 'Value_9');

-- test rescan cases
set enable_material = false;

EXPLAIN (costs off)
select * from
  (SELECT count(*) FROM pctest_h1 WHERE b > 10) ss
  right join (values (1),(2),(3)) v(x) on true;
select * from
  (SELECT count(*) FROM pctest_h1 WHERE b > 10) ss
  right join (values (1),(2),(3)) v(x) on true;

EXPLAIN (costs off)
select * from
  (SELECT count(*) FROM pctest_h1 WHERE c > 10) ss
  right join (values (1),(2),(3)) v(x) on true;
select * from
  (SELECT count(*) FROM pctest_h1 WHERE c > 10) ss
  right join (values (1),(2),(3)) v(x) on true;

reset enable_material;

-- Modify table (no parallelism)
EXPLAIN (costs off)
UPDATE pctest_h1 SET b = 0 WHERE d LIKE 'Value_9';
UPDATE pctest_h1 SET b = 0 WHERE d LIKE 'Value_9';
SELECT count(*) FROM pctest_h1 WHERE b = 0;

EXPLAIN (costs off)
DELETE FROM pctest_h1 WHERE d LIKE 'Value_8';
DELETE FROM pctest_h1 WHERE d LIKE 'Value_8';
SELECT count(*) FROM pctest_h1;

-- index scan with aggregates pushdown such that #atts being pushed down > #atts in relation
CREATE TABLE pctest_h3(k int primary key, a int unique);
INSERT INTO pctest_h3 SELECT i, i FROM generate_series(1, 1000) i;
ANALYZE pctest_h3;
EXPLAIN (costs off) SELECT count(*), max(k), min(k) FROM pctest_h3 WHERE k > 123;
SELECT count(*), max(k), min(k) FROM pctest_h3 WHERE k > 123;

-- Parallel index scan
EXPLAIN (costs off)
SELECT * FROM pctest_h1 WHERE k < 10;
SELECT * FROM pctest_h1 WHERE k < 10;

EXPLAIN (costs off)
SELECT * FROM pctest_h1 WHERE d LIKE 'Value_9' ORDER BY k;
SELECT * FROM pctest_h1 WHERE d LIKE 'Value_9' ORDER BY k;

--secondary index
EXPLAIN (costs off)
SELECT * FROM pctest_h1 ORDER BY a LIMIT 10;
SELECT * FROM pctest_h1 ORDER BY a LIMIT 10;

-- Joins
-- Nest loop
/*+
  Set(enable_mergejoin off) Set(enable_hashjoin off)
  Set(yb_bnl_batch_size 1) Set(enable_material off)
*/
EXPLAIN (costs off)
SELECT pctest_h1.* FROM pctest_h1, pctest_h2
  WHERE pctest_h1.a = pctest_h2.b and pctest_h1.a % 10 = 0;
-- TODO row order varies between runs (parallel workers on hash-sharded
-- outer interleave).
-- /*+
--   Set(enable_mergejoin off) Set(enable_hashjoin off)
--   Set(yb_bnl_batch_size 1) Set(enable_material off)
-- */
-- SELECT pctest_h1.* FROM pctest_h1, pctest_h2
--  WHERE pctest_h1.a = pctest_h2.b and pctest_h1.a % 10 = 0;
EXPLAIN (costs off)
/*+YbBatchedNL(pctest_h1 pctest_h2)*/
SELECT pctest_h1.*, pctest_h2.k FROM pctest_h1, pctest_h2
  WHERE pctest_h1.c = 42 AND pctest_h1.k = pctest_h2.k ORDER BY pctest_h1.k;
/*+YbBatchedNL(pctest_h1 pctest_h2)*/
SELECT pctest_h1.*, pctest_h2.k FROM pctest_h1, pctest_h2
  WHERE pctest_h1.c = 42 AND pctest_h1.k = pctest_h2.k ORDER BY pctest_h1.k;

-- Hash join
set enable_mergejoin to false;
EXPLAIN (costs off)
SELECT pctest_h1.k, pctest_h2.k FROM pctest_h1 JOIN pctest_h2 USING (a, c) ORDER BY pctest_h1.k, pctest_h2.k;
SELECT pctest_h1.k, pctest_h2.k FROM pctest_h1 JOIN pctest_h2 USING (a, c) ORDER BY pctest_h1.k, pctest_h2.k;
reset enable_mergejoin;

-- Merge join
set enable_hashjoin to false;
set yb_bnl_batch_size = 1;
EXPLAIN (costs off)
SELECT pctest_h1.*, pctest_h2.k FROM pctest_h1, pctest_h2
  WHERE pctest_h1.c = 42 AND pctest_h1.k = pctest_h2.k ORDER BY pctest_h1.k;
SELECT pctest_h1.*, pctest_h2.k FROM pctest_h1, pctest_h2
  WHERE pctest_h1.c = 42 AND pctest_h1.k = pctest_h2.k ORDER BY pctest_h1.k;
reset enable_hashjoin;
reset yb_bnl_batch_size;

-- Subquery
EXPLAIN (costs off)
SELECT x, d FROM
  (SELECT pctest_h1.* FROM pctest_h1, pctest_h2
     WHERE pctest_h1.k = pctest_h2.k AND pctest_h1.c = pctest_h2.c) ss RIGHT JOIN
  (values (15),(16),(17)) v(x) on ss.b = v.x ORDER BY x;
SELECT x, d FROM
  (SELECT pctest_h1.* FROM pctest_h1, pctest_h2
     WHERE pctest_h1.k = pctest_h2.k AND pctest_h1.c = pctest_h2.c) ss RIGHT JOIN
  (values (15),(16),(17)) v(x) on ss.b = v.x ORDER BY x;

EXPLAIN (costs off)
SELECT * FROM
  (SELECT pctest_h1.* FROM pctest_h1, pctest_h2
     WHERE pctest_h1.k = pctest_h2.k AND pctest_h1.c = pctest_h2.c) s1 JOIN
  (SELECT pctest_h2.* FROM pctest_h1, pctest_h2
     WHERE pctest_h1.k = pctest_h2.k AND pctest_h1.b = pctest_h2.b) s2 ON s1.b = s2.c;
SELECT * FROM
  (SELECT pctest_h1.* FROM pctest_h1, pctest_h2
     WHERE pctest_h1.k = pctest_h2.k AND pctest_h1.c = pctest_h2.c) s1 JOIN
  (SELECT pctest_h2.* FROM pctest_h1, pctest_h2
     WHERE pctest_h1.k = pctest_h2.k AND pctest_h1.b = pctest_h2.b) s2 ON s1.b = s2.c;

-- index only scan with aggregates pushdown such that #atts being pushed down > #atts in relation
EXPLAIN (costs off) SELECT count(*), max(a), min(a) FROM pctest_h3 WHERE a > 123;
SELECT count(*), max(a), min(a) FROM pctest_h3 WHERE a > 123;

-- conditions on yb_hash_code()
EXPLAIN (costs off) SELECT * FROM pctest_h1 WHERE yb_hash_code(k) >= 512 AND yb_hash_code(k) < 1024;
SELECT * FROM pctest_h1 WHERE yb_hash_code(k) >= 512 AND yb_hash_code(k) < 1024;
EXPLAIN (costs off) SELECT * FROM pctest_h1 WHERE yb_hash_code(c) >= 1024 AND yb_hash_code(c) < 2048;
SELECT * FROM pctest_h1 WHERE yb_hash_code(c) >= 1024 AND yb_hash_code(c) < 2048;

DROP TABLE pctest1;
DROP TABLE pctest2;
DROP TABLE pctest3;
DROP TABLE pctest_h1;
DROP TABLE pctest_h2;
DROP TABLE pctest_h3;
