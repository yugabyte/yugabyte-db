LOAD 'pg_hint_plan';
SET pg_hint_plan.enable TO on;
SET pg_hint_plan.debug_print TO on;
SET client_min_messages TO LOG;
SET search_path TO public;

EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3, s1.t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;

----
---- No. L-1-1 specified pattern of the object name
----

-- No. L-1-1-1
/*+Leading(t4 t2 t3 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3, s1.t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;

-- No. L-1-1-2
/*+Leading(t4 t2 t3 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 t_1, s1.t2 t_2, s1.t3 t_3, s1.t4 t_4 WHERE t_1.c1 = t_2.c1 AND t_1.c1 = t_3.c1 AND t_1.c1 = t_4.c1;

-- No. L-1-1-3
/*+Leading(t_4 t_2 t_3 t_1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 t_1, s1.t2 t_2, s1.t3 t_3, s1.t4 t_4 WHERE t_1.c1 = t_2.c1 AND t_1.c1 = t_3.c1 AND t_1.c1 = t_4.c1;

----
---- No. L-1-2 specified schema name in the hint option
----

-- No. L-1-2-1
/*+Leading(t4 t2 t3 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3, s1.t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;

-- No. L-1-2-2
/*+Leading(s1.t4 s1.t2 s1.t3 s1.t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3, s1.t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;

----
---- No. L-1-3 table doesn't exist in the hint option
----

-- No. L-1-3-1
/*+Leading(t4 t2 t3 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3, s1.t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;

-- No. L-1-3-1
/*+Leading(t5 t2 t3 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3, s1.t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;

----
---- No. L-1-4 conflict table name
----

-- No. L-1-4-1
/*+Leading(t4 t2 t3 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3, s1.t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;

-- No. L-1-4-2
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3, s2.t1 WHERE s1.t1.c1 = t2.c1 AND s1.t1.c1 = t3.c1 AND s1.t1.c1 = s2.t1.c1;
/*+Leading(t1 t2 t3 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3, s2.t1 WHERE s1.t1.c1 = t2.c1 AND s1.t1.c1 = t3.c1 AND s1.t1.c1 = s2.t1.c1;
/*+Leading(s1.t1 t2 t3 s2.t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3, s2.t1 WHERE s1.t1.c1 = t2.c1 AND s1.t1.c1 = t3.c1 AND s1.t1.c1 = s2.t1.c1;

EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3, s2.t1 s2t1 WHERE s1.t1.c1 = t2.c1 AND s1.t1.c1 = t3.c1 AND s1.t1.c1 = s2t1.c1;
/*+Leading(s2t1 t1 t3 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3, s2.t1 s2t1 WHERE s1.t1.c1 = t2.c1 AND s1.t1.c1 = t3.c1 AND s1.t1.c1 = s2t1.c1;

-- No. L-1-4-3
EXPLAIN (COSTS false) SELECT *, (SELECT max(t1.c1) FROM s1.t1, s1.t2, s1.t3, s1.t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1) FROM s1.t1, s1.t2, s1.t3, s1.t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;
/*+Leading(t4 t2 t3 t1)*/
EXPLAIN (COSTS false) SELECT *, (SELECT max(t1.c1) FROM s1.t1, s1.t2, s1.t3, s1.t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1) FROM s1.t1, s1.t2, s1.t3, s1.t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;
/*+Leading(st1 st2 st3 st4 t4 t2 t3 t1)*/
EXPLAIN (COSTS false) SELECT *, (SELECT max(st1.c1) FROM s1.t1 st1, s1.t2 st2, s1.t3 st3, s1.t4 st4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1) FROM s1.t1, s1.t2, s1.t3, s1.t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;

----
---- No. L-1-5 conflict table name
----

-- No. L-1-5-1
/*+Leading(t4 t2 t3 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3, s1.t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;

-- No. L-1-5-2
/*+Leading(t4 t2 t3 t1 t4)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3, s1.t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;
/*+Leading(t4 t2 t3 t4)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3, s1.t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;

-- No. L-1-5-3
/*+Leading(t4 t2 t3 t1 t4 t2 t3 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3, s1.t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;
/*+Leading(t4 t2 t2 t4)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3, s1.t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;

----
---- No. L-1-6 object type for the hint
----

-- No. L-1-6-1
/*+Leading(t4 t2 t3 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3, s1.t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;

-- No. L-1-6-2
EXPLAIN (COSTS false) SELECT * FROM s1.p1 t1, s1.p1 t2, s1.p1 t3, s1.p1 t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;
/*+Leading(t4 t3 t2 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.p1 t1, s1.p1 t2, s1.p1 t3, s1.p1 t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;

-- No. L-1-6-3
EXPLAIN (COSTS false) SELECT * FROM s1.ul1 t1, s1.ul1 t2, s1.ul1 t3, s1.ul1 t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;
/*+Leading(t4 t3 t2 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ul1 t1, s1.ul1 t2, s1.ul1 t3, s1.ul1 t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;

-- No. L-1-6-4
CREATE TEMP TABLE tm1 (LIKE s1.t1 INCLUDING ALL);
EXPLAIN (COSTS false) SELECT * FROM tm1 t1, tm1 t2, tm1 t3, tm1 t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;
/*+Leading(t4 t3 t2 t1)*/
EXPLAIN (COSTS false) SELECT * FROM tm1 t1, tm1 t2, tm1 t3, tm1 t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;

