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

----
---- No. L-2-1 complexity query block
----

-- No. L-2-1-1
EXPLAIN (COSTS false)
SELECT max(bmt1.c1), (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = b1t2.c1 AND b1t2.ctid = '(1,1)' AND b1t1.c1 = b1t3.c1 AND b1t3.ctid = '(1,1)' AND b1t1.c1 = b1t4.c1 AND b1t4.ctid = '(1,1)'
), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2, s1.t3 b2t3, s1.t4 b2t4 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = b2t2.c1 AND b2t2.ctid = '(1,1)' AND b2t1.c1 = b2t3.c1 AND b2t3.ctid = '(1,1)' AND b2t1.c1 = b2t4.c1 AND b2t4.ctid = '(1,1)'
)
                    FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = bmt3.c1 AND bmt3.ctid = '(1,1)' AND bmt1.c1 = bmt4.c1 AND bmt4.ctid = '(1,1)'
;
/*+
Leading(bmt1 bmt2 bmt3 bmt4 b1t2 b1t3 b1t4 b1t1 b2t3 b2t4 b2t1 b2t2)
*/
EXPLAIN (COSTS false)
SELECT max(bmt1.c1), (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = b1t2.c1 AND b1t2.ctid = '(1,1)' AND b1t1.c1 = b1t3.c1 AND b1t3.ctid = '(1,1)' AND b1t1.c1 = b1t4.c1 AND b1t4.ctid = '(1,1)'
), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2, s1.t3 b2t3, s1.t4 b2t4 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = b2t2.c1 AND b2t2.ctid = '(1,1)' AND b2t1.c1 = b2t3.c1 AND b2t3.ctid = '(1,1)' AND b2t1.c1 = b2t4.c1 AND b2t4.ctid = '(1,1)'
)
                    FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = bmt3.c1 AND bmt3.ctid = '(1,1)' AND bmt1.c1 = bmt4.c1 AND bmt4.ctid = '(1,1)'
;

-- No. L-2-1-2
EXPLAIN (COSTS false)
SELECT max(bmt1.c1), (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = b1t2.c1 AND b1t2.ctid = '(1,1)' AND b1t1.c1 = b1t3.c1 AND b1t3.ctid = '(1,1)' AND b1t1.c1 = b1t4.c1 AND b1t4.ctid = '(1,1)'
), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2, s1.t3 b2t3, s1.t4 b2t4 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = b2t2.c1 AND b2t2.ctid = '(1,1)' AND b2t1.c1 = b2t3.c1 AND b2t3.ctid = '(1,1)' AND b2t1.c1 = b2t4.c1 AND b2t4.ctid = '(1,1)'
), (
SELECT max(b3t1.c1) FROM s1.t1 b3t1, s1.t2 b3t2, s1.t3 b3t3, s1.t4 b3t4 WHERE b3t1.ctid = '(1,1)' AND b3t1.c1 = b3t2.c1 AND b3t2.ctid = '(1,1)' AND b3t1.c1 = b3t3.c1 AND b3t3.ctid = '(1,1)' AND b3t1.c1 = b3t4.c1 AND b3t4.ctid = '(1,1)'
)
                    FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = bmt3.c1 AND bmt3.ctid = '(1,1)' AND bmt1.c1 = bmt4.c1 AND bmt4.ctid = '(1,1)'
;
/*+
Leading(bmt1 bmt2 bmt3 bmt4 b1t2 b1t3 b1t4 b1t1 b2t3 b2t4 b2t1 b2t2 b3t4 b3t1 b3t2 b3t3)
*/
EXPLAIN (COSTS false)
SELECT max(bmt1.c1), (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = b1t2.c1 AND b1t2.ctid = '(1,1)' AND b1t1.c1 = b1t3.c1 AND b1t3.ctid = '(1,1)' AND b1t1.c1 = b1t4.c1 AND b1t4.ctid = '(1,1)'
), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2, s1.t3 b2t3, s1.t4 b2t4 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = b2t2.c1 AND b2t2.ctid = '(1,1)' AND b2t1.c1 = b2t3.c1 AND b2t3.ctid = '(1,1)' AND b2t1.c1 = b2t4.c1 AND b2t4.ctid = '(1,1)'
), (
SELECT max(b3t1.c1) FROM s1.t1 b3t1, s1.t2 b3t2, s1.t3 b3t3, s1.t4 b3t4 WHERE b3t1.ctid = '(1,1)' AND b3t1.c1 = b3t2.c1 AND b3t2.ctid = '(1,1)' AND b3t1.c1 = b3t3.c1 AND b3t3.ctid = '(1,1)' AND b3t1.c1 = b3t4.c1 AND b3t4.ctid = '(1,1)'
)
                    FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = bmt3.c1 AND bmt3.ctid = '(1,1)' AND bmt1.c1 = bmt4.c1 AND bmt4.ctid = '(1,1)'
