LOAD 'pg_hint_plan';
SET pg_hint_plan.enable_hint TO on;
SET pg_hint_plan.debug_print TO on;
SET client_min_messages TO LOG;
SET search_path TO public;

\o results/R_sample.out.log
EXPLAIN SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_sample.out.log > results/R_sample.out
\! diff expected/R_sample.out results/R_sample.out

----
---- No. R-1-1 specified pattern of the object name
----

-- No. R-1-1-1
\o results/R_1-1-1.out.log
/*+Rows(t1 t2 #1)*/
EXPLAIN SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-1-1.out.log > results/R_1-1-1.out
\! diff expected/R_1-1-1.out results/R_1-1-1.out

-- No. R-1-1-2
\o results/R_1-1-2.out.log
/*+Rows(t1 t2 #1)*/
EXPLAIN SELECT * FROM s1.t1 t_1, s1.t2 t_2 WHERE t_1.c1 = t_2.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-1-2.out.log > results/R_1-1-2.out
\! diff expected/R_1-1-2.out results/R_1-1-2.out

-- No. R-1-1-3
\o results/R_1-1-3.out.log
/*+Rows(t_1 t_2 #1)*/
EXPLAIN SELECT * FROM s1.t1 t_1, s1.t2 t_2 WHERE t_1.c1 = t_2.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-1-3.out.log > results/R_1-1-3.out
\! diff expected/R_1-1-3.out results/R_1-1-3.out


----
---- No. R-1-2 specified schema name in the hint option
----

-- No. R-1-2-1
\o results/R_1-2-1.out.log
/*+Rows(t1 t2 #1)*/
EXPLAIN SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-2-1.out.log > results/R_1-2-1.out
\! diff expected/R_1-2-1.out results/R_1-2-1.out

-- No. R-1-2-2
\o results/R_1-2-2.out.log
/*+Rows(s1.t1 s1.t2 #1)*/
EXPLAIN SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-2-2.out.log > results/R_1-2-2.out
\! diff expected/R_1-2-2.out results/R_1-2-2.out

----
---- No. R-1-3 table doesn't exist in the hint option
----

-- No. R-1-3-1
\o results/R_1-3-1.out.log
/*+Rows(t1 t2 #1)*/
EXPLAIN SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-3-1.out.log > results/R_1-3-1.out
\! diff expected/R_1-3-1.out results/R_1-3-1.out

-- No. R-1-3-2
\o results/R_1-3-2.out.log
/*+Rows(t3 t4 #1)*/
EXPLAIN SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-3-2.out.log > results/R_1-3-2.out
\! diff expected/R_1-3-2.out results/R_1-3-2.out

----
---- No. R-1-4 conflict table name
----

-- No. R-1-4-1
\o results/R_1-4-1.out.log
/*+Rows(t1 t2 #1)*/
EXPLAIN SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-4-1.out.log > results/R_1-4-1.out
\! diff expected/R_1-4-1.out results/R_1-4-1.out

-- No. R-1-4-2
\o results/R_1-4-2.out.log
EXPLAIN SELECT * FROM s1.t1, s2.t1 WHERE s1.t1.c1 = s2.t1.c1;
/*+Rows(t1 t1 #1)*/
EXPLAIN SELECT * FROM s1.t1, s2.t1 WHERE s1.t1.c1 = s2.t1.c1;
/*+Rows(s1.t1 s2.t1 #1)*/
EXPLAIN SELECT * FROM s1.t1, s2.t1 WHERE s1.t1.c1 = s2.t1.c1;

EXPLAIN SELECT * FROM s1.t1, s2.t1 s2t1 WHERE s1.t1.c1 = s2t1.c1;
/*+Rows(t1 s2t1 #1)*/
EXPLAIN SELECT * FROM s1.t1, s2.t1 s2t1 WHERE s1.t1.c1 = s2t1.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-4-2.out.log > results/R_1-4-2.out
\! diff expected/R_1-4-2.out results/R_1-4-2.out

-- No. R-1-4-3
\o results/R_1-4-3.out.log
EXPLAIN SELECT *, (SELECT max(t1.c1) FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1) FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
/*+Rows(t1 t2 #1)*/
EXPLAIN SELECT *, (SELECT max(t1.c1) FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1) FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
/*+Rows(st1 st2 #1)Rows(t1 t2 #1)*/
EXPLAIN SELECT *, (SELECT max(st1.c1) FROM s1.t1 st1, s1.t2 st2 WHERE st1.c1 = st2.c1) FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-4-3.out.log > results/R_1-4-3.out
\! diff expected/R_1-4-3.out results/R_1-4-3.out

