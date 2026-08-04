LOAD 'pg_hint_plan';
SET pg_hint_plan.enable_hint TO on;
SET pg_hint_plan.debug_print TO on;
SET client_min_messages TO LOG;
SET search_path TO public;
SET max_parallel_workers_per_gather TO 0;

EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

----
---- No. J-1-1 specified pattern of the object name
----

-- No. J-1-1-1
/*+HashJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

-- No. J-1-1-2
/*+HashJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 t_1, s1.t2 t_2 WHERE t_1.c1 = t_2.c1;

-- No. J-1-1-3
/*+HashJoin(t_1 t_2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 t_1, s1.t2 t_2 WHERE t_1.c1 = t_2.c1;


----
---- No. J-1-2 specified schema name in the hint option
----

-- No. J-1-2-1
/*+HashJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

-- No. J-1-2-2
/*+HashJoin(s1.t1 s1.t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

----
---- No. J-1-3 table doesn't exist in the hint option
----

-- No. J-1-3-1
/*+HashJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

-- No. J-1-3-2
/*+HashJoin(t3 t4)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

----
---- No. J-1-4 conflict table name
----

-- No. J-1-4-1
/*+HashJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

-- No. J-1-4-2
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s2.t1 WHERE s1.t1.c1 = s2.t1.c1;
/*+HashJoin(t1 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s2.t1 WHERE s1.t1.c1 = s2.t1.c1;
/*+HashJoin(s1.t1 s2.t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s2.t1 WHERE s1.t1.c1 = s2.t1.c1;

EXPLAIN (COSTS false) SELECT * FROM s1.t1, s2.t1 s2t1 WHERE s1.t1.c1 = s2t1.c1;
/*+HashJoin(t1 s2t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s2.t1 s2t1 WHERE s1.t1.c1 = s2t1.c1;

-- No. J-1-4-3
EXPLAIN (COSTS false) SELECT *, (SELECT max(t1.c1) FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1) FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
/*+HashJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT *, (SELECT max(t1.c1) FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1) FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
/*+NestLoop(st1 st2)HashJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT *, (SELECT max(st1.c1) FROM s1.t1 st1, s1.t2 st2 WHERE st1.c1 = st2.c1) FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

----
---- No. J-1-5 conflict table name
----

-- No. J-1-5-1
/*+HashJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

-- No. J-1-5-2
/*+HashJoin(t1 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

-- No. J-1-5-3
/*+HashJoin(t1 t1)HashJoin(t2 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1;
/*+HashJoin(t1 t2 t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, s1.t3, s1.t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;

----
---- No. J-1-6 object type for the hint
----

-- No. J-1-6-1
/*+HashJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

-- No. J-1-6-2
EXPLAIN (COSTS false) SELECT * FROM s1.p1 t1, s1.p1 t2 WHERE t1.c1 = t2.c1;
/*+NestLoop(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.p1 t1, s1.p1 t2 WHERE t1.c1 = t2.c1;

-- No. J-1-6-3
EXPLAIN (COSTS false) SELECT * FROM s1.ul1 t1, s1.ul1 t2 WHERE t1.c1 = t2.c1;
/*+NestLoop(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ul1 t1, s1.ul1 t2 WHERE t1.c1 = t2.c1;

-- No. J-1-6-4
CREATE TEMP TABLE tm1 (LIKE s1.t1 INCLUDING ALL);
EXPLAIN (COSTS false) SELECT * FROM tm1 t1, tm1 t2 WHERE t1.c1 = t2.c1;
/*+NestLoop(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM tm1 t1, tm1 t2 WHERE t1.c1 = t2.c1;

-- No. J-1-6-5
EXPLAIN (COSTS false) SELECT * FROM pg_catalog.pg_class t1, pg_catalog.pg_class t2 WHERE t1.oid = t2.oid;
/*+NestLoop(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM pg_catalog.pg_class t1, pg_catalog.pg_class t2 WHERE t1.oid = t2.oid;

-- No. J-1-6-6
-- refer ut-fdw.sql

-- No. J-1-6-7
EXPLAIN (COSTS false) SELECT * FROM s1.f1() t1, s1.f1() t2 WHERE t1.c1 = t2.c1;
/*+HashJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.f1() t1, s1.f1() t2 WHERE t1.c1 = t2.c1;

-- No. J-1-6-8
EXPLAIN (COSTS false) SELECT * FROM (VALUES(1,1,1,'1'), (2,2,2,'2'), (3,3,3,'3')) AS t1 (c1, c2, c3, c4),  s1.t2 WHERE t1.c1 = t2.c1;
/*+NestLoop(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM (VALUES(1,1,1,'1'), (2,2,2,'2'), (3,3,3,'3')) AS t1 (c1, c2, c3, c4),  s1.t2 WHERE t1.c1 = t2.c1;
/*+NestLoop(*VALUES* t2)*/
EXPLAIN (COSTS false) SELECT * FROM (VALUES(1,1,1,'1'), (2,2,2,'2'), (3,3,3,'3')) AS t1 (c1, c2, c3, c4),  s1.t2 WHERE t1.c1 = t2.c1;