;

-- No. L-2-1-3
EXPLAIN (COSTS false) SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, (SELECT ctid, * FROM s1.t3 bmt3) sbmt3, (SELECT ctid, * FROM s1.t4 bmt4) sbmt4 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = sbmt3.c1 AND sbmt3.ctid = '(1,1)' AND bmt1.c1 = sbmt4.c1 AND sbmt4.ctid = '(1,1)';
/*+
Leading(bmt4 bmt3 bmt2 bmt1)
*/
EXPLAIN (COSTS false) SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, (SELECT ctid, * FROM s1.t3 bmt3) sbmt3, (SELECT ctid, * FROM s1.t4 bmt4) sbmt4 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = sbmt3.c1 AND sbmt3.ctid = '(1,1)' AND bmt1.c1 = sbmt4.c1 AND sbmt4.ctid = '(1,1)';

-- No. L-2-1-4
EXPLAIN (COSTS false) SELECT max(bmt1.c1) FROM s1.t1 bmt1, (SELECT ctid, * FROM s1.t2 bmt2) sbmt2, (SELECT ctid, * FROM s1.t3 bmt3) sbmt3, (SELECT ctid, * FROM s1.t4 bmt4) sbmt4 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = sbmt2.c1 AND sbmt2.ctid = '(1,1)' AND bmt1.c1 = sbmt3.c1 AND sbmt3.ctid = '(1,1)' AND bmt1.c1 = sbmt4.c1 AND sbmt4.ctid = '(1,1)';
/*+
Leading(bmt4 bmt3 bmt2 bmt1)
*/
EXPLAIN (COSTS false) SELECT max(bmt1.c1) FROM s1.t1 bmt1, (SELECT ctid, * FROM s1.t2 bmt2) sbmt2, (SELECT ctid, * FROM s1.t3 bmt3) sbmt3, (SELECT ctid, * FROM s1.t4 bmt4) sbmt4 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = sbmt2.c1 AND sbmt2.ctid = '(1,1)' AND bmt1.c1 = sbmt3.c1 AND sbmt3.ctid = '(1,1)' AND bmt1.c1 = sbmt4.c1 AND sbmt4.ctid = '(1,1)';

-- No. L-2-1-5
EXPLAIN (COSTS false)
SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = bmt3.c1 AND bmt3.ctid = '(1,1)' AND bmt1.c1 = bmt4.c1 AND bmt4.ctid = '(1,1)'
  AND bmt1.c1 <> (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = b1t2.c1 AND b1t2.ctid = '(1,1)' AND b1t1.c1 = b1t3.c1 AND b1t3.ctid = '(1,1)' AND b1t1.c1 = b1t4.c1 AND b1t4.ctid = '(1,1)'
) AND bmt1.c1 <> (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2, s1.t3 b2t3, s1.t4 b2t4 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = b2t2.c1 AND b2t2.ctid = '(1,1)' AND b2t1.c1 = b2t3.c1 AND b2t3.ctid = '(1,1)' AND b2t1.c1 = b2t4.c1 AND b2t4.ctid = '(1,1)'
)
;
/*+
Leading(bmt1 bmt2 bmt3 bmt4 b1t2 b1t3 b1t4 b1t1 b2t3 b2t4 b2t1 b2t2)
*/
EXPLAIN (COSTS false)
SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = bmt3.c1 AND bmt3.ctid = '(1,1)' AND bmt1.c1 = bmt4.c1 AND bmt4.ctid = '(1,1)'
  AND bmt1.c1 <> (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = b1t2.c1 AND b1t2.ctid = '(1,1)' AND b1t1.c1 = b1t3.c1 AND b1t3.ctid = '(1,1)' AND b1t1.c1 = b1t4.c1 AND b1t4.ctid = '(1,1)'
) AND bmt1.c1 <> (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2, s1.t3 b2t3, s1.t4 b2t4 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = b2t2.c1 AND b2t2.ctid = '(1,1)' AND b2t1.c1 = b2t3.c1 AND b2t3.ctid = '(1,1)' AND b2t1.c1 = b2t4.c1 AND b2t4.ctid = '(1,1)'
)
;