----
---- No. R-1-5 conflict table name
----

-- No. R-1-5-1
\o results/R_1-5-1.out.log
/*+Rows(t1 t2 #1)*/
EXPLAIN SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-5-1.out.log > results/R_1-5-1.out
\! diff expected/R_1-5-1.out results/R_1-5-1.out

-- No. R-1-5-2
\o results/R_1-5-2.out.log
/*+Rows(t1 t1 #1)*/
EXPLAIN SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-5-2.out.log > results/R_1-5-2.out
\! diff expected/R_1-5-2.out results/R_1-5-2.out

-- No. R-1-5-3
\o results/R_1-5-3.out.log
/*+(t1 t1)(t2 t2)*/
EXPLAIN SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
EXPLAIN SELECT * FROM s1.t1, s1.t2, s1.t3 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1;
/*+(t1 t2 t1 t2)*/
EXPLAIN SELECT * FROM s1.t1, s1.t2, s1.t3, s1.t4 WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-5-3.out.log > results/R_1-5-3.out
\! diff expected/R_1-5-3.out results/R_1-5-3.out

----
---- No. R-1-6 object type for the hint
----

-- No. R-1-6-1
\o results/R_1-6-1.out.log
/*+Rows(t1 t2 #1)*/
EXPLAIN SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-6-1.out.log > results/R_1-6-1.out
\! diff expected/R_1-6-1.out results/R_1-6-1.out

-- No. R-1-6-2
\o results/R_1-6-2.out.log
EXPLAIN SELECT * FROM s1.p1 t1, s1.p1 t2 WHERE t1.c1 = t2.c1;
/*+Rows(t1 t2 #1)*/
EXPLAIN SELECT * FROM s1.p1 t1, s1.p1 t2 WHERE t1.c1 = t2.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-6-2.out.log > results/R_1-6-2.out
\! diff expected/R_1-6-2.out results/R_1-6-2.out

-- No. R-1-6-3
\o results/R_1-6-3.out.log
EXPLAIN SELECT * FROM s1.ul1 t1, s1.ul1 t2 WHERE t1.c1 = t2.c1;
/*+Rows(t1 t2 #1)*/
EXPLAIN SELECT * FROM s1.ul1 t1, s1.ul1 t2 WHERE t1.c1 = t2.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-6-3.out.log > results/R_1-6-3.out
\! diff expected/R_1-6-3.out results/R_1-6-3.out

-- No. R-1-6-4
CREATE TEMP TABLE tm1 (LIKE s1.t1 INCLUDING ALL);
\o results/R_1-6-4.out.log
EXPLAIN SELECT * FROM tm1 t1, tm1 t2 WHERE t1.c1 = t2.c1;
/*+Rows(t1 t2 #1)*/
EXPLAIN SELECT * FROM tm1 t1, tm1 t2 WHERE t1.c1 = t2.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-6-4.out.log > results/R_1-6-4.out
\! diff expected/R_1-6-4.out results/R_1-6-4.out

-- No. R-1-6-5
\o results/R_1-6-5.out.log
EXPLAIN SELECT * FROM pg_catalog.pg_class t1, pg_catalog.pg_class t2 WHERE t1.oid = t2.oid;
/*+Rows(t1 t2 #1)*/
EXPLAIN SELECT * FROM pg_catalog.pg_class t1, pg_catalog.pg_class t2 WHERE t1.oid = t2.oid;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-6-5.out.log > results/R_1-6-5.out
\! diff expected/R_1-6-5.out results/R_1-6-5.out

-- No. R-1-6-6
-- refer ut-fdw.sql

-- No. R-1-6-7
\o results/R_1-6-7.out.log
EXPLAIN SELECT * FROM s1.f1() t1, s1.f1() t2 WHERE t1.c1 = t2.c1;
/*+Rows(t1 t2 #3)*/
EXPLAIN SELECT * FROM s1.f1() t1, s1.f1() t2 WHERE t1.c1 = t2.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-6-7.out.log > results/R_1-6-7.out
\! diff expected/R_1-6-7.out results/R_1-6-7.out