-- No. J-1-6-9
EXPLAIN (COSTS false) WITH c1(c1) AS (SELECT max(t1.c1) FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1) SELECT * FROM s1.t1, c1 WHERE t1.c1 = c1.c1;
/*+NestLoop(t1 t2)HashJoin(t1 c1)*/
EXPLAIN (COSTS false) WITH c1(c1) AS (SELECT max(t1.c1) FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1) SELECT * FROM s1.t1, c1 WHERE t1.c1 = c1.c1;

-- No. J-1-6-10
EXPLAIN (COSTS false) SELECT * FROM s1.v1 t1, s1.v1 t2 WHERE t1.c1 = t2.c1;
/*+NestLoop(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.v1 t1, s1.v1 t2 WHERE t1.c1 = t2.c1;
/*+NestLoop(v1t1 v1t1_)*/
EXPLAIN (COSTS false) SELECT * FROM s1.v1 t1, s1.v1_ t2 WHERE t1.c1 = t2.c1;

-- No. J-1-6-11
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1 AND t1.c1 = (SELECT max(st1.c1) FROM s1.t1 st1, s1.t2 st2 WHERE st1.c1 = st2.c1);

\o results/ut-J.tmpout
/*+MergeJoin(t1 t2)NestLoop(st1 st2)*/
EXPLAIN (COSTS true) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1 AND t1.c1 = (SELECT max(st1.c1) FROM s1.t1 st1, s1.t2 st2 WHERE st1.c1 = st2.c1);
\o
\! sql/maskout.sh results/ut-J.tmpout

--
-- There are cases where difference in the measured value and predicted value
-- depending upon the version of PostgreSQL
--

EXPLAIN (COSTS false) SELECT * FROM s1.t1, (SELECT t2.c1 FROM s1.t2) st2 WHERE t1.c1 = st2.c1;
/*+HashJoin(t1 st2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, (SELECT t2.c1 FROM s1.t2) st2 WHERE t1.c1 = st2.c1;
/*+HashJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, (SELECT t2.c1 FROM s1.t2) st2 WHERE t1.c1 = st2.c1;

----
---- No. J-2-1 some complexity query blocks
----

-- No. J-2-1-1
EXPLAIN (COSTS false)
SELECT max(bmt1.c1), (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = b1t2.c1 AND b1t2.ctid = '(1,1)' AND b1t1.c1 = b1t3.c1 AND b1t3.ctid = '(1,1)' AND b1t1.c1 = b1t4.c1 AND b1t4.ctid = '(1,1)'
), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2, s1.t3 b2t3, s1.t4 b2t4 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = b2t2.c1 AND b2t2.ctid = '(1,1)' AND b2t1.c1 = b2t3.c1 AND b2t3.ctid = '(1,1)' AND b2t1.c1 = b2t4.c1 AND b2t4.ctid = '(1,1)'
)
                    FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = bmt3.c1 AND bmt3.ctid = '(1,1)' AND bmt1.c1 = bmt4.c1 AND bmt4.ctid = '(1,1)'
;
/*+
Leading(bmt1 bmt2 bmt3 bmt4)
Leading(b1t2 b1t3 b1t4 b1t1)
Leading(b2t3 b2t4 b2t1 b2t2)
MergeJoin(bmt1 bmt2)HashJoin(bmt1 bmt2 bmt3)NestLoop(bmt1 bmt2 bmt3 bmt4)
MergeJoin(b1t2 b1t3)HashJoin(b1t2 b1t3 b1t4)NestLoop(b1t2 b1t3 b1t4 b1t1)
MergeJoin(b2t3 b2t4)HashJoin(b2t3 b2t4 b2t1)NestLoop(b2t3 b2t4 b2t1 b2t2)
*/
EXPLAIN (COSTS false)
SELECT max(bmt1.c1), (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = b1t2.c1 AND b1t2.ctid = '(1,1)' AND b1t1.c1 = b1t3.c1 AND b1t3.ctid = '(1,1)' AND b1t1.c1 = b1t4.c1 AND b1t4.ctid = '(1,1)'
), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2, s1.t3 b2t3, s1.t4 b2t4 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = b2t2.c1 AND b2t2.ctid = '(1,1)' AND b2t1.c1 = b2t3.c1 AND b2t3.ctid = '(1,1)' AND b2t1.c1 = b2t4.c1 AND b2t4.ctid = '(1,1)'
)
                    FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = bmt3.c1 AND bmt3.ctid = '(1,1)' AND bmt1.c1 = bmt4.c1 AND bmt4.ctid = '(1,1)'
