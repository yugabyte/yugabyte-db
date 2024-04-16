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

--
-- Rescans where we don't need the actual rows
-- This test is based off #21526 identified by the random query generator.
-- To speed up the test, don't bother creating new tables.
--
/*+ BitmapScan(joina) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joinb  WHERE EXISTS (SELECT FROM joina WHERE joina.a >= joinb.d) OR joinb.c = 1 ORDER BY joinb.k;
/*+ BitmapScan(joina) */
SELECT * FROM joinb  WHERE EXISTS (SELECT FROM joina WHERE joina.a >= joinb.d) OR joinb.c = 1 ORDER BY joinb.k;
/*+ Set(enable_bitmapscan false) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT * FROM joinb  WHERE EXISTS (SELECT FROM joina WHERE joina.a >= joinb.d) OR joinb.c = 1 ORDER BY joinb.k;
/*+ Set(enable_bitmapscan false) */
SELECT * FROM joinb  WHERE EXISTS (SELECT FROM joina WHERE joina.a >= joinb.d) OR joinb.c = 1 ORDER BY joinb.k;

--
-- BitmapAnd tests
-- This is issue #21495 identified by the random query generator.
--
CREATE TABLE bb (
    pk serial,
    col_int_nokey integer,
    col_int_key integer,
    col_date_key date,
    col_date_nokey date,
    col_time_key time without time zone,
    col_time_nokey time without time zone,
    col_datetime_key timestamp without time zone,
    col_datetime_nokey timestamp without time zone,
    col_varchar_key character varying(1),
    col_varchar_nokey character varying(1),
    CONSTRAINT bb_pkey PRIMARY KEY(pk ASC)
);

CREATE INDEX bb_int_key ON bb USING lsm (col_int_key ASC);

CREATE TABLE c (
    pk serial,
    col_int_nokey integer,
    col_int_key integer,
    col_date_key date,
    col_date_nokey date,
    col_time_key time without time zone,
    col_time_nokey time without time zone,
    col_datetime_key timestamp without time zone,
    col_datetime_nokey timestamp without time zone,
    col_varchar_key character varying(1),
    col_varchar_nokey character varying(1),
    CONSTRAINT c_pkey PRIMARY KEY(pk ASC)
);

CREATE INDEX c_int_key ON c USING lsm (col_int_key ASC);
CREATE INDEX c_varchar_key ON c USING lsm (col_varchar_key ASC, col_int_key ASC);

COPY bb (pk, col_int_nokey, col_int_key, col_date_key, col_date_nokey, col_time_key, col_time_nokey, col_datetime_key, col_datetime_nokey, col_varchar_key, col_varchar_nokey) FROM stdin;
10	7	8	2004-10-02	2004-10-02	04:07:22.028954	04:07:22.028954	2032-10-08 00:00:00	2032-10-08 00:00:00	g	g
\.

COPY c (pk, col_int_nokey, col_int_key, col_date_key, col_date_nokey, col_time_key, col_time_nokey, col_datetime_key, col_datetime_nokey, col_varchar_key, col_varchar_nokey) FROM stdin;
1	2	4	1984-12-05	1984-12-05	22:34:09.023306	22:34:09.023306	1900-01-01 00:00:00	1900-01-01 00:00:00	v	v
2	150	62	2021-03-27	2021-03-27	14:26:02.007788	14:26:02.007788	1979-01-03 10:33:32.027981	1979-01-03 10:33:32.027981	v	v
3	\N	7	2025-04-09	2025-04-09	14:03:03.042673	14:03:03.042673	2027-11-28 00:50:27.051028	2027-11-28 00:50:27.051028	c	c
4	2	1	1972-05-13	1972-05-13	01:46:09.016386	01:46:09.016386	2003-10-09 19:53:04.008332	2003-10-09 19:53:04.008332	\N	\N
5	5	0	1992-05-06	1992-05-06	16:21:18.052408	16:21:18.052408	2027-11-08 21:02:12.009395	2027-11-08 21:02:12.009395	x	x
6	3	7	1977-03-03	1977-03-03	18:56:33.027423	18:56:33.027423	1974-04-01 00:00:00	1974-04-01 00:00:00	i	i
7	1	7	1973-12-28	1973-12-28	\N	\N	1900-01-01 00:00:00	1900-01-01 00:00:00	e	e
8	4	1	2035-10-20	2035-10-20	09:29:08.048031	09:29:08.048031	1973-07-12 00:00:00	1973-07-12 00:00:00	p	p
9	\N	7	2004-04-09	2004-04-09	19:11:10.040728	19:11:10.040728	2006-04-04 01:21:01.040391	2006-04-04 01:21:01.040391	s	s
10	2	1	2021-12-25	2021-12-25	11:57:26.013363	11:57:26.013363	1900-01-01 00:00:00	1900-01-01 00:00:00	j	j
11	6	5	1900-01-01	1900-01-01	00:39:46.041355	00:39:46.041355	1984-03-05 03:41:18.061978	1984-03-05 03:41:18.061978	z	z
12	6	2	\N	\N	03:28:15.007081	03:28:15.007081	1989-08-03 11:33:04.049998	1989-08-03 11:33:04.049998	c	c
13	8	0	1900-01-01	1900-01-01	06:44:18.007092	06:44:18.007092	2010-04-28 21:44:45.050791	2010-04-28 21:44:45.050791	a	a
14	2	1	2027-01-16	2027-01-16	14:36:39.062494	14:36:39.062494	1972-04-06 00:00:00	1972-04-06 00:00:00	q	q
15	6	8	1900-01-01	1900-01-01	18:42:45.053707	18:42:45.053707	2017-04-18 00:00:00	2017-04-18 00:00:00	y	y
16	8	1	2012-11-23	2012-11-23	02:57:29.012755	02:57:29.012755	2009-12-18 19:39:55.005399	2009-12-18 19:39:55.005399	\N	\N
17	3	1	2005-11-04	2005-11-04	16:46:13.01546	16:46:13.01546	2011-08-01 12:19:39.028493	2011-08-01 12:19:39.028493	r	r
18	3	9	1994-03-12	1994-03-12	19:39:02.040624	19:39:02.040624	1995-09-25 21:29:06.004058	1995-09-25 21:29:06.004058	v	v
19	9	1	2013-06-22	2013-06-22	\N	\N	2010-09-20 09:11:48.065041	2010-09-20 09:11:48.065041	\N	\N
20	6	5	1995-10-10	1995-10-10	20:58:33.049572	20:58:33.049572	2020-03-27 09:32:04.056959	2020-03-27 09:32:04.056959	r	r
\.

