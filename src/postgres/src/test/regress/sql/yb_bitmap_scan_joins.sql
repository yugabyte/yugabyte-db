--
-- Joins with YB Bitmap Scans (bitmap index scans + YB bitmap table scans)
--
SET yb_explain_hide_non_deterministic_fields = true;
SET enable_bitmapscan = true;
SET yb_prefer_bnl = false;

CREATE TABLE joina (k INT, a INT, b INT, PRIMARY KEY (k ASC));
CREATE INDEX ON joina (a ASC);
CREATE INDEX ON joina (b ASC);

CREATE TABLE joinb (k INT, c INT, d INT, PRIMARY KEY (k ASC));
CREATE INDEX ON joinb (c ASC);
CREATE INDEX ON joinb (d ASC);

INSERT INTO joina SELECT i, i * 2, i * 3 FROM generate_series(1, 10) i;
INSERT INTO joinb SELECT i, i * 2, i * 3 FROM generate_series(1, 10) i;

--
-- Test Bitmap Scan as Outer Join table --
--

-- join PK to PK
/*+ NestLoop(joina joinb) Leading(joina joinb) BitmapScan(joina) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joina JOIN joinb ON joina.k = joinb.k WHERE joina.a < 10 OR joina.b < 15 ORDER BY joina.a;
/*+ NestLoop(joina joinb) Leading(joina joinb) BitmapScan(joina) */
SELECT * FROM joina JOIN joinb ON joina.k = joinb.k WHERE joina.a < 10 OR joina.b < 15 ORDER BY joina.a;
/*+ YbBatchedNL(joina joinb) Leading(joina joinb) BitmapScan(joina) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joina JOIN joinb ON joina.k = joinb.k WHERE joina.a < 10 OR joina.b < 15 ORDER BY joina.a;
/*+ YbBatchedNL(joina joinb) Leading(joina joinb) BitmapScan(joina) */
SELECT * FROM joina JOIN joinb ON joina.k = joinb.k WHERE joina.a < 10 OR joina.b < 15 ORDER BY joina.a;

-- join index col to PK
/*+ NestLoop(joina joinb) Leading(joina joinb) BitmapScan(joina) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joina JOIN joinb ON joina.a = joinb.k WHERE joina.a < 10 OR joina.b < 15 ORDER BY joina.a;
/*+ NestLoop(joina joinb) Leading(joina joinb) BitmapScan(joina) */
SELECT * FROM joina JOIN joinb ON joina.a = joinb.k WHERE joina.a < 10 OR joina.b < 15 ORDER BY joina.a;
/*+ YbBatchedNL(joina joinb) Leading(joina joinb) BitmapScan(joina) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joina JOIN joinb ON joina.a = joinb.k WHERE joina.a < 10 OR joina.b < 15 ORDER BY joina.a;
/*+ YbBatchedNL(joina joinb) Leading(joina joinb) BitmapScan(joina) */
SELECT * FROM joina JOIN joinb ON joina.a = joinb.k WHERE joina.a < 10 OR joina.b < 15 ORDER BY joina.a;

-- join PK to index col
/*+ NestLoop(joina joinb) Leading(joina joinb) BitmapScan(joina) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joina JOIN joinb ON joina.k = joinb.c WHERE joina.a < 10 OR joina.b < 15 ORDER BY joina.a;
/*+ NestLoop(joina joinb) Leading(joina joinb) BitmapScan(joina) */
SELECT * FROM joina JOIN joinb ON joina.k = joinb.c WHERE joina.a < 10 OR joina.b < 15 ORDER BY joina.a;
/*+ YbBatchedNL(joina joinb) Leading(joina joinb) BitmapScan(joina) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joina JOIN joinb ON joina.k = joinb.c WHERE joina.a < 10 OR joina.b < 15 ORDER BY joina.a;
/*+ YbBatchedNL(joina joinb) Leading(joina joinb) BitmapScan(joina) */
SELECT * FROM joina JOIN joinb ON joina.k = joinb.c WHERE joina.a < 10 OR joina.b < 15 ORDER BY joina.a;

-- join index col to index col
/*+ NestLoop(joina joinb) Leading(joina joinb) BitmapScan(joina) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joina JOIN joinb ON joina.a = joinb.c WHERE joina.a < 10 OR joina.b < 15 ORDER BY joina.a;
/*+ NestLoop(joina joinb) Leading(joina joinb) BitmapScan(joina) */
SELECT * FROM joina JOIN joinb ON joina.a = joinb.c WHERE joina.a < 10 OR joina.b < 15 ORDER BY joina.a;
/*+ YbBatchedNL(joina joinb) Leading(joina joinb) BitmapScan(joina) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joina JOIN joinb ON joina.a = joinb.c WHERE joina.a < 10 OR joina.b < 15 ORDER BY joina.a;
/*+ YbBatchedNL(joina joinb) Leading(joina joinb) BitmapScan(joina) */
SELECT * FROM joina JOIN joinb ON joina.a = joinb.c WHERE joina.a < 10 OR joina.b < 15 ORDER BY joina.a;