-- No. L-1-6-5
EXPLAIN (COSTS false) SELECT * FROM pg_catalog.pg_class t1, pg_catalog.pg_class t2, pg_catalog.pg_class t3, pg_catalog.pg_class t4 WHERE t1.oid = t2.oid AND t1.oid = t3.oid AND t1.oid = t4.oid;
/*+Leading(t4 t3 t2 t1)*/
EXPLAIN (COSTS false) SELECT * FROM pg_catalog.pg_class t1, pg_catalog.pg_class t2, pg_catalog.pg_class t3, pg_catalog.pg_class t4 WHERE t1.oid = t2.oid AND t1.oid = t3.oid AND t1.oid = t4.oid;

-- No. L-1-6-6
-- refer fdw.sql

-- No. L-1-6-7
EXPLAIN (COSTS false) SELECT * FROM s1.f1() t1, s1.f1() t2, s1.f1() t3, s1.f1() t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;
/*+Leading(t4 t3 t2 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.f1() t1, s1.f1() t2, s1.f1() t3, s1.f1() t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;

-- No. L-1-6-8
EXPLAIN (COSTS false) SELECT * FROM (VALUES(1,1,1,'1')) AS t1 (c1, c2, c3, c4), (VALUES(1,1,1,'1'), (2,2,2,'2')) AS t2 (c1, c2, c3, c4), (VALUES(1,1,1,'1'), (2,2,2,'2'), (3,3,3,'3')) AS t3 (c1, c2, c3, c4), (VALUES(1,1,1,'1'), (2,2,2,'2'), (3,3,3,'3'), (4,4,4,'4')) AS t4  (c1, c2, c3, c4) WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;
/*+Leading(t4 t3 t2 t1)*/
EXPLAIN (COSTS false) SELECT * FROM (VALUES(1,1,1,'1')) AS t1 (c1, c2, c3, c4), (VALUES(1,1,1,'1'), (2,2,2,'2')) AS t2 (c1, c2, c3, c4), (VALUES(1,1,1,'1'), (2,2,2,'2'), (3,3,3,'3')) AS t3 (c1, c2, c3, c4), (VALUES(1,1,1,'1'), (2,2,2,'2'), (3,3,3,'3'), (4,4,4,'4')) AS t4  (c1, c2, c3, c4) WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;

-- No. L-1-6-9
EXPLAIN (COSTS false) WITH c1(c1) AS (SELECT st1.c1 FROM s1.t1 st1, s1.t1 st2, s1.t1 st3, s1.t1 st4 WHERE st1.c1 = st2.c1 AND st1.c1 = st3.c1 AND st1.c1 = st4.c1) SELECT * FROM c1 ct1, c1 ct2, c1 ct3, c1 ct4 WHERE ct1.c1 = ct2.c1 AND ct1.c1 = ct3.c1 AND ct1.c1 = ct4.c1;
/*+Leading(ct4 ct3 ct2 ct1 st4 st3 st2 st1)*/
EXPLAIN (COSTS false) WITH c1(c1) AS (SELECT st1.c1 FROM s1.t1 st1, s1.t1 st2, s1.t1 st3, s1.t1 st4 WHERE st1.c1 = st2.c1 AND st1.c1 = st3.c1 AND st1.c1 = st4.c1) SELECT * FROM c1 ct1, c1 ct2, c1 ct3, c1 ct4 WHERE ct1.c1 = ct2.c1 AND ct1.c1 = ct3.c1 AND ct1.c1 = ct4.c1;

-- No. L-1-6-10
EXPLAIN (COSTS false) SELECT * FROM s1.v1 t1, s1.v1 t2, s1.v1 t3, s1.v1 t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;
/*+Leading(t4 t3 t2 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.v1 t1, s1.v1 t2, s1.v1 t3, s1.v1 t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;
EXPLAIN (COSTS false) SELECT * FROM s1.v1 t1, s1.v1_ t2, s1.t3, s1.t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;
/*+Leading(t4 v1t1_ v1t1 t3)*/
EXPLAIN (COSTS false) SELECT * FROM s1.v1 t1, s1.v1_ t2, s1.t3, s1.t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;

-- No. L-1-6-11
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3, (SELECT t4.c1 FROM s1.t4) st4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = st4.c1;
/*+Leading(st4 t2 t3 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3, (SELECT t4.c1 FROM s1.t4) st4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = st4.c1;
/*+Leading(t4 t2 t3 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3, (SELECT t4.c1 FROM s1.t4) st4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = st4.c1;
