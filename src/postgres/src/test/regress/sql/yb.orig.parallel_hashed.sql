--
-- PARALLEL queries to hash sharded tables
--

-- create tables
CREATE TABLE pctest1(k int, a int, b int, c int, d text, primary key(k hash));
CREATE TABLE pctest2(k int, a int, b int, c int, d text, primary key(k hash));
CREATE UNIQUE INDEX ON pctest1(a hash);
CREATE INDEX ON pctest1(c hash);
CREATE INDEX ON pctest2(b hash);
INSERT INTO pctest1
    SELECT i, 1000 - i, i/3, i%50, 'Value' || i::text FROM generate_series(1, 1000) i;
INSERT INTO pctest2
    SELECT i, 200 + i, i/5, i%10, 'Other value ' || i::text FROM generate_series(1, 200) i;
ANALYZE pctest1, pctest2;

set yb_enable_parallel_scan_hash_sharded to true;
-- set smaller parallel interval to produce more ranges
set yb_parallel_range_size to 1024;

-- enable parallel query for YB tables
set yb_parallel_range_rows to 1;
set yb_enable_cbo = on;

-- parallel bitmap scan not supported yet
set enable_bitmapscan = false;

-- Parallel sequential scan
/*+ Parallel(pctest1 2 hard) */
EXPLAIN (costs off)
SELECT * FROM pctest1 WHERE d LIKE 'Value_9';
/*+ Parallel(pctest1 2 hard) */
SELECT * FROM pctest1 WHERE d LIKE 'Value_9';

-- with aggregates
/*+ Parallel(pctest1 2 hard) */
EXPLAIN (costs off)
SELECT count(*) FROM pctest1 WHERE d LIKE 'Value_9';
/*+ Parallel(pctest1 2 hard) */
SELECT count(*) FROM pctest1 WHERE d LIKE 'Value_9';

-- with sort
/*+ Parallel(pctest1 2 hard) */
EXPLAIN (costs off)
SELECT * FROM pctest1 WHERE d LIKE 'Value_9' ORDER BY b DESC;
/*+ Parallel(pctest1 2 hard) */
SELECT * FROM pctest1 WHERE d LIKE 'Value_9' ORDER BY b DESC;

-- with grouping
/*+ Parallel(pctest1 2 hard) */
EXPLAIN (costs off)
SELECT b, count(*) FROM pctest1 WHERE d LIKE 'Value9%' GROUP BY b;
/*+ Parallel(pctest1 2 hard) */
SELECT b, count(*) FROM pctest1 WHERE d LIKE 'Value9%' GROUP BY b;

-- Parallel index scan
--secondary index
/*+ Parallel(pctest1 2 hard) */
EXPLAIN (costs off)
SELECT * FROM pctest1 WHERE c = 10;
/*+ Parallel(pctest1 2 hard) */
SELECT * FROM pctest1 WHERE c = 10;

-- with aggregates
/*+ Parallel(pctest1 2 hard) */
EXPLAIN (costs off)
SELECT count(*) FROM pctest1 WHERE k > 123;
/*+ Parallel(pctest1 2 hard) */
SELECT count(*) FROM pctest1 WHERE k > 123;

-- index only
/*+ Parallel(pctest1 2 hard) */
EXPLAIN (costs off)
SELECT a FROM pctest1 WHERE a < 10;
/*+ Parallel(pctest1 2 hard) */
SELECT a FROM pctest1 WHERE a < 10;

-- with grouping
/*+ Parallel(pctest1 2 hard) */
EXPLAIN (costs off)
SELECT c, count(*) FROM pctest1 WHERE c > 40 GROUP BY c;
/*+ Parallel(pctest1 2 hard) */
SELECT c, count(*) FROM pctest1 WHERE c > 40 GROUP BY c;

-- Subquery
/*+
  Parallel(pctest1 2 hard) Parallel(pctest2 2 hard)
  Parallel(pctest3 2 hard) Parallel(pctest4 2 hard)
 */
EXPLAIN (costs off)
SELECT * FROM
  (SELECT pctest1.* FROM pctest1, pctest2
     WHERE pctest1.k = pctest2.k AND pctest1.c = pctest2.c) s1 JOIN
  (SELECT pctest4.* FROM pctest1 pctest3, pctest2 pctest4
     WHERE pctest3.k = pctest4.k AND pctest3.b = pctest4.b) s2 ON s1.b = s2.c