;

-- No. J-2-1-2
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
Leading(bmt1 bmt2 bmt3 bmt4)
Leading(b1t2 b1t3 b1t4 b1t1)
Leading(b2t3 b2t4 b2t1 b2t2)
Leading(b3t4 b3t1 b3t2 b3t3)
MergeJoin(bmt1 bmt2)HashJoin(bmt1 bmt2 bmt3)NestLoop(bmt1 bmt2 bmt3 bmt4)
MergeJoin(b1t2 b1t3)HashJoin(b1t2 b1t3 b1t4)NestLoop(b1t2 b1t3 b1t4 b1t1)
MergeJoin(b2t3 b2t4)HashJoin(b2t3 b2t4 b2t1)NestLoop(b2t3 b2t4 b2t1 b2t2)
MergeJoin(b3t4 b3t1)HashJoin(b3t4 b3t1 b3t2)NestLoop(b3t1 b3t2 b3t3 b3t4)
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

-- No. J-2-1-3
EXPLAIN (COSTS false) SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, (SELECT ctid, * FROM s1.t3 bmt3) sbmt3, (SELECT ctid, * FROM s1.t4 bmt4) sbmt4 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = sbmt3.c1 AND sbmt3.ctid = '(1,1)' AND bmt1.c1 = sbmt4.c1 AND sbmt4.ctid = '(1,1)';
/*+
Leading(bmt4 bmt3 bmt2 bmt1)
MergeJoin(bmt4 bmt3)HashJoin(bmt4 bmt3 bmt2)NestLoop(bmt1 bmt2 bmt3 bmt4)
*/
EXPLAIN (COSTS false) SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, (SELECT ctid, * FROM s1.t3 bmt3) sbmt3, (SELECT ctid, * FROM s1.t4 bmt4) sbmt4 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = sbmt3.c1 AND sbmt3.ctid = '(1,1)' AND bmt1.c1 = sbmt4.c1 AND sbmt4.ctid = '(1,1)';

-- No. J-2-1-4
EXPLAIN (COSTS false) SELECT max(bmt1.c1) FROM s1.t1 bmt1, (SELECT ctid, * FROM s1.t2 bmt2) sbmt2, (SELECT ctid, * FROM s1.t3 bmt3) sbmt3, (SELECT ctid, * FROM s1.t4 bmt4) sbmt4 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = sbmt2.c1 AND sbmt2.ctid = '(1,1)' AND bmt1.c1 = sbmt3.c1 AND sbmt3.ctid = '(1,1)' AND bmt1.c1 = sbmt4.c1 AND sbmt4.ctid = '(1,1)';
/*+
Leading(bmt4 bmt3 bmt2 bmt1)
MergeJoin(bmt4 bmt3)HashJoin(bmt4 bmt3 bmt2)NestLoop(bmt1 bmt2 bmt3 bmt4)
*/
EXPLAIN (COSTS false) SELECT max(bmt1.c1) FROM s1.t1 bmt1, (SELECT ctid, * FROM s1.t2 bmt2) sbmt2, (SELECT ctid, * FROM s1.t3 bmt3) sbmt3, (SELECT ctid, * FROM s1.t4 bmt4) sbmt4 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = sbmt2.c1 AND sbmt2.ctid = '(1,1)' AND bmt1.c1 = sbmt3.c1 AND sbmt3.ctid = '(1,1)' AND bmt1.c1 = sbmt4.c1 AND sbmt4.ctid = '(1,1)';