-- No. L-2-1-6
EXPLAIN (COSTS false)
SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = bmt3.c1 AND bmt3.ctid = '(1,1)' AND bmt1.c1 = bmt4.c1 AND bmt4.ctid = '(1,1)'
  AND bmt1.c1 <> (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = b1t2.c1 AND b1t2.ctid = '(1,1)' AND b1t1.c1 = b1t3.c1 AND b1t3.ctid = '(1,1)' AND b1t1.c1 = b1t4.c1 AND b1t4.ctid = '(1,1)'
) AND bmt1.c1 <> (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2, s1.t3 b2t3, s1.t4 b2t4 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = b2t2.c1 AND b2t2.ctid = '(1,1)' AND b2t1.c1 = b2t3.c1 AND b2t3.ctid = '(1,1)' AND b2t1.c1 = b2t4.c1 AND b2t4.ctid = '(1,1)'
) AND bmt1.c1 <> (
SELECT max(b3t1.c1) FROM s1.t1 b3t1, s1.t2 b3t2, s1.t3 b3t3, s1.t4 b3t4 WHERE b3t1.ctid = '(1,1)' AND b3t1.c1 = b3t2.c1 AND b3t2.ctid = '(1,1)' AND b3t1.c1 = b3t3.c1 AND b3t3.ctid = '(1,1)' AND b3t1.c1 = b3t4.c1 AND b3t4.ctid = '(1,1)'
)
;
/*+
Leading(bmt1 bmt2 bmt3 bmt4 b1t2 b1t3 b1t4 b1t1 b2t3 b2t4 b2t1 b2t2 b3t4 b3t1 b3t2 b3t3)
*/
EXPLAIN (COSTS false)
SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = bmt3.c1 AND bmt3.ctid = '(1,1)' AND bmt1.c1 = bmt4.c1 AND bmt4.ctid = '(1,1)'
  AND bmt1.c1 <> (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = b1t2.c1 AND b1t2.ctid = '(1,1)' AND b1t1.c1 = b1t3.c1 AND b1t3.ctid = '(1,1)' AND b1t1.c1 = b1t4.c1 AND b1t4.ctid = '(1,1)'
) AND bmt1.c1 <> (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2, s1.t3 b2t3, s1.t4 b2t4 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = b2t2.c1 AND b2t2.ctid = '(1,1)' AND b2t1.c1 = b2t3.c1 AND b2t3.ctid = '(1,1)' AND b2t1.c1 = b2t4.c1 AND b2t4.ctid = '(1,1)'
) AND bmt1.c1 <> (
SELECT max(b3t1.c1) FROM s1.t1 b3t1, s1.t2 b3t2, s1.t3 b3t3, s1.t4 b3t4 WHERE b3t1.ctid = '(1,1)' AND b3t1.c1 = b3t2.c1 AND b3t2.ctid = '(1,1)' AND b3t1.c1 = b3t3.c1 AND b3t3.ctid = '(1,1)' AND b3t1.c1 = b3t4.c1 AND b3t4.ctid = '(1,1)'
)
;

-- No. L-2-1-7
EXPLAIN (COSTS false)
WITH c1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = b1t2.c1 AND b1t2.ctid = '(1,1)' AND b1t1.c1 = b1t3.c1 AND b1t3.ctid = '(1,1)' AND b1t1.c1 = b1t4.c1 AND b1t4.ctid = '(1,1)'
)
, c2 (c1) AS (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2, s1.t3 b2t3, s1.t4 b2t4 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = b2t2.c1 AND b2t2.ctid = '(1,1)' AND b2t1.c1 = b2t3.c1 AND b2t3.ctid = '(1,1)' AND b2t1.c1 = b2t4.c1 AND b2t4.ctid = '(1,1)'
)
SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4
, c1, c2
                                                                        WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = bmt3.c1 AND bmt3.ctid = '(1,1)' AND bmt1.c1 = bmt4.c1 AND bmt4.ctid = '(1,1)'