-- No. R-1-6-8
\o results/R_1-6-8.out.log
EXPLAIN SELECT * FROM (VALUES(1,1,1,'1'), (2,2,2,'2'), (3,3,3,'3')) AS t1 (c1, c2, c3, c4),  s1.t2 WHERE t1.c1 = t2.c1;
/*+Rows(t1 t2 #1)*/
EXPLAIN SELECT * FROM (VALUES(1,1,1,'1'), (2,2,2,'2'), (3,3,3,'3')) AS t1 (c1, c2, c3, c4),  s1.t2 WHERE t1.c1 = t2.c1;
/*+Rows(*VALUES* t2 #1)*/
EXPLAIN SELECT * FROM (VALUES(1,1,1,'1'), (2,2,2,'2'), (3,3,3,'3')) AS t1 (c1, c2, c3, c4),  s1.t2 WHERE t1.c1 = t2.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-6-8.out.log > results/R_1-6-8.out
\! diff expected/R_1-6-8.out results/R_1-6-8.out

-- No. R-1-6-9
\o results/R_1-6-9.out.log
EXPLAIN WITH c1(c1) AS (SELECT max(t1.c1) FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1) SELECT * FROM s1.t1, c1 WHERE t1.c1 = c1.c1;
/*+Rows(t1 t2 #1)Rows(t1 c1 +1)*/
EXPLAIN WITH c1(c1) AS (SELECT max(t1.c1) FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1) SELECT * FROM s1.t1, c1 WHERE t1.c1 = c1.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-6-9.out.log > results/R_1-6-9.out
\! diff expected/R_1-6-9.out results/R_1-6-9.out

-- No. R-1-6-10
\o results/R_1-6-10.out.log
EXPLAIN SELECT * FROM s1.v1 t1, s1.v1 t2 WHERE t1.c1 = t2.c1;
/*+Rows(t1 t2 #1)*/
EXPLAIN SELECT * FROM s1.v1 t1, s1.v1 t2 WHERE t1.c1 = t2.c1;
/*+Rows(v1t1 v1t1_ #1)*/
EXPLAIN SELECT * FROM s1.v1 t1, s1.v1_ t2 WHERE t1.c1 = t2.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-6-10.out.log > results/R_1-6-10.out
\! diff expected/R_1-6-10.out results/R_1-6-10.out

-- No. R-1-6-11
EXPLAIN SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1 AND t1.c1 = (SELECT max(st1.c1) FROM s1.t1 st1, s1.t2 st2 WHERE st1.c1 = st2.c1);
/*+Rows(t1 t2 #1)Rows(st1 st2 #1)*/
EXPLAIN (COSTS true) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1 AND t1.c1 = (SELECT max(st1.c1) FROM s1.t1 st1, s1.t2 st2 WHERE st1.c1 = st2.c1);
--
-- There are cases where difference in the measured value and predicted value
-- depending upon the version of PostgreSQL
--

\o results/R_1-6-11.out.log
EXPLAIN SELECT * FROM s1.t1, (SELECT t2.c1 FROM s1.t2) st2 WHERE t1.c1 = st2.c1;
/*+Rows(t1 st2 #1)*/
EXPLAIN SELECT * FROM s1.t1, (SELECT t2.c1 FROM s1.t2) st2 WHERE t1.c1 = st2.c1;
/*+Rows(t1 t2 #1)*/
EXPLAIN SELECT * FROM s1.t1, (SELECT t2.c1 FROM s1.t2) st2 WHERE t1.c1 = st2.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-6-11.out.log > results/R_1-6-11.out
\! diff expected/R_1-6-11.out results/R_1-6-11.out

----
---- No. R-1-7 specified number of conditions
----

-- No. R-1-7-1
\o results/R_1-7-1.out.log
/*+Rows(t1 #1)*/
EXPLAIN SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-7-1.out.log > results/R_1-7-1.out
\! diff expected/R_1-7-1.out results/R_1-7-1.out

-- No. R-1-7-2
\o results/R_1-7-2.out.log
/*+Rows(t1 t2 1)*/
EXPLAIN SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-7-2.out.log > results/R_1-7-2.out
\! diff expected/R_1-7-2.out results/R_1-7-2.out

-- No. R-1-7-3
\o results/R_1-7-3.out.log
/*+Rows(t1 t2 #notrows)*/
EXPLAIN SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
\o
\! sed 's/cost=[\.0-9]*/cost=xxx/' results/R_1-7-3.out.log > results/R_1-7-3.out
\! diff expected/R_1-7-3.out results/R_1-7-3.out

----
---- No. R-2-1 some complexity query blocks
----

-- No. R-2-1-1
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

-- No. R-2-1-2
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