-- No. J-2-1-5
EXPLAIN (COSTS false)
SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = bmt3.c1 AND bmt3.ctid = '(1,1)' AND bmt1.c1 = bmt4.c1 AND bmt4.ctid = '(1,1)'
  AND bmt1.c1 <> (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = b1t2.c1 AND b1t2.ctid = '(1,1)' AND b1t1.c1 = b1t3.c1 AND b1t3.ctid = '(1,1)' AND b1t1.c1 = b1t4.c1 AND b1t4.ctid = '(1,1)'
) AND bmt1.c1 <> (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2, s1.t3 b2t3, s1.t4 b2t4 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = b2t2.c1 AND b2t2.ctid = '(1,1)' AND b2t1.c1 = b2t3.c1 AND b2t3.ctid = '(1,1)' AND b2t1.c1 = b2t4.c1 AND b2t4.ctid = '(1,1)'
)
;
/*+
Leading(bmt1 bmt2 bmt3 bmt4)
Leading(b1t2 b1t3 b1t4 b1t1)
Leading(b2t3 b2t4 b2t1 b2t2)
MergeJoin(bmt1 bmt2)HashJoin(bmt1 bmt2 bmt3)NestLoop(bmt1 bmt2 bmt3 bmt4)
MergeJoin(b1t2 b1t3)HashJoin(b1t2 b1t3 b1t4)NestLoop(b1t2 b1t3 b1t4 b1t1)
MergeJoin(b2t3 b2t4)HashJoin(b2t3 b2t4 b2t1)NestLoop(b2t3 b2t4 b2t1 b2t2)
*/
EXPLAIN (COSTS false)
SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = bmt3.c1 AND bmt3.ctid = '(1,1)' AND bmt1.c1 = bmt4.c1 AND bmt4.ctid = '(1,1)'
  AND bmt1.c1 <> (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = b1t2.c1 AND b1t2.ctid = '(1,1)' AND b1t1.c1 = b1t3.c1 AND b1t3.ctid = '(1,1)' AND b1t1.c1 = b1t4.c1 AND b1t4.ctid = '(1,1)'
) AND bmt1.c1 <> (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2, s1.t3 b2t3, s1.t4 b2t4 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = b2t2.c1 AND b2t2.ctid = '(1,1)' AND b2t1.c1 = b2t3.c1 AND b2t3.ctid = '(1,1)' AND b2t1.c1 = b2t4.c1 AND b2t4.ctid = '(1,1)'
)
;

-- No. J-2-1-6
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
Leading(bmt1 bmt2 bmt3 bmt4)
Leading(b1t2 b1t3 b1t4 b1t1)
Leading(b2t3 b2t4 b2t1 b2t2)
Leading(b3t4 b3t1 b3t2 b3t3)
MergeJoin(bmt1 bmt2)HashJoin(bmt1 bmt2 bmt3)NestLoop(bmt1 bmt2 bmt3 bmt4)
MergeJoin(b1t2 b1t3)HashJoin(b1t2 b1t3 b1t4)NestLoop(b1t2 b1t3 b1t4 b1t1)
MergeJoin(b2t3 b2t4)HashJoin(b2t3 b2t4 b2t1)NestLoop(b2t3 b2t4 b2t1 b2t2)
MergeJoin(b3t4 b3t1)HashJoin(b3t4 b3t1 b3t2)NestLoop(b3t1 b3t2 b3t3 b3t4)
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