--
-- Test Bitmap Scan as Inner Join table --
--

-- join PK to PK
/*+ NestLoop(joina joinb) Leading(joina joinb) BitmapScan(joinb) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joina JOIN joinb ON joina.k = joinb.k WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ NestLoop(joina joinb) Leading(joina joinb) BitmapScan(joinb) */
SELECT * FROM joina JOIN joinb ON joina.k = joinb.k WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ MergeJoin(joina joinb) Leading(joina joinb) BitmapScan(joinb) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joina JOIN joinb ON joina.k = joinb.k WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ MergeJoin(joina joinb) Leading(joina joinb) BitmapScan(joinb) */
SELECT * FROM joina JOIN joinb ON joina.k = joinb.k WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ HashJoin(joina joinb) Leading(joina joinb) BitmapScan(joinb) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joina JOIN joinb ON joina.k = joinb.k WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ HashJoin(joina joinb) Leading(joina joinb) BitmapScan(joinb) */
SELECT * FROM joina JOIN joinb ON joina.k = joinb.k WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ YbBatchedNL(joina joinb) Leading(joina joinb) BitmapScan(joinb) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joina JOIN joinb ON joina.k = joinb.k WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ YbBatchedNL(joina joinb) Leading(joina joinb) BitmapScan(joinb) */
SELECT * FROM joina JOIN joinb ON joina.k = joinb.k WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;

-- join index col to PK
/*+ NestLoop(joina joinb) Leading(joina joinb) BitmapScan(joinb) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joina JOIN joinb ON joina.a = joinb.k WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ NestLoop(joina joinb) Leading(joina joinb) BitmapScan(joinb) */
SELECT * FROM joina JOIN joinb ON joina.a = joinb.k WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ MergeJoin(joina joinb) Leading(joina joinb) BitmapScan(joinb) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joina JOIN joinb ON joina.a = joinb.k WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ MergeJoin(joina joinb) Leading(joina joinb) BitmapScan(joinb) */
SELECT * FROM joina JOIN joinb ON joina.a = joinb.k WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ HashJoin(joina joinb) Leading(joina joinb) BitmapScan(joinb) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joina JOIN joinb ON joina.a = joinb.k WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ HashJoin(joina joinb) Leading(joina joinb) BitmapScan(joinb) */
SELECT * FROM joina JOIN joinb ON joina.a = joinb.k WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ YbBatchedNL(joina joinb) Leading(joina joinb) BitmapScan(joinb) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joina JOIN joinb ON joina.a = joinb.k WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ YbBatchedNL(joina joinb) Leading(joina joinb) BitmapScan(joinb) */
SELECT * FROM joina JOIN joinb ON joina.a = joinb.k WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;

-- join PK to index col
/*+ NestLoop(joina joinb) Leading(joina joinb) BitmapScan(joinb) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joina JOIN joinb ON joina.k = joinb.c WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ NestLoop(joina joinb) Leading(joina joinb) BitmapScan(joinb) */
SELECT * FROM joina JOIN joinb ON joina.k = joinb.c WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ MergeJoin(joina joinb) Leading(joina joinb) BitmapScan(joinb) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joina JOIN joinb ON joina.k = joinb.c WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ MergeJoin(joina joinb) Leading(joina joinb) BitmapScan(joinb) */
SELECT * FROM joina JOIN joinb ON joina.k = joinb.c WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ HashJoin(joina joinb) Leading(joina joinb) BitmapScan(joinb) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joina JOIN joinb ON joina.k = joinb.c WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ HashJoin(joina joinb) Leading(joina joinb) BitmapScan(joinb) */
SELECT * FROM joina JOIN joinb ON joina.k = joinb.c WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ YbBatchedNL(joina joinb) Leading(joina joinb) BitmapScan(joinb) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joina JOIN joinb ON joina.k = joinb.c WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ YbBatchedNL(joina joinb) Leading(joina joinb) BitmapScan(joinb) */
SELECT * FROM joina JOIN joinb ON joina.k = joinb.c WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;