-- No. R-2-1-3
EXPLAIN SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, (SELECT ctid, * FROM s1.t3 bmt3) sbmt3, (SELECT ctid, * FROM s1.t4 bmt4) sbmt4 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = sbmt3.c1 AND sbmt3.ctid = '(1,1)' AND bmt1.c1 = sbmt4.c1 AND sbmt4.ctid = '(1,1)';
/*+
Leading(bmt4 bmt3 bmt2 bmt1)
MergeJoin(bmt4 bmt3)HashJoin(bmt4 bmt3 bmt2)NestLoop(bmt1 bmt2 bmt3 bmt4)
*/
EXPLAIN SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, (SELECT ctid, * FROM s1.t3 bmt3) sbmt3, (SELECT ctid, * FROM s1.t4 bmt4) sbmt4 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)' AND bmt1.c1 = sbmt3.c1 AND sbmt3.ctid = '(1,1)' AND bmt1.c1 = sbmt4.c1 AND sbmt4.ctid = '(1,1)';

-- No. R-2-1-4
EXPLAIN SELECT max(bmt1.c1) FROM s1.t1 bmt1, (SELECT ctid, * FROM s1.t2 bmt2) sbmt2, (SELECT ctid, * FROM s1.t3 bmt3) sbmt3, (SELECT ctid, * FROM s1.t4 bmt4) sbmt4 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = sbmt2.c1 AND sbmt2.ctid = '(1,1)' AND bmt1.c1 = sbmt3.c1 AND sbmt3.ctid = '(1,1)' AND bmt1.c1 = sbmt4.c1 AND sbmt4.ctid = '(1,1)';
/*+
Leading(bmt4 bmt3 bmt2 bmt1)
MergeJoin(bmt4 bmt3)HashJoin(bmt4 bmt3 bmt2)NestLoop(bmt1 bmt2 bmt3 bmt4)
*/
EXPLAIN SELECT max(bmt1.c1) FROM s1.t1 bmt1, (SELECT ctid, * FROM s1.t2 bmt2) sbmt2, (SELECT ctid, * FROM s1.t3 bmt3) sbmt3, (SELECT ctid, * FROM s1.t4 bmt4) sbmt4 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = sbmt2.c1 AND sbmt2.ctid = '(1,1)' AND bmt1.c1 = sbmt3.c1 AND sbmt3.ctid = '(1,1)' AND bmt1.c1 = sbmt4.c1 AND sbmt4.ctid = '(1,1)';

-- No. R-2-1-5
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

-- No. R-2-1-6
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

-- No. R-2-1-7
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

-- No. R-2-1-8
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
---- No. R-2-2 the number of the tables per quiry block
----

-- No. R-2-2-1
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

-- No. R-2-2-2
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

-- No. R-2-2-3
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

-- No. R-2-2-4
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
---- No. R-2-3 RULE or VIEW
----

-- No. R-2-3-1
EXPLAIN UPDATE s1.r1 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+
Leading(t4 t3 t2 t1 r1)
MergeJoin(t4 t3 t2 t1 r1)
HashJoin(t4 t3 t2 t1)
NestLoop(t4 t3 t2)
MergeJoin(t4 t3)
*/
EXPLAIN UPDATE s1.r1 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
EXPLAIN UPDATE s1.r1_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+
Leading(b1t4 b1t3 b1t2 b1t1 r1_)
MergeJoin(b1t4 b1t3 b1t2 b1t1 r1_)
HashJoin(b1t4 b1t3 b1t2 b1t1)
NestLoop(b1t4 b1t3 b1t2)
MergeJoin(b1t4 b1t3)
*/
EXPLAIN UPDATE s1.r1_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. R-2-3-2
EXPLAIN UPDATE s1.r2 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+
Leading(t4 t3 t2 t1 r2)
MergeJoin(t4 t3 t2 t1 r2)
HashJoin(t4 t3 t2 t1)
NestLoop(t4 t3 t2)
MergeJoin(t4 t3)
*/
EXPLAIN UPDATE s1.r2 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
EXPLAIN UPDATE s1.r2_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
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
EXPLAIN UPDATE s1.r2_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. R-2-3-3
EXPLAIN UPDATE s1.r3 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+
Leading(t4 t3 t2 t1 r3)
MergeJoin(t4 t3 t2 t1 r3)
HashJoin(t4 t3 t2 t1)
NestLoop(t4 t3 t2)
MergeJoin(t4 t3)
*/
EXPLAIN UPDATE s1.r3 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
EXPLAIN UPDATE s1.r3_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
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
EXPLAIN UPDATE s1.r3_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. R-2-3-4
EXPLAIN SELECT * FROM s1.v1 v1, s1.v1 v2 WHERE v1.c1 = v2.c1;
/*+(v1t1 v1t1)*/
EXPLAIN SELECT * FROM s1.v1 v1, s1.v1 v2 WHERE v1.c1 = v2.c1;