-- No. J-2-1-7
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
Leading(c2 c1 bmt1 bmt2 bmt3 bmt4)
Leading(b1t2 b1t3 b1t4 b1t1)
Leading(b2t3 b2t4 b2t1 b2t2)
MergeJoin(c2 c1)HashJoin(c2 c1 bmt1)NestLoop(c2 c1 bmt1 bmt2)MergeJoin(c2 c1 bmt1 bmt2 bmt3)HashJoin(c2 c1 bmt1 bmt2 bmt3 bmt4)
MergeJoin(b1t2 b1t3)HashJoin(b1t2 b1t3 b1t4)NestLoop(b1t2 b1t3 b1t4 b1t1)
MergeJoin(b2t3 b2t4)HashJoin(b2t3 b2t4 b2t1)NestLoop(b2t3 b2t4 b2t1 b2t2)
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

-- No. J-2-1-8
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
Leading(c3 c2 c1 bmt1 bmt2 bmt3 bmt4)
Leading(b1t2 b1t3 b1t4 b1t1)
Leading(b2t3 b2t4 b2t1 b2t2)
Leading(b3t4 b3t1 b3t2 b3t3)
MergeJoin(c3 c2)HashJoin(c3 c2 c1)NestLoop(c3 c2 c1 bmt1)MergeJoin(c3 c2 c1 bmt1 bmt2)HashJoin(c3 c2 c1 bmt1 bmt2 bmt3)NestLoop(c3 c2 c1 bmt1 bmt2 bmt3 bmt4)
MergeJoin(b1t2 b1t3)HashJoin(b1t2 b1t3 b1t4)NestLoop(b1t2 b1t3 b1t4 b1t1)
MergeJoin(b2t3 b2t4)HashJoin(b2t3 b2t4 b2t1)NestLoop(b2t3 b2t4 b2t1 b2t2)
MergeJoin(b3t4 b3t1)HashJoin(b3t4 b3t1 b3t2)NestLoop(b3t1 b3t2 b3t3 b3t4)
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
---- No. J-2-2 the number of the tables per quiry block
----

-- No. J-2-2-1
EXPLAIN (COSTS false)
WITH c1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = 1
)
SELECT max(bmt1.c1), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = 1
)
                    FROM s1.t1 bmt1, c1 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = 1
AND bmt1.c1 = c1.c1
AND bmt1.c1 <> (
SELECT max(b3t1.c1) FROM s1.t1 b3t1 WHERE b3t1.ctid = '(1,1)' AND b3t1.c1 = 1
)
;
/*+
Leading(c1 bmt1)
HashJoin(bmt1 c1)
HashJoin(b1t1 c1)
HashJoin(b2t1 c1)
HashJoin(b3t1 c1)
*/
EXPLAIN (COSTS false)
WITH c1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = 1
)
SELECT max(bmt1.c1), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = 1
)
                    FROM s1.t1 bmt1, c1 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = 1
AND bmt1.c1 = c1.c1
AND bmt1.c1 <> (
SELECT max(b3t1.c1) FROM s1.t1 b3t1 WHERE b3t1.ctid = '(1,1)' AND b3t1.c1 = 1
)
;

-- No. J-2-2-2
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
Leading(c1 bmt2 bmt1)
Leading(b1t2 b1t1)
Leading(b2t2 b2t1)
Leading(b3t2 b3t1)
MergeJoin(c1 bmt2)
HashJoin(c1 bmt1 bmt2)
MergeJoin(b1t1 b1t2)
MergeJoin(b2t1 b2t2)
MergeJoin(b3t1 b3t2)
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

-- No. J-2-2-3
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
Leading(c1 bmt4 bmt3 bmt2 bmt1)
Leading(b1t4 b1t3 b1t2 b1t1) 
Leading(b2t4 b2t3 b2t2 b2t1)
Leading(b3t4 b3t3 b3t2 b3t1)
MergeJoin(c1 bmt4)
HashJoin(c1 bmt4 bmt3)
NestLoop(c1 bmt4 bmt3 bmt2)
MergeJoin(c1 bmt4 bmt3 bmt2 bmt1)
HashJoin(b1t4 b1t3)
NestLoop(b1t4 b1t3 b1t2)
MergeJoin(b1t4 b1t3 b1t2 b1t1)
HashJoin(b2t4 b2t3)
NestLoop(b2t4 b2t3 b2t2)
MergeJoin(b2t4 b2t3 b2t2 b2t1)
HashJoin(b3t4 b3t3)
NestLoop(b3t4 b3t3 b3t2)
MergeJoin(b3t4 b3t3 b3t2 b3t1)
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