/*+ BitmapScan(subquery1_t1) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT table1.pk AS pk FROM ( ( SELECT SUBQUERY1_t1 .* FROM ( C AS SUBQUERY1_t1 INNER JOIN BB ON ( BB.col_int_key = SUBQUERY1_t1.pk ) ) ) AS table1 JOIN ( SELECT * FROM C ) AS table2 ON ( table2.col_varchar_key = table1.col_varchar_key ) ) WHERE table1.col_int_key IN ( SELECT col_int_nokey FROM C AS C WHERE C.col_varchar_key != table2.col_varchar_key AND C.col_varchar_nokey >= table2.col_varchar_nokey ) AND table1.pk = table2 .col_int_key OR table1.col_int_key = table2.col_int_key;
/*+ BitmapScan(subquery1_t1) */
SELECT table1.pk AS pk FROM ( ( SELECT SUBQUERY1_t1 .* FROM ( C AS SUBQUERY1_t1 INNER JOIN BB ON ( BB.col_int_key = SUBQUERY1_t1.pk ) ) ) AS table1 JOIN ( SELECT * FROM C ) AS table2 ON ( table2.col_varchar_key = table1.col_varchar_key ) ) WHERE table1.col_int_key IN ( SELECT col_int_nokey FROM C AS C WHERE C.col_varchar_key != table2.col_varchar_key AND C.col_varchar_nokey >= table2.col_varchar_nokey ) AND table1.pk = table2 .col_int_key OR table1.col_int_key = table2.col_int_key;
/*+ Set(enable_bitmapscan false) */ EXPLAIN (ANALYZE, COSTS OFF)
SELECT table1.pk AS pk FROM ( ( SELECT SUBQUERY1_t1 .* FROM ( C AS SUBQUERY1_t1 INNER JOIN BB ON ( BB.col_int_key = SUBQUERY1_t1.pk ) ) ) AS table1 JOIN ( SELECT * FROM C ) AS table2 ON ( table2.col_varchar_key = table1.col_varchar_key ) ) WHERE table1.col_int_key IN ( SELECT col_int_nokey FROM C AS C WHERE C.col_varchar_key != table2.col_varchar_key AND C.col_varchar_nokey >= table2.col_varchar_nokey ) AND table1.pk = table2 .col_int_key OR table1.col_int_key = table2.col_int_key;
/*+ Set(enable_bitmapscan false) */
SELECT table1.pk AS pk FROM ( ( SELECT SUBQUERY1_t1 .* FROM ( C AS SUBQUERY1_t1 INNER JOIN BB ON ( BB.col_int_key = SUBQUERY1_t1.pk ) ) ) AS table1 JOIN ( SELECT * FROM C ) AS table2 ON ( table2.col_varchar_key = table1.col_varchar_key ) ) WHERE table1.col_int_key IN ( SELECT col_int_nokey FROM C AS C WHERE C.col_varchar_key != table2.col_varchar_key AND C.col_varchar_nokey >= table2.col_varchar_nokey ) AND table1.pk = table2 .col_int_key OR table1.col_int_key = table2.col_int_key;

--
-- Semi Join
--
/*+ BitmapScan(joinb) */ EXPLAIN ANALYZE
SELECT joina.a FROM joina WHERE EXISTS (SELECT FROM joinb WHERE joinb.c >= joina.b) ORDER BY joina.a;
SELECT joina.a FROM joina WHERE EXISTS (SELECT FROM joinb WHERE joinb.c >= joina.b) ORDER BY joina.a;
/*+ Set(enable_bitmapscan false) */ EXPLAIN ANALYZE
SELECT joina.a FROM joina WHERE EXISTS (SELECT FROM joinb WHERE joinb.c >= joina.b) ORDER BY joina.a;
SELECT joina.a FROM joina WHERE EXISTS (SELECT FROM joinb WHERE joinb.c >= joina.b) ORDER BY joina.a;

--
-- Anti Join
--
/*+ BitmapScan(joinb) */ EXPLAIN ANALYZE
SELECT joina.a FROM joina WHERE NOT EXISTS (SELECT FROM joinb WHERE joinb.c >= joina.b) ORDER BY joina.a;
SELECT joina.a FROM joina WHERE NOT EXISTS (SELECT FROM joinb WHERE joinb.c >= joina.b) ORDER BY joina.a;
/*+ Set(enable_bitmapscan false) */ EXPLAIN ANALYZE
SELECT joina.a FROM joina WHERE NOT EXISTS (SELECT FROM joinb WHERE joinb.c >= joina.b) ORDER BY joina.a;
SELECT joina.a FROM joina WHERE NOT EXISTS (SELECT FROM joinb WHERE joinb.c >= joina.b) ORDER BY joina.a;

RESET yb_explain_hide_non_deterministic_fields;
RESET enable_bitmapscan;
RESET yb_prefer_bnl;