ORDER BY s1.k;
/*+
  Parallel(pctest1 2 hard) Parallel(pctest2 2 hard)
  Parallel(pctest3 2 hard) Parallel(pctest4 2 hard)
 */
SELECT * FROM
  (SELECT pctest1.* FROM pctest1, pctest2
     WHERE pctest1.k = pctest2.k AND pctest1.c = pctest2.c) s1 JOIN
  (SELECT pctest4.* FROM pctest1 pctest3, pctest2 pctest4
     WHERE pctest3.k = pctest4.k AND pctest3.b = pctest4.b) s2 ON s1.b = s2.c
ORDER BY s1.k;

-- no parallelism
EXPLAIN (costs off)
SELECT * from pctest2
  WHERE b < (SELECT avg(b) / 20 FROM pctest1 WHERE c = pctest2.c);
SELECT * from pctest2
  WHERE b < (SELECT avg(b) / 20 FROM pctest1 WHERE c = pctest2.c);

-- passing parameters to workers
/*+ Parallel(pctest1 2 hard) Parallel(pctest1_1 2 hard) Parallel(pctest2 2 hard) */
EXPLAIN (costs off)
SELECT * from pctest2
  WHERE c IN (SELECT b FROM pctest1 WHERE d LIKE 'Value_9')
    AND b < (SELECT avg(b) FROM pctest1 WHERE d LIKE 'Value_9');
/*+ Parallel(pctest1 2 hard) Parallel(pctest1_1 2 hard) Parallel(pctest2 2 hard) */
SELECT * from pctest2
  WHERE c IN (SELECT b FROM pctest1 WHERE d LIKE 'Value_9')
    AND b < (SELECT avg(b) FROM pctest1 WHERE d LIKE 'Value_9');

-- test rescan cases
set enable_material = false;

/*+ Parallel(pctest1 2 hard) */
EXPLAIN (costs off)
select * from
  (SELECT count(*) FROM pctest1 WHERE b > 10) ss
  right join (values (1),(2),(3)) v(x) on true;
select * from
  (SELECT count(*) FROM pctest1 WHERE b > 10) ss
  right join (values (1),(2),(3)) v(x) on true;

/*+ Parallel(pctest1 2 hard) */
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
CREATE TABLE pctest3(k int primary key, a int unique);
INSERT INTO pctest3 SELECT i, i FROM generate_series(1, 1000) i;
ANALYZE pctest3;
/*+ Parallel(pctest3 2 hard) */
EXPLAIN (costs off) SELECT count(*), max(k), min(k) FROM pctest3 WHERE k > 123;
/*+ Parallel(pctest3 2 hard) */
SELECT count(*), max(k), min(k) FROM pctest3 WHERE k > 123;

-- Parallel index scan
/*+ Parallel(pctest1 2 hard) */
EXPLAIN (costs off)
SELECT * FROM pctest1 WHERE k < 10;
/*+ Parallel(pctest1 2 hard) */
SELECT * FROM pctest1 WHERE k < 10;

/*+ Parallel(pctest1 2 hard) */
EXPLAIN (costs off)
SELECT * FROM pctest1 WHERE d LIKE 'Value_9' ORDER BY k;
/*+ Parallel(pctest1 2 hard) */
SELECT * FROM pctest1 WHERE d LIKE 'Value_9' ORDER BY k;

--secondary index
/*+ Parallel(pctest1 2 hard) */
EXPLAIN (costs off)
SELECT * FROM pctest1 ORDER BY a LIMIT 10;
/*+ Parallel(pctest1 2 hard) */
SELECT * FROM pctest1 ORDER BY a LIMIT 10;

-- Joins
-- Nest loop
/*+
  Parallel(pctest1 2 hard) Parallel(pctest2 2 hard)
  Set(enable_mergejoin off) Set(enable_hashjoin off)
  Set(yb_bnl_batch_size 1) Set(enable_material off)
*/
EXPLAIN (costs off)
SELECT pctest1.* FROM pctest1, pctest2
  WHERE pctest1.a = pctest2.b and pctest1.a % 10 = 0;