-- No. J-2-2-4
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
Leading(c1 bmt4 bmt3 bmt2 bmt1)
Leading(b1t4 b1t3 b1t2 b1t1)
MergeJoin(c1 bmt4)
HashJoin(c1 bmt4 bmt3)
NestLoop(c1 bmt4 bmt3 bmt2)
MergeJoin(c1 bmt4 bmt3 bmt2 bmt1)
MergeJoin(b1t4 b1t3)
HashJoin(b1t4 b1t3 b1t2)
NestLoop(b1t4 b1t3 b1t2 b1t1)
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
---- No. J-2-3 RULE or VIEW
----

-- No. J-2-3-1
EXPLAIN (COSTS false) UPDATE s1.r1 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+
Leading(t4 t3 t2 t1 r1)
MergeJoin(t4 t3 t2 t1 r1)
HashJoin(t4 t3 t2 t1)
NestLoop(t4 t3 t2)
MergeJoin(t4 t3)
*/
EXPLAIN (COSTS false) UPDATE s1.r1 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
EXPLAIN (COSTS false) UPDATE s1.r1_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+
Leading(b1t4 b1t3 b1t2 b1t1 r1_)
MergeJoin(b1t4 b1t3 b1t2 b1t1 r1_)
HashJoin(b1t4 b1t3 b1t2 b1t1)
NestLoop(b1t4 b1t3 b1t2)
MergeJoin(b1t4 b1t3)
*/
EXPLAIN (COSTS false) UPDATE s1.r1_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. J-2-3-2
EXPLAIN (COSTS false) UPDATE s1.r2 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+
Leading(t4 t3 t2 t1 r2)
MergeJoin(t4 t3 t2 t1 r2)
HashJoin(t4 t3 t2 t1)
NestLoop(t4 t3 t2)
MergeJoin(t4 t3)
*/
EXPLAIN (COSTS false) UPDATE s1.r2 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
EXPLAIN (COSTS false) UPDATE s1.r2_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+
Leading(b1t1 b1t2 b1t3 b1t4 r2_)
Leading(b2t1 b2t2 b2t3 b2t4 r2_)
MergeJoin(b1t1 b1t2)
HashJoin(b1t1 b1t2 b1t3)
NestLoop(b1t1 b1t2 b1t3 b1t4)
MergeJoin(b1t1 b1t2 b1t3 b1t4 r2_)
MergeJoin(b2t1 b2t2)
HashJoin(b2t1 b2t2 b2t3)
NestLoop(b2t1 b2t2 b2t3 b2t4)
MergeJoin(b2t1 b2t2 b2t3 b2t4 r2_)
*/
EXPLAIN (COSTS false) UPDATE s1.r2_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. J-2-3-3
EXPLAIN (COSTS false) UPDATE s1.r3 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+
Leading(t4 t3 t2 t1 r3)
MergeJoin(t4 t3 t2 t1 r3)
HashJoin(t4 t3 t2 t1)
NestLoop(t4 t3 t2)
MergeJoin(t4 t3)
*/
EXPLAIN (COSTS false) UPDATE s1.r3 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
EXPLAIN (COSTS false) UPDATE s1.r3_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+
Leading(b1t1 b1t2 b1t3 b1t4 r3_)
Leading(b2t1 b2t2 b2t3 b2t4 r3_)
Leading(b3t1 b3t2 b3t3 b3t4 r3_)
MergeJoin(b1t1 b1t2)
HashJoin(b1t1 b1t2 b1t3)
NestLoop(b1t1 b1t2 b1t3 b1t4)
MergeJoin(b1t1 b1t2 b1t3 b1t4 r3_)
MergeJoin(b2t1 b2t2)
HashJoin(b2t1 b2t2 b2t3)
NestLoop(b2t1 b2t2 b2t3 b2t4)
MergeJoin(b2t1 b2t2 b2t3 b2t4 r3_)
MergeJoin(b3t1 b3t2)
HashJoin(b3t1 b3t2 b3t3)
NestLoop(b3t1 b3t2 b3t3 b3t4)
MergeJoin(b3t1 b3t2 b3t3 b3t4 r3_)
*/
EXPLAIN (COSTS false) UPDATE s1.r3_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. J-2-3-4
EXPLAIN (COSTS false) SELECT * FROM s1.v1 v1, s1.v1 v2 WHERE v1.c1 = v2.c1;
/*+HashJoin(v1t1 v1t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.v1 v1, s1.v1 v2 WHERE v1.c1 = v2.c1;

-- No. J-2-3-5
EXPLAIN (COSTS false) SELECT * FROM s1.v1 v1, s1.v1_ v2 WHERE v1.c1 = v2.c1;
/*+NestLoop(v1t1 v1t1_)*/
EXPLAIN (COSTS false) SELECT * FROM s1.v1 v1, s1.v1_ v2 WHERE v1.c1 = v2.c1;