-- No. R-2-3-5
EXPLAIN SELECT * FROM s1.v1 v1, s1.v1_ v2 WHERE v1.c1 = v2.c1;
/*+(v1t1 v1t1_)*/
EXPLAIN SELECT * FROM s1.v1 v1, s1.v1_ v2 WHERE v1.c1 = v2.c1;

-- No. R-2-3-6
EXPLAIN SELECT * FROM s1.r4 t1, s1.r4 t2 WHERE t1.c1 = t2.c1;
/*+(r4t1 r4t1)*/
EXPLAIN SELECT * FROM s1.r4 t1, s1.r4 t2 WHERE t1.c1 = t2.c1;

-- No. R-2-3-7
EXPLAIN SELECT * FROM s1.r4 t1, s1.r5 t2 WHERE t1.c1 = t2.c1;
/*+(r4t1 r5t1)*/
EXPLAIN SELECT * FROM s1.r4 t1, s1.r5 t2 WHERE t1.c1 = t2.c1;

----
---- No. R-2-4 VALUES clause
----

-- No. R-2-4-1
EXPLAIN SELECT * FROM s1.t1, s1.t2, (VALUES(1,1,1,'1')) AS t3 (c1, c2, c3, c4) WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1;
/*+ Leading(t3 t1 t2) (t3 t1)(t3 t1 t2)*/
EXPLAIN SELECT * FROM s1.t1, s1.t2, (VALUES(1,1,1,'1')) AS t3 (c1, c2, c3, c4) WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1;
/*+ Leading(*VALUES* t1 t2) (*VALUES* t1)(*VALUES* t1 t2)*/
EXPLAIN SELECT * FROM s1.t1, s1.t2, (VALUES(1,1,1,'1')) AS t3 (c1, c2, c3, c4) WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1;

-- No. R-2-4-2
EXPLAIN SELECT * FROM s1.t1, s1.t2, (VALUES(1,1,1,'1')) AS t3 (c1, c2, c3, c4), (VALUES(1,1,1,'1'), (2,2,2,'2')) AS t4 (c1, c2, c3, c4) WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;
/*+ Leading(t4 t3 t2 t1) (t4 t3)(t4 t3 t2)(t4 t3 t2 t1)*/
EXPLAIN SELECT * FROM s1.t1, s1.t2, (VALUES(1,1,1,'1')) AS t3 (c1, c2, c3, c4), (VALUES(1,1,1,'1'), (2,2,2,'2')) AS t4 (c1, c2, c3, c4) WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;
/*+ Leading(*VALUES* t3 t2 t1) (t4 t3)(*VALUES* t3 t2)(*VALUES* t3 t2 t1)*/
EXPLAIN SELECT * FROM s1.t1, s1.t2, (VALUES(1,1,1,'1')) AS t3 (c1, c2, c3, c4), (VALUES(1,1,1,'1'), (2,2,2,'2')) AS t4 (c1, c2, c3, c4) WHERE t1.c1 = t2.c1 AND t1.c1 = t3.c1 AND t1.c1 = t4.c1;

----
---- No. R-2-5
----

----
---- No. R-3-1 
----

----
---- No. R-3-2 join inherit tables
----
EXPLAIN SELECT * FROM s1.p1, s1.p2 WHERE p1.c1 = p2.c1;

-- No. R-3-2-1
/*+(p1 p2)*/
EXPLAIN SELECT * FROM s1.p1, s1.p2 WHERE p1.c1 = p2.c1;

-- No. R-3-2-2
/*+(p1c1 p2c1)*/
EXPLAIN SELECT * FROM s1.p1, s1.p2 WHERE p1.c1 = p2.c1;

----
---- No. R-3-5 conflict join method hint
----
EXPLAIN SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

-- No. R-3-5-1
/*+(t1 t2)(t1 t2)*/
EXPLAIN SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

-- No. R-3-5-2
/*+(t1 t2)(t1 t2)(t1 t2)*/
EXPLAIN SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

-- No. R-3-5-3
/*+(t1 t2)(t2 t1)*/
EXPLAIN SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

-- No. R-3-5-4
/*+(t2 t1)(t1 t2)(t2 t1)*/
EXPLAIN SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

----
---- No. R-3-6 hint state output
----

-- No. R-3-6-1
/*+(t1 t2)*/
SELECT * FROM s1.t1, s1.t2 WHERE false;