-- TODO row order varies between the runs
-- /*+
--   Set(enable_mergejoin off) Set(enable_hashjoin off)
--   Set(yb_bnl_batch_size 1) Set(enable_material off)
-- */
-- SELECT pctest1.* FROM pctest1, pctest2
--  WHERE pctest1.a = pctest2.b and pctest1.a % 10 = 0;
EXPLAIN (costs off)
/*+YbBatchedNL(pctest1 pctest2) Parallel(pctest1 2 hard) Parallel(pctest2 2 hard) */
SELECT pctest1.*, pctest2.k FROM pctest1, pctest2
  WHERE pctest1.c = 42 AND pctest1.k = pctest2.k ORDER BY pctest1.k;
/*+YbBatchedNL(pctest1 pctest2) Parallel(pctest1 2 hard) Parallel(pctest2 2 hard) */
SELECT pctest1.*, pctest2.k FROM pctest1, pctest2
  WHERE pctest1.c = 42 AND pctest1.k = pctest2.k ORDER BY pctest1.k;

-- Hash join
set enable_mergejoin to false;
/*+ Parallel(pctest1 2 hard) Parallel(pctest2 2 hard) */
EXPLAIN (costs off)
SELECT pctest1.k, pctest2.k FROM pctest1 JOIN pctest2 USING (a, c) ORDER BY pctest1.k, pctest2.k;
/*+ Parallel(pctest1 2 hard) Parallel(pctest2 2 hard) */
SELECT pctest1.k, pctest2.k FROM pctest1 JOIN pctest2 USING (a, c) ORDER BY pctest1.k, pctest2.k;
reset enable_mergejoin;

-- Merge join
set enable_hashjoin to false;
/*+ Parallel(pctest1 2 hard) Parallel(pctest2 2 hard) */
EXPLAIN (costs off)
SELECT pctest1.*, pctest2.k FROM pctest1, pctest2
  WHERE pctest1.c = 42 AND pctest1.k = pctest2.k ORDER BY pctest1.k;
/*+ Parallel(pctest1 2 hard) Parallel(pctest2 2 hard) */
SELECT pctest1.*, pctest2.k FROM pctest1, pctest2
  WHERE pctest1.c = 42 AND pctest1.k = pctest2.k ORDER BY pctest1.k;
reset enable_hashjoin;

-- Subquery
/*+ Parallel(pctest1 2 hard) Parallel(pctest2 2 hard) */
EXPLAIN (costs off)
SELECT x, d FROM
  (SELECT pctest1.* FROM pctest1, pctest2
     WHERE pctest1.k = pctest2.k AND pctest1.c = pctest2.c) ss RIGHT JOIN
  (values (15),(16),(17)) v(x) on ss.b = v.x ORDER BY x;
/*+ Parallel(pctest1 2 hard) Parallel(pctest2 2 hard) */
SELECT x, d FROM
  (SELECT pctest1.* FROM pctest1, pctest2
     WHERE pctest1.k = pctest2.k AND pctest1.c = pctest2.c) ss RIGHT JOIN
  (values (15),(16),(17)) v(x) on ss.b = v.x ORDER BY x;

/*+ Parallel(pctest1 2 hard) Parallel(pctest2 2 hard) */
EXPLAIN (costs off)
SELECT * FROM
  (SELECT pctest1.* FROM pctest1, pctest2
     WHERE pctest1.k = pctest2.k AND pctest1.c = pctest2.c) s1 JOIN
  (SELECT pctest2.* FROM pctest1, pctest2
     WHERE pctest1.k = pctest2.k AND pctest1.b = pctest2.b) s2 ON s1.b = s2.c;
/*+ Parallel(pctest1 2 hard) Parallel(pctest2 2 hard) */
SELECT * FROM
  (SELECT pctest1.* FROM pctest1, pctest2
     WHERE pctest1.k = pctest2.k AND pctest1.c = pctest2.c) s1 JOIN
  (SELECT pctest2.* FROM pctest1, pctest2
     WHERE pctest1.k = pctest2.k AND pctest1.b = pctest2.b) s2 ON s1.b = s2.c;

-- index only scan with aggregates pushdown such that #atts being pushed down > #atts in relation
/*+ Parallel(pctest3 3 hard) */
EXPLAIN (costs off) SELECT count(*), max(a), min(a) FROM pctest3 WHERE a > 123;
/*+ Parallel(pctest3 3 hard) */
SELECT count(*), max(a), min(a) FROM pctest3 WHERE a > 123;

DROP TABLE pctest1;
DROP TABLE pctest2;
DROP TABLE pctest3;