-- No. J-2-3-6
EXPLAIN (COSTS false) SELECT * FROM s1.r4 t1, s1.r4 t2 WHERE t1.c1 = t2.c1;
/*+HashJoin(r4t1 r4t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.r4 t1, s1.r4 t2 WHERE t1.c1 = t2.c1;

-- No. J-2-3-7
EXPLAIN (COSTS false) SELECT * FROM s1.r4 t1, s1.r5 t2 WHERE t1.c1 = t2.c1;
/*+NestLoop(r4t1 r5t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.r4 t1, s1.r5 t2 WHERE t1.c1 = t2.c1;

----
---- No. J-2-4 VALUES clause
----

-- No. J-2-4-1
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, (VALUES(1,1,1,'1')) AS t3 (c1, c2, c3, c4) WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1;
/*+ Leading(t3 t1 t2) HashJoin(t3 t1)NestLoop(t3 t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, (VALUES(1,1,1,'1')) AS t3 (c1, c2, c3, c4) WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1;
/*+ Leading(*VALUES* t1 t2) HashJoin(*VALUES* t1)NestLoop(*VALUES* t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, (VALUES(1,1,1,'1'), (2,2,2,'2')) AS t3 (c1, c2, c3, c4) WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1;

-- No. J-2-4-2
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, (VALUES(1,1,1,'1'), (2,2,2,'2')) AS t3 (c1, c2, c3, c4), (VALUES(1,1,1,'1'), (2,2,2,'2')) AS t4 (c1, c2, c3, c4) WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;
/*+ Leading(t4 t3 t2 t1) NestLoop(t4 t3)HashJoin(t4 t3 t2)MergeJoin(t4 t3 t2 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, (VALUES(1,1,1,'1'), (2,2,2,'2')) AS t3 (c1, c2, c3, c4), (VALUES(1,1,1,'1'), (2,2,2,'2')) AS t4 (c1, c2, c3, c4) WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;
/*+ Leading(*VALUES* t3 t2 t1) NestLoop(t4 t3)HashJoin(*VALUES* t3 t2)MergeJoin(*VALUES* t3 t2 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2, (VALUES(1,1,1,'1'), (2,2,2,'2')) AS t3 (c1, c2, c3, c4), (VALUES(1,1,1,'1'), (2,2,2,'2')) AS t4 (c1, c2, c3, c4) WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;

----
---- No. J-3-1 join method hint
----

-- No. J-3-1-1~6
SET enable_nestloop TO on;
SET enable_mergejoin TO off;
SET enable_hashjoin TO off;
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
/*+NestLoop(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
/*+HashJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
/*+MergeJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
SET enable_mergejoin TO on;
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1 AND t1.ctid = '(1,1)';;
/*+NoNestLoop(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1 AND t1.ctid = '(1,1)';;
SET enable_mergejoin TO off;
/*+NoHashJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
/*+NoMergeJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

-- No. J-3-1-7~12
SET enable_nestloop TO off;
SET enable_mergejoin TO off;
SET enable_hashjoin TO on;
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
/*+NestLoop(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
/*+HashJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
/*+MergeJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
/*+NoNestLoop(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
/*+NoHashJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
/*+NoMergeJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

-- No. J-3-1-13~18
SET enable_nestloop TO off;
SET enable_mergejoin TO on;
SET enable_hashjoin TO off;
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
/*+NestLoop(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
/*+HashJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
/*+MergeJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
/*+NoNestLoop(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
/*+NoHashJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
/*+NoMergeJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

SET enable_nestloop TO on;
SET enable_mergejoin TO on;
SET enable_hashjoin TO on;

----
---- No. J-3-2 join inherit tables
----
EXPLAIN (COSTS false) SELECT * FROM s1.p1, s1.p2 WHERE p1.c1 = p2.c1;

-- No. J-3-2-1
/*+MergeJoin(p1 p2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.p1, s1.p2 WHERE p1.c1 = p2.c1;

-- No. J-3-2-2
/*+MergeJoin(p1c1 p2c1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.p1, s1.p2 WHERE p1.c1 = p2.c1;

----
---- No. J-3-2-2 join partitioned tables
----
EXPLAIN (COSTS false) SELECT * FROM s1.pt1, s1.p2 WHERE pt1.c1 = p2.c1;

/*+MergeJoin(pt1 p2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.pt1, s1.p2 WHERE pt1.c1 = p2.c1;

/*+MergeJoin(pt1_c1 p2c1)*/ /* will ignored */
EXPLAIN (COSTS false) SELECT * FROM s1.pt1, s1.p2 WHERE pt1.c1 = p2.c1;

