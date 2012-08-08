LOAD 'pg_hint_plan';
SET pg_hint_plan.enable TO on;
SET pg_hint_plan.debug_print TO on;
SET client_min_messages TO LOG;
SET search_path TO public;

EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 >= 1;
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c3 < 10;
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1 AND t1.ctid = '(1,1)';

----
---- No. S-1-1 specified pattern of the object name
----

-- No. S-1-1-1
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. S-1-1-2
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 t_1 WHERE t_1.c1 = 1;

-- No. S-1-1-3
/*+SeqScan(t_1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 t_1 WHERE t_1.c1 = 1;

----
---- No. S-1-2 specified schema name in the hint option
----

-- No. S-1-2-1
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. S-1-2-2
/*+SeqScan(s1.t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

----
---- No. S-1-3 table doesn't exist in the hint option
----

-- No. S-1-3-1
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. S-1-3-2
/*+SeqScan(t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

----
---- No. S-1-4 conflict table name
----

-- No. S-1-4-1
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = 1 AND t1.c1 = t2.c1;
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = 1 AND t1.c1 = t2.c1;

-- No. S-1-4-2
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s2.t1 WHERE s1.t1.c1 = 1 AND s1.t1.c1 = s2.t1.c1;
/*+IndexScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s2.t1 WHERE s1.t1.c1 = 1 AND s1.t1.c1 = s2.t1.c1;

/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s2.t1 s2t1 WHERE s1.t1.c1 = 1 AND s1.t1.c1 = s2t1.c1;
/*+BitmapScan(s2t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s2.t1 s2t1 WHERE s1.t1.c1 = 1 AND s1.t1.c1 = s2t1.c1;

-- No. S-1-4-3
EXPLAIN (COSTS false) SELECT (SELECT max(c1) FROM s1.t1 WHERE s1.t1.c1 = 1) FROM s1.t1 WHERE s1.t1.c1 = 1;
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT (SELECT max(c1) FROM s1.t1 WHERE s1.t1.c1 = 1) FROM s1.t1 WHERE s1.t1.c1 = 1;

/*+SeqScan(t11)*/
EXPLAIN (COSTS false) SELECT (SELECT max(c1) FROM s1.t1 t11 WHERE t11.c1 = 1) FROM s1.t1 t12 WHERE t12.c1 = 1;
/*+SeqScan(t12)*/
EXPLAIN (COSTS false) SELECT (SELECT max(c1) FROM s1.t1 t11 WHERE t11.c1 = 1) FROM s1.t1 t12 WHERE t12.c1 = 1;

----
---- No. S-1-5 object type for the hint
----

-- No. S-1-5-1
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. S-1-5-2
EXPLAIN (COSTS false) SELECT * FROM s1.p1 WHERE p1.c1 = 1;
/*+IndexScan(p1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.p1 WHERE p1.c1 = 1;

-- No. S-1-5-3
EXPLAIN (COSTS false) SELECT * FROM s1.ul1 WHERE ul1.c1 = 1;
/*+SeqScan(ul1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ul1 WHERE ul1.c1 = 1;

-- No. S-1-5-4
CREATE TEMP TABLE tm1 (LIKE s1.t1 INCLUDING ALL);
EXPLAIN (COSTS false) SELECT * FROM tm1 WHERE tm1.c1 = 1;
/*+SeqScan(tm1)*/
EXPLAIN (COSTS false) SELECT * FROM tm1 WHERE tm1.c1 = 1;

-- No. S-1-5-5
EXPLAIN (COSTS false) SELECT * FROM pg_catalog.pg_class WHERE oid = 1;
/*+SeqScan(pg_class)*/
EXPLAIN (COSTS false) SELECT * FROM pg_catalog.pg_class WHERE oid = 1;

-- No. S-1-5-6
-- refer fdw.sql

-- No. S-1-5-7
EXPLAIN (COSTS false) SELECT * FROM s1.f1() AS ft1 WHERE ft1.c1 = 1;
/*+SeqScan(ft1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.f1() AS ft1 WHERE ft1.c1 = 1;

-- No. S-1-5-8
EXPLAIN (COSTS false) SELECT * FROM (VALUES(1,1,1,'1'), (2,2,2,'2'), (3,3,3,'3')) AS val1 (c1, c2, c3, c4) WHERE val1.c1 = 1;
/*+SeqScan(val1)*/
EXPLAIN (COSTS false) SELECT * FROM (VALUES(1,1,1,'1'), (2,2,2,'2'), (3,3,3,'3')) AS val1 (c1, c2, c3, c4) WHERE val1.c1 = 1;
/*+SeqScan(*VALUES*)*/
EXPLAIN (COSTS false) SELECT * FROM (VALUES(1,1,1,'1'), (2,2,2,'2'), (3,3,3,'3')) AS val1 (c1, c2, c3, c4) WHERE val1.c1 = 1;

-- No. S-1-5-9
EXPLAIN (COSTS false) WITH c1(c1) AS (SELECT max(c1) FROM s1.t1 WHERE t1.c1 = 1)
SELECT * FROM s1.t1, c1 WHERE t1.c1 = 1 AND t1.c1 = c1.c1;
/*+SeqScan(c1)*/
EXPLAIN (COSTS false) WITH c1(c1) AS (SELECT max(c1) FROM s1.t1 WHERE t1.c1 = 1)
SELECT * FROM s1.t1, c1 WHERE t1.c1 = 1 AND t1.c1 = c1.c1;

-- No. S-1-5-10
EXPLAIN (COSTS false) SELECT * FROM s1.v1 WHERE v1.c1 = 1;
/*+SeqScan(v1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.v1 WHERE v1.c1 = 1;

-- No. S-1-5-11
EXPLAIN (COSTS false) SELECT * FROM (SELECT * FROM s1.t1 WHERE t1.c1 = 1) AS s1 WHERE s1.c1 = 1;
/*+SeqScan(s1)*/
EXPLAIN (COSTS false) SELECT * FROM (SELECT * FROM s1.t1 WHERE t1.c1 = 1) AS s1 WHERE s1.c1 = 1;

----
---- No. S-3-1 scan method hint
----

-- No. S-3-1-1
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 >= 1;
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 >= 1;

-- No. S-3-1-2
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. S-3-1-3
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;
/*+IndexScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. S-3-1-4
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 >= 1;
/*+IndexScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 >= 1;

-- No. S-3-1-5
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c3 < 10;
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c3 < 10;

-- No. S-3-1-6
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. S-3-1-7
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1 AND t1.ctid = '(1,1)';
/*+TidScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1 AND t1.ctid = '(1,1)';

-- No. S-3-1-8
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1 AND t1.ctid IN ('(1,1)', '(2,2)', '(3,3)');
/*+TidScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1 AND t1.ctid IN ('(1,1)', '(2,2)', '(3,3)');

-- No. S-3-1-9
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 >= 1;
/*+NoSeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 >= 1;

-- No. S-3-1-10
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;
/*+NoSeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. S-3-1-11
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;
/*+NoIndexScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. S-3-1-12
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 >= 1;
/*+NoIndexScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 >= 1;

-- No. S-3-1-13
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c3 < 10;
/*+NoBitmapScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c3 < 10;

-- No. S-3-1-14
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;
/*+NoBitmapScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. S-3-1-15
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1 AND t1.ctid = '(1,1)';
/*+NoTidScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1 AND t1.ctid = '(1,1)';

-- No. S-3-1-16
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;
/*+NoTidScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

