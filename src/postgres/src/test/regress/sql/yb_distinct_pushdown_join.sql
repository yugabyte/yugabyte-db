SET yb_explain_hide_non_deterministic_fields = true;

-- Split at 1, ... to ensure that the value r1 = 1 is present in more than one tablet.
-- See #18101.
CREATE TABLE t(r1 INT, r2 INT, r3 INT, v INT, PRIMARY KEY(r1 ASC, r2 ASC, r3 ASC)) SPLIT AT VALUES ((1, 1, 500));
INSERT INTO t (SELECT 1, i%3, i, i/3 FROM GENERATE_SERIES(1, 1000) AS i);
-- Add one more distinct value to catch bugs that arise only with more than one distinct value.
INSERT INTO t (SELECT 10, i%3, i, i/3 FROM GENERATE_SERIES(1, 1000) AS i);

-- Start with CROSS/INNER/LEFT/RIGHT/FULL joins.
-- CROSS JOIN
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT t1.r1, t2.r1 FROM t t1 CROSS JOIN t t2;
SELECT DISTINCT t1.r1, t2.r1 FROM t t1 CROSS JOIN t t2;
-- INNER JOIN
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT t1.r1 FROM t t1 INNER JOIN t t2 USING (r1);
SELECT DISTINCT t1.r1 FROM t t1 INNER JOIN t t2 USING (r1);
-- In the Distinct Index Scan of t2, there are 7 rows, not 6, because the tablet split ends up with 1, 1 represented in two tablets.
SET yb_explain_hide_non_deterministic_fields = true;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT t1.r1 FROM t t1 INNER JOIN t t2 ON t1.r1 = t2.r2;
SELECT DISTINCT t1.r1 FROM t t1 INNER JOIN t t2 ON t1.r1 = t2.r2;
-- LEFT JOIN
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT t1.r1 FROM t t1 LEFT JOIN t t2 ON t1.r1 = t2.r2;
SELECT DISTINCT t1.r1 FROM t t1 LEFT JOIN t t2 ON t1.r1 = t2.r2;
-- RIGHT JOIN
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT t1.r1 FROM t t1 RIGHT JOIN t t2 ON t1.r1 = t2.r2;
SELECT DISTINCT t1.r1 FROM t t1 RIGHT JOIN t t2 ON t1.r1 = t2.r2;
-- FULL JOIN
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT t1.r1, t2.r2 FROM t t1 FULL JOIN t t2 ON t1.r1 = t2.r2;
SELECT DISTINCT t1.r1, t2.r2 FROM t t1 FULL JOIN t t2 ON t1.r1 = t2.r2;

-- Now, let's test various join predicate types.
-- Range predicates.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT t1.r1 FROM t t1 JOIN t t2 ON t1.r1 < t2.r2;
SELECT DISTINCT t1.r1 FROM t t1 JOIN t t2 ON t1.r1 < t2.r2;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT t2.r1 FROM t t1 JOIN t t2 ON t1.r1 < t2.r2;
SELECT DISTINCT t2.r1 FROM t t1 JOIN t t2 ON t1.r1 < t2.r2;
-- "DISTINCT" Semijoin. These queries could be optimized by extending our distinctness analysis.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r1 FROM t WHERE r2 IN (SELECT r1 FROM t);
SELECT DISTINCT r1 FROM t WHERE r2 IN (SELECT r1 FROM t);
-- Join clauses have volatile functions. Do not use a Distinct Index Scan in this case.
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT t1.r1 FROM t t1 JOIN t t2 ON t1.r1 + RANDOM() < t2.r1 + RANDOM();
SELECT DISTINCT t1.r1 FROM t t1 JOIN t t2 ON t1.r1 + RANDOM() < t2.r1 + RANDOM();
-- Targets have volatile functions. Do not use a Distinct Index Scan in this case.
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT t1.r1 * RANDOM() FROM t t1 JOIN t t2 USING (r1);