-- join index col to index col
/*+ NestLoop(joina joinb) Leading(joina joinb) BitmapScan(joinb) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joina JOIN joinb ON joina.a = joinb.c WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ NestLoop(joina joinb) Leading(joina joinb) BitmapScan(joinb) */
SELECT * FROM joina JOIN joinb ON joina.a = joinb.c WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ MergeJoin(joina joinb) Leading(joina joinb) BitmapScan(joinb) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joina JOIN joinb ON joina.a = joinb.c WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ MergeJoin(joina joinb) Leading(joina joinb) BitmapScan(joinb) */
SELECT * FROM joina JOIN joinb ON joina.a = joinb.c WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ HashJoin(joina joinb) Leading(joina joinb) BitmapScan(joinb) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joina JOIN joinb ON joina.a = joinb.c WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ HashJoin(joina joinb) Leading(joina joinb) BitmapScan(joinb) */
SELECT * FROM joina JOIN joinb ON joina.a = joinb.c WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ YbBatchedNL(joina joinb) Leading(joina joinb) BitmapScan(joinb) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joina JOIN joinb ON joina.a = joinb.c WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;
/*+ YbBatchedNL(joina joinb) Leading(joina joinb) BitmapScan(joinb) */
SELECT * FROM joina JOIN joinb ON joina.a = joinb.c WHERE joina.a < 10 OR joina.b < 15 OR joinb.c < 10 OR joinb.d < 15 ORDER BY joina.a;

--
-- Test joins where one of the paths is never executed
--
/*+ BitmapScan(joinb) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT joina.a,
       (SELECT joinb.c FROM joinb WHERE (joina.k = joinb.k OR joinb.d = 1)
                                    AND joina.b = -1) -- unsatisfiable
  FROM joina ORDER BY joina.a;
/*+ BitmapScan(joinb) */
SELECT joina.a,
       (SELECT joinb.c FROM joinb WHERE (joina.k = joinb.k OR joinb.d = 1)
                                    AND joina.b = -1) -- unsatisfiable
  FROM joina ORDER BY joina.a;

--
-- test joins with a function scan
--
/*+ BitmapScan(gr) */ EXPLAIN (ANALYZE, COSTS OFF)
   SELECT grpname, is_colocated
     FROM pg_catalog.yb_table_properties(16384) p
LEFT JOIN pg_catalog.pg_yb_tablegroup gr
       ON gr.oid = p.tablegroup_oid;
/*+ BitmapScan(gr) */
   SELECT grpname, is_colocated
     FROM pg_catalog.yb_table_properties(16384) p
LEFT JOIN pg_catalog.pg_yb_tablegroup gr
       ON gr.oid = p.tablegroup_oid;

--
-- test where a join filter should be required
--
CREATE TABLE test_join_filter(a INT, b INT, v VARCHAR);
CREATE INDEX ON test_join_filter (a ASC);
CREATE INDEX ON test_join_filter (b ASC);
CREATE INDEX ON test_join_filter (v ASC);
INSERT INTO test_join_filter VALUES (1, 4, 'v'), (2, 62, 'v'), (3, 7, 'c'), (4, 1, NULL), (5, 0, 'x'),
                                    (6, 7, 'i'), (7, 7, 'e'), (8, 1, 'p'), (9, 7, 's'), (10, 1, 'j'),
                                    (11, 5, 'z'), (12, 2, 'c'), (13, 0, 'a'), (14, 1, 'q'), (15, 8, 'y'),
                                    (16, 1, NULL), (17, 1, 'r'), (18, 9, 'v'), (19, 1, NULL), (20, 5, 'r');

-- we need a join filter here because the final scan does not contain all quals
/*+ BitmapScan(table2) SeqScan(table1) Leading(((table3 table2) table1)) */ EXPLAIN (ANALYZE, COSTS OFF)
    SELECT table1.v, table1.b
      FROM test_join_filter AS table1
INNER JOIN (( test_join_filter AS table2 INNER JOIN test_join_filter AS table3 ON (( table3.v = table2.v ) OR ( table3.b = table2.a ) ) ) )
        ON (( table3.a >= table2.a ) AND (table3.a <> table2.b ) )
     WHERE ( table1.v = 'g' AND table1.v = 's' ) OR table1.a <= table2.b;

-- we don't need a join filter here because the final scan does satisfy all quals (because its a bitmap scan)
/*+ BitmapScan(table1) Leading(((table3 table2) table1)) */ EXPLAIN (ANALYZE, COSTS OFF)
    SELECT table1.v, table1.b
      FROM test_join_filter AS table1
INNER JOIN (( test_join_filter AS table2 INNER JOIN test_join_filter AS table3 ON (( table3.v = table2.v ) OR ( table3.b = table2.a ) ) ) )
        ON (( table3.a >= table2.a ) AND (table3.a <> table2.b ) )
     WHERE ( table1.v = 'g' AND table1.v = 's' ) OR table1.a <= table2.b;