AND bmt1.c1 = c1.c1
AND bmt1.c1 = c2.c1
;
/*+
Leading(c2 c1 bmt1 bmt2 bmt3 bmt4 b1t2 b1t3 b1t4 b1t1 b2t3 b2t4 b2t1 b2t2)
*/
EXPLAIN (COSTS false)
WITH c1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = b1t2.c1 AND b1t2.ctid = '(1,1)' AND b1t1.c1 = b1t3.c1 AND b1t3.ctid = '(1,1)' AND b1t1.c1 = b1t4.c1 AND b1t4.ctid = '(1,1)'
)
, c2 (c1) AS (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2, s1.t3 b2t3, s1.t4 b2t4 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = b2t2.c1 AND b2t2.ctid = '(1,1)' AND b2t1.c1 = b2t3.c1 AND b2t3.ctid = '(1,1)' AND b2t1.c1 = b2t4.c1 AND b2t4.ctid = '(1,1)'
)
SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4
, c1, c2
                                                                        WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = bmt3.c1 AND bmt3.ctid = '(1,1)' AND bmt1.c1 = bmt4.c1 AND bmt4.ctid = '(1,1)'
AND bmt1.c1 = c1.c1
AND bmt1.c1 = c2.c1
;

-- No. L-2-1-8
EXPLAIN (COSTS false)
WITH c1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = b1t2.c1 AND b1t2.ctid = '(1,1)' AND b1t1.c1 = b1t3.c1 AND b1t3.ctid = '(1,1)' AND b1t1.c1 = b1t4.c1 AND b1t4.ctid = '(1,1)'
)
, c2 (c1) AS (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2, s1.t3 b2t3, s1.t4 b2t4 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = b2t2.c1 AND b2t2.ctid = '(1,1)' AND b2t1.c1 = b2t3.c1 AND b2t3.ctid = '(1,1)' AND b2t1.c1 = b2t4.c1 AND b2t4.ctid = '(1,1)'
)
, c3 (c1) AS (
SELECT max(b3t1.c1) FROM s1.t1 b3t1, s1.t2 b3t2, s1.t3 b3t3, s1.t4 b3t4 WHERE b3t1.ctid = '(1,1)' AND b3t1.c1 = b3t2.c1 AND b3t2.ctid = '(1,1)' AND b3t1.c1 = b3t3.c1 AND b3t3.ctid = '(1,1)' AND b3t1.c1 = b3t4.c1 AND b3t4.ctid = '(1,1)'
)
SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4
, c1, c2, c3
                                                                        WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = bmt3.c1 AND bmt3.ctid = '(1,1)' AND bmt1.c1 = bmt4.c1 AND bmt4.ctid = '(1,1)'
AND bmt1.c1 = c1.c1
AND bmt1.c1 = c2.c1
AND bmt1.c1 = c3.c1
;
/*+
Leading(c3 c2 c1 bmt1 bmt2 bmt3 bmt4 b1t2 b1t3 b1t4 b1t1 b2t3 b2t4 b2t1 b2t2 b3t4 b3t1 b3t2 b3t3)
*/
EXPLAIN (COSTS false)
WITH c1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = b1t2.c1 AND b1t2.ctid = '(1,1)' AND b1t1.c1 = b1t3.c1 AND b1t3.ctid = '(1,1)' AND b1t1.c1 = b1t4.c1 AND b1t4.ctid = '(1,1)'
)
, c2 (c1) AS (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2, s1.t3 b2t3, s1.t4 b2t4 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = b2t2.c1 AND b2t2.ctid = '(1,1)' AND b2t1.c1 = b2t3.c1 AND b2t3.ctid = '(1,1)' AND b2t1.c1 = b2t4.c1 AND b2t4.ctid = '(1,1)'
)
, c3 (c1) AS (
SELECT max(b3t1.c1) FROM s1.t1 b3t1, s1.t2 b3t2, s1.t3 b3t3, s1.t4 b3t4 WHERE b3t1.ctid = '(1,1)' AND b3t1.c1 = b3t2.c1 AND b3t2.ctid = '(1,1)' AND b3t1.c1 = b3t3.c1 AND b3t3.ctid = '(1,1)' AND b3t1.c1 = b3t4.c1 AND b3t4.ctid = '(1,1)'
)
SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4
, c1, c2, c3
                                                                        WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = bmt3.c1 AND bmt3.ctid = '(1,1)' AND bmt1.c1 = bmt4.c1 AND bmt4.ctid = '(1,1)'