-- Join methods - Merge/Hash/Nestloop.
/*+MergeJoin(t1 t2)*/ EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r1 FROM t t1 JOIN t t2 USING (r1);
/*+MergeJoin(t1 t2)*/ SELECT DISTINCT r1 FROM t t1 JOIN t t2 USING (r1);
/*+HashJoin(t1 t2)*/ EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r1 FROM t t1 JOIN t t2 USING (r1);
/*+HashJoin(t1 t2)*/ SELECT DISTINCT r1 FROM t t1 JOIN t t2 USING (r1);
/*+Nestloop(t1 t2)*/ EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r1 FROM t t1 JOIN t t2 USING (r1);
/*+Nestloop(t1 t2)*/ SELECT DISTINCT r1 FROM t t1 JOIN t t2 USING (r1);

-- Test queries for whether they need a HashAggregate or a Unique node on top of the join plan.
-- Pushdown distinct only into the relation which has no volatile clause.
-- Require additional distinctification on top when distinct is pushed down to only one of the relations.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT t1.r1 FROM t t1 JOIN t t2 ON t1.r1 = t2.r2 WHERE t1.r1 + RANDOM() < 5;
SELECT DISTINCT t1.r1 FROM t t1 JOIN t t2 ON t1.r1 = t2.r2 WHERE t1.r1 + RANDOM() < 5;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT t1.r1 FROM t t1 JOIN t t2 USING (r1) JOIN t t3 USING (r1) WHERE t3.r1 + RANDOM() < 5;
SELECT DISTINCT t1.r1 FROM t t1 JOIN t t2 USING (r1) JOIN t t3 USING (r1) WHERE t3.r1 + RANDOM() < 5;
-- Target list order does not matter.
-- In vanilla postgres, the order of the target list matters, i.e. SELECT DISTINCT t1.r1, t2.r1 does not generate the same plan as SELECT DISTINCT t2.r1, t1.r1.
-- Original order.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT t1.r1, t2.r1 FROM t t1 JOIN t t2 USING (r1);
SELECT DISTINCT t1.r1, t2.r1 FROM t t1 JOIN t t2 USING (r1);
-- Permuted order.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT t2.r1, t1.r1 FROM t t1 JOIN t t2 USING (r1);
SELECT DISTINCT t2.r1, t1.r1 FROM t t1 JOIN t t2 USING (r1);
-- Original order.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT t1.r1, t2.r1, t2.r2 FROM t t1 JOIN t t2 USING (r1);
SELECT DISTINCT t1.r1, t2.r1, t2.r2 FROM t t1 JOIN t t2 USING (r1);
-- Permuted order.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT t2.r2, t1.r1, t2.r1 FROM t t1 JOIN t t2 USING (r1);
SELECT DISTINCT t2.r2, t1.r1, t2.r1 FROM t t1 JOIN t t2 USING (r1);
-- t1.r2 = t2.r2 and t2.r2 = 2 => t1.r2 = 2.
-- Moreover, constants are excluded from the prefix, so r2 is not in either distinct index scan prefix.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT t1.r2 FROM t t1 JOIN t t2 USING (r2) WHERE t2.r2 = 2;
SELECT DISTINCT t1.r2 FROM t t1 JOIN t t2 USING (r2) WHERE t2.r2 = 2;
-- Check constants for distinctness as well.
/*+ Seqscan(t1) */ EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT t1.r1 FROM t t1 JOIN t t2 USING (r1) WHERE t2.r1 = 1;
/*+ Seqscan(t1) */ SELECT DISTINCT t1.r1 FROM t t1 JOIN t t2 USING (r1) WHERE t2.r1 = 1;

-- Try a hash partitioned table now.
CREATE TABLE th(h1 INT, h2 INT, r1 INT, r2 INT, v INT, PRIMARY KEY((h1, h2) HASH, r1 ASC, r2 ASC)) SPLIT INTO 16 TABLETS;
INSERT INTO th (SELECT 1, i%3, 2-i%3, i, i/3 FROM GENERATE_SERIES(1, 1000) AS i);
INSERT INTO th (SELECT 10, i%3, 2-i%3, i, i/3 FROM GENERATE_SERIES(1, 1000) AS i);

-- Try self join on the hash partitioned table.
/*+ MergeJoin(t1 t2) */ EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT h1, h2 FROM th t1 JOIN th t2 USING (h1, h2);
/*+ MergeJoin(t1 t2) */ SELECT DISTINCT h1, h2 FROM th t1 JOIN th t2 USING (h1, h2);
/*+ HashJoin(t1 t2) */ EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT h1, h2 FROM th t1 JOIN th t2 USING (h1, h2);
/*+ HashJoin(t1 t2) */ SELECT DISTINCT h1, h2 FROM th t1 JOIN th t2 USING (h1, h2);
/*+ Nestloop(t1 t2) */ EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT h1, h2 FROM th t1 JOIN th t2 USING (h1, h2);
/*+ Nestloop(t1 t2) */ SELECT DISTINCT h1, h2 FROM th t1 JOIN th t2 USING (h1, h2);