----
---- No. J-3-3 conflict join method hint
----
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

-- No. J-3-3-1
/*+HashJoin(t1 t2)NestLoop(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

-- No. J-3-3-2
/*+MergeJoin(t1 t2)HashJoin(t1 t2)NestLoop(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

-- No. J-3-3-3
/*+HashJoin(t1 t2)NestLoop(t2 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

-- No. J-3-3-4
/*+MergeJoin(t2 t1)HashJoin(t1 t2)NestLoop(t2 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

----
---- No. J-3-4 hint state output
----

-- No. J-3-4-1
/*+NestLoop(t1 t2)*/
SELECT * FROM s1.t1, s1.t2 WHERE false;

-- No. J-3-4-2
/*+HashJoin(t1 t2)*/
SELECT * FROM s1.t1, s1.t2 WHERE false;

-- No. J-3-4-3
/*+MergeJoin(t1 t2)*/
SELECT * FROM s1.t1, s1.t2 WHERE false;

-- No. J-3-4-4
/*+NoNestLoop(t1 t2)*/
SELECT * FROM s1.t1, s1.t2 WHERE false;

-- No. J-3-4-5
/*+NoHashJoin(t1 t2)*/
SELECT * FROM s1.t1, s1.t2 WHERE false;

-- No. J-3-4-6
/*+NoMergeJoin(t1 t2)*/
SELECT * FROM s1.t1, s1.t2 WHERE false;

-- No. J-3-4-7
/*+NestLoop()*/
SELECT * FROM s1.t1 WHERE false;

-- No. J-3-4-8
/*+NestLoop(t1)*/
SELECT * FROM s1.t1 WHERE false;

-- No. J-3-4-9
/*+NestLoop(t1 t2)*/
SELECT * FROM s1.t1, s1.t2 WHERE false;

-- No. J-3-4-10
/*+NestLoop(t1 t2 t3)*/
SELECT * FROM s1.t1, s1.t2, s1.t3 WHERE false;

----
---- No. J-3-5 not used hint
----
-- No. J-3-5-1
EXPLAIN (COSTS false) SELECT * FROM s1.t1 FULL OUTER JOIN s1.t2 ON (t1.c1 = t2.c1);
\o results/ut-J.tmpout
/*+NestLoop(t1 t2)*/
EXPLAIN (COSTS true) SELECT * FROM s1.t1 FULL OUTER JOIN s1.t2 ON (t1.c1 = t2.c1);
\o
\! sql/maskout.sh results/ut-J.tmpout
\! rm results/ut-J.tmpout

-- Memoize
EXPLAIN (COSTS false) SELECT * FROM t1, t2, t3 WHERE t1.val = t2.val and t2.id = t3.id;
/*+ nomemoize(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2, t3 WHERE t1.val = t2.val and t2.id = t3.id;  -- doesn't work
/*+ nomemoize(t1 t2 t3)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2, t3 WHERE t1.val = t2.val and t2.id = t3.id;