AND bmt1.c1 = c1.c1
AND bmt1.c1 = c2.c1
AND bmt1.c1 = c3.c1
;

----
---- No. L-2-2 the number of the tables per quiry block
----

-- No. L-2-2-1
EXPLAIN (COSTS false)
WITH c1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = 1
)
SELECT max(bmt1.c1), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = 1
)
                    FROM s1.t1 bmt1, c1 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = 1
AND bmt1.c1 <> (
SELECT max(b3t1.c1) FROM s1.t1 b3t1 WHERE b3t1.ctid = '(1,1)' AND b3t1.c1 = 1
)
;
/*+
Leading(c1 bmt1 b1t1 b2t1 b3t1)
*/
EXPLAIN (COSTS false)
WITH c1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = 1
)
SELECT max(bmt1.c1), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = 1
)
                    FROM s1.t1 bmt1, c1 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = 1
AND bmt1.c1 <> (
SELECT max(b3t1.c1) FROM s1.t1 b3t1 WHERE b3t1.ctid = '(1,1)' AND b3t1.c1 = 1
)
;

-- No. L-2-2-2
EXPLAIN (COSTS false)
WITH c1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = b1t2.c1 AND b1t2.ctid = '(1,1)'
)
SELECT max(bmt1.c1), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = b2t2.c1 AND b2t2.ctid = '(1,1)'
)
                    FROM s1.t1 bmt1, s1.t2 bmt2, c1 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)'
AND bmt1.c1 = c1.c1
AND bmt1.c1 <> (
SELECT max(b3t1.c1) FROM s1.t1 b3t1, s1.t2 b3t2 WHERE b3t1.ctid = '(1,1)' AND b3t1.c1 = b3t2.c1 AND b3t2.ctid = '(1,1)'
)
;
/*+
Leading(c1 bmt2 b1t2 b2t2 b3t2 bmt1 b1t1 b2t1 b3t1)
*/
EXPLAIN (COSTS false)
WITH c1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = b1t2.c1 AND b1t2.ctid = '(1,1)'
)
SELECT max(bmt1.c1), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = b2t2.c1 AND b2t2.ctid = '(1,1)'
)
                    FROM s1.t1 bmt1, s1.t2 bmt2, c1 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)'
AND bmt1.c1 = c1.c1
AND bmt1.c1 <> (
SELECT max(b3t1.c1) FROM s1.t1 b3t1, s1.t2 b3t2 WHERE b3t1.ctid = '(1,1)' AND b3t1.c1 = b3t2.c1 AND b3t2.ctid = '(1,1)'
)
;

-- No. L-2-2-3
EXPLAIN (COSTS false)
WITH c1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = b1t2.c1 AND b1t2.ctid = '(1,1)' AND b1t1.c1 = b1t3.c1 AND b1t3.ctid = '(1,1)' AND b1t1.c1 = b1t4.c1 AND b1t4.ctid = '(1,1)'
)
SELECT max(bmt1.c1), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2, s1.t3 b2t3, s1.t4 b2t4 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = b2t2.c1 AND b2t2.ctid = '(1,1)' AND b2t1.c1 = b2t3.c1 AND b2t3.ctid = '(1,1)' AND b2t1.c1 = b2t4.c1 AND b2t4.ctid = '(1,1)'
)
                    FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4, c1 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = bmt3.c1 AND bmt3.ctid = '(1,1)' AND bmt1.c1 = bmt4.c1 AND bmt4.ctid = '(1,1)' AND bmt1.c1 = c1.c1
AND bmt1.c1 <> (
SELECT max(b3t1.c1) FROM s1.t1 b3t1, s1.t2 b3t2, s1.t3 b3t3, s1.t4 b3t4 WHERE b3t1.ctid = '(1,1)' AND b3t1.c1 = b3t2.c1 AND b3t2.ctid = '(1,1)' AND b3t1.c1 = b3t3.c1 AND b3t3.ctid = '(1,1)' AND b3t1.c1 = b3t4.c1 AND b3t4.ctid = '(1,1)'
)
;
/*+
Leading(c1 bmt4 b1t4 b2t4 b3t4 bmt3 b1t3 b2t3 b3t3 bmt2 b1t2 b2t2 b3t2 bmt1 b1t1 b2t1 b3t1)
*/
EXPLAIN (COSTS false)
WITH c1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = b1t2.c1 AND b1t2.ctid = '(1,1)' AND b1t1.c1 = b1t3.c1 AND b1t3.ctid = '(1,1)' AND b1t1.c1 = b1t4.c1 AND b1t4.ctid = '(1,1)'
)
SELECT max(bmt1.c1), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2, s1.t3 b2t3, s1.t4 b2t4 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = b2t2.c1 AND b2t2.ctid = '(1,1)' AND b2t1.c1 = b2t3.c1 AND b2t3.ctid = '(1,1)' AND b2t1.c1 = b2t4.c1 AND b2t4.ctid = '(1,1)'
)
                    FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4, c1 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = bmt3.c1 AND bmt3.ctid = '(1,1)' AND bmt1.c1 = bmt4.c1 AND bmt4.ctid = '(1,1)' AND bmt1.c1 = c1.c1