-- Try join across hash and range partitioned tables.
/*+ MergeJoin(th t) */ EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT th.h1, th.h2 FROM th JOIN t ON th.h1 = t.r1 AND th.h2 = t.r2;
/*+ MergeJoin(th t) */ SELECT DISTINCT th.h1, th.h2 FROM th JOIN t ON th.h1 = t.r1 AND th.h2 = t.r2;
/*+ HashJoin(th t) */ EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT th.h1, th.h2 FROM th JOIN t ON th.h1 = t.r1 AND th.h2 = t.r2;
/*+ HashJoin(th t) */ SELECT DISTINCT th.h1, th.h2 FROM th JOIN t ON th.h1 = t.r1 AND th.h2 = t.r2;
/*+ Nestloop(th t) */ EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT th.h1, th.h2 FROM th JOIN t ON th.h1 = t.r1 AND th.h2 = t.r2;
/*+ Nestloop(th t) */ SELECT DISTINCT th.h1, th.h2 FROM th JOIN t ON th.h1 = t.r1 AND th.h2 = t.r2;

DROP TABLE th;

-- Secondary index only scan.
CREATE INDEX irv ON t(v, r1);

EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT v FROM t t1 JOIN t t2 USING (v);

DROP INDEX irv;

DROP TABLE t;

-- Regression test case for GitHub issue #20827

CREATE TABLE t1 (col_int_key int, pad int, primary key(col_int_key asc, pad asc));
INSERT INTO t1 (select 3, i from generate_series(1, 1000) i);
INSERT INTO t1 (select 4, i from generate_series(1, 1000) i);
ANALYZE t1;

CREATE TABLE t2 (pk int, col_int int, primary key(pk asc));
INSERT INTO t2 (SELECT i, i FROM generate_series(1, 1000) i);
ANALYZE t2;

/*+ Set(enable_mergejoin off) Set(enable_hashjoin off) Set(enable_material off) */
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
SELECT DISTINCT t2.pk FROM t1 JOIN t2 ON t1.col_int_key = t2.col_int WHERE t2.pk < 5;

/*+ Set(enable_mergejoin off) Set(enable_hashjoin off) Set(enable_material off) */
SELECT DISTINCT t2.pk FROM t1 JOIN t2 ON t1.col_int_key = t2.col_int WHERE t2.pk < 5;

DROP TABLE t1;
DROP TABLE t2;

-- Regression test for #20893

CREATE TABLE t (k1 INT, k2 INT, PRIMARY KEY(k1 ASC, k2 ASC));

-- Protect against memory corruption bug when not using ANALYZE.
SELECT DISTINCT a1.k1 FROM t a1 RIGHT OUTER JOIN t a2 RIGHT OUTER JOIN t a3
USING (k1) USING (k1) WHERE a2.k1 = 5 AND a3.k1 = 5 GROUP BY a1.k1 ORDER BY a1.k1;

INSERT INTO t (SELECT i%10, i FROM GENERATE_SERIES(1, 10000) AS i);
ANALYZE t;

-- Protect against memory corruption bug when using ANALYZE.
SELECT DISTINCT a1.k1 FROM t a1 RIGHT OUTER JOIN t a2 USING (k1) WHERE a2.k1 = 5 GROUP BY a1.k1 ORDER BY a1.k1;

-- Grouping Sets with distinct.
SELECT DISTINCT t.k1 FROM t GROUP BY GROUPING SETS ((k1), (k1)) ORDER BY k1;

DROP TABLE t;

CREATE TABLE th (h1 INT, r1 INT, PRIMARY KEY(h1 HASH, r1 ASC));

-- Protect against memory corruption bug when using hash partitioned table.
SELECT DISTINCT a1.h1 FROM th a1, th a2, th a3 where a1.h1 < a2.h1 and a2.h1 = a3.h1 and a2.h1 = 5 AND a3.h1 = 5 GROUP BY a1.h1;

DROP TABLE th;