AND bmt1.c1 <> (
SELECT max(b3t1.c1) FROM s1.t1 b3t1, s1.t2 b3t2, s1.t3 b3t3, s1.t4 b3t4 WHERE b3t1.ctid = '(1,1)' AND b3t1.c1 = b3t2.c1 AND b3t2.ctid = '(1,1)' AND b3t1.c1 = b3t3.c1 AND b3t3.ctid = '(1,1)' AND b3t1.c1 = b3t4.c1 AND b3t4.ctid = '(1,1)'
)
;

-- No. L-2-2-4
EXPLAIN (COSTS false)
WITH c1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = b1t2.c1 AND b1t2.ctid = '(1,1)' AND b1t1.c1 = b1t3.c1 AND b1t3.ctid = '(1,1)' AND b1t1.c1 = b1t4.c1 AND b1t4.ctid = '(1,1)'
)
SELECT max(bmt1.c1), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = 1
)
                    FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4, c1 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = bmt3.c1 AND bmt3.ctid = '(1,1)' AND bmt1.c1 = bmt4.c1 AND bmt4.ctid = '(1,1)' AND bmt1.c1 = c1.c1
AND bmt1.c1 <> (
SELECT max(b3t1.c1) FROM s1.t1 b3t1 WHERE b3t1.ctid = '(1,1)'
)
;
/*+
Leading(c1 bmt4 b1t4 bmt3 b1t3 bmt2 b1t2 bmt1 b1t1 b2t1 b3t1)
*/
EXPLAIN (COSTS false)
WITH c1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = b1t2.c1 AND b1t2.ctid = '(1,1)' AND b1t1.c1 = b1t3.c1 AND b1t3.ctid = '(1,1)' AND b1t1.c1 = b1t4.c1 AND b1t4.ctid = '(1,1)'
)
SELECT max(bmt1.c1), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = 1
)
                    FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4, c1 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = bmt3.c1 AND bmt3.ctid = '(1,1)' AND bmt1.c1 = bmt4.c1 AND bmt4.ctid = '(1,1)' AND bmt1.c1 = c1.c1
AND bmt1.c1 <> (
SELECT max(b3t1.c1) FROM s1.t1 b3t1 WHERE b3t1.ctid = '(1,1)'
)
;

----
---- No. L-2-3 RULE definition table
----

-- No. L-2-3-1
EXPLAIN (COSTS false) UPDATE s1.r1 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+ Leading(t4 t3 t2 t1 r1) */
EXPLAIN (COSTS false) UPDATE s1.r1 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
EXPLAIN (COSTS false) UPDATE s1.r1_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+ Leading(b1t1 b1t2 b1t3 b1t4 r1_) */
EXPLAIN (COSTS false) UPDATE s1.r1_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. L-2-3-2
EXPLAIN (COSTS false) UPDATE s1.r2 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+ Leading(t4 t3 t2 t1 r2) */
EXPLAIN (COSTS false) UPDATE s1.r2 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
EXPLAIN (COSTS false) UPDATE s1.r2_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+Leading(
b1t1 b1t2 b1t3 b1t4 
b2t1 b2t2 b2t3 b2t4 r2_
) */
EXPLAIN (COSTS false) UPDATE s1.r2_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. L-2-3-3
EXPLAIN (COSTS false) UPDATE s1.r3 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+ Leading(t4 t3 t2 t1 r3) */
EXPLAIN (COSTS false) UPDATE s1.r3 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
EXPLAIN (COSTS false) UPDATE s1.r3_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+Leading(
b1t1 b1t2 b1t3 b1t4
b2t1 b2t2 b2t3 b2t4
b3t1 b3t2 b3t3 b3t4 r3_
) */
EXPLAIN (COSTS false) UPDATE s1.r3_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';

----
---- No. L-2-4 VALUES clause
----

-- No. L-2-4-1
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, (VALUES(1,1,1,'1')) AS t3 (c1, c2, c3, c4) WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1;
/*+ Leading(t3 t1 t2) */
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, (VALUES(1,1,1,'1')) AS t3 (c1, c2, c3, c4) WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1;
/*+ Leading(*VALUES* t1 t2) */
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, (VALUES(1,1,1,'1')) AS t3 (c1, c2, c3, c4) WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1;

-- No. L-2-4-2
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, (VALUES(1,1,1,'1')) AS t3 (c1, c2, c3, c4), (VALUES(1,1,1,'1'), (2,2,2,'2')) AS t4 (c1, c2, c3, c4) WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;
/*+ Leading(t4 t3 t2 t1) */
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, (VALUES(1,1,1,'1')) AS t3 (c1, c2, c3, c4), (VALUES(1,1,1,'1'), (2,2,2,'2')) AS t4 (c1, c2, c3, c4) WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;
/*+ Leading(*VALUES* t3 t2 t1) */
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, (VALUES(1,1,1,'1')) AS t3 (c1, c2, c3, c4), (VALUES(1,1,1,'1'), (2,2,2,'2')) AS t4 (c1, c2, c3, c4) WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;

----
---- No. L-3-1 leading the order of table joins
----

EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1;

-- No. L-3-1-1
/*+Leading(t3 t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1;

-- No. L-3-1-2
/*+Leading(t1 t2 t3)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1;

----
---- No. L-3-2 GUC parameter to disable hints
----

EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1;

-- No. L-3-2-1
Set geqo_threshold = 3;
Set geqo_seed = 0;
/*+Leading(t1 t2 t3)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1;
Reset geqo_threshold;

-- No. L-3-2-2
Set geqo_threshold = 4;
Set geqo_seed = 0;
/*+Leading(t1 t2 t3)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1;
Reset geqo_threshold;

-- No. L-3-2-3
Set from_collapse_limit = 2;
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.v2 WHERE t1.c1 = v2.c1;
/*+Leading(t1 v2t1 v2t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.v2 WHERE t1.c1 = v2.c1;
Reset from_collapse_limit;

-- No. L-3-2-4
Set from_collapse_limit = 3;
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.v2 WHERE t1.c1 = v2.c1;
/*+Leading(v2t1 v2t2 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.v2 WHERE t1.c1 = v2.c1;
Reset from_collapse_limit;

-- No. L-3-2-5
Set join_collapse_limit = 2;
EXPLAIN (COSTS false) SELECT * FROM s1.t3
  JOIN s1.t2 ON (t3.c1 = t2.c1)
  JOIN s1.t1 ON (t1.c1 = t3.c1);
/*+Leading(t1 t2 t3)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t3
  JOIN s1.t2 ON (t3.c1 = t2.c1)
  JOIN s1.t1 ON (t1.c1 = t3.c1);
Reset join_collapse_limit;

-- No. L-3-2-6
Set join_collapse_limit = 3;
EXPLAIN (COSTS false) SELECT * FROM s1.t3
  JOIN s1.t2 ON (t3.c1 = t2.c1)
  JOIN s1.t1 ON (t1.c1 = t3.c1);
/*+Leading(t1 t2 t3)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t3
  JOIN s1.t2 ON (t3.c1 = t2.c1)
  JOIN s1.t1 ON (t1.c1 = t3.c1);
Reset join_collapse_limit;

----
---- No. L-3-3 join between parents or between children
----

-- No. L-3-3-1
/*+Leading(t1 t2 t3)*/
EXPLAIN (COSTS false) SELECT * FROM s1.p2c1 t1
  JOIN s1.p2c2 t2 ON (t1.c1 = t2.c1)
  JOIN s1.p2c3 t3 ON (t1.c1 = t3.c1);

-- No. L-3-3-2
/*+Leading(p2c1c1 p2c2c1 p2c3c1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.p2c1 t1
  JOIN s1.p2c2 t2 ON (t1.c1 = t2.c1)
  JOIN s1.p2c3 t3 ON (t1.c1 = t3.c1);
