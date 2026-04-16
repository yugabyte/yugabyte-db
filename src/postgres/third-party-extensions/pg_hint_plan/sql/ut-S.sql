LOAD 'pg_hint_plan';
-- We cannot do ALTER USER current_user SET ...
DELETE FROM pg_db_role_setting WHERE setrole = (SELECT oid FROM pg_roles WHERE rolname = current_user);
INSERT INTO pg_db_role_setting (SELECT 0, (SELECT oid FROM pg_roles WHERE rolname = current_user), '{client_min_messages=log,pg_hint_plan.debug_print=on}');
ALTER SYSTEM SET session_preload_libraries TO 'pg_hint_plan';
SELECT pg_reload_conf();
SET pg_hint_plan.enable_hint TO on;
SET pg_hint_plan.debug_print TO on;
SET client_min_messages TO LOG;
SET search_path TO public;
SET max_parallel_workers_per_gather TO 0;

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
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s2.t1 WHERE s1.t1.c1 = 1 AND s1.t1.c1 = s2.t1.c1;

/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s2.t1 s2t1 WHERE s1.t1.c1 = 1 AND s1.t1.c1 = s2t1.c1;
/*+BitmapScan(s2t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s2.t1 s2t1 WHERE s1.t1.c1 = 1 AND s1.t1.c1 = s2t1.c1;

-- No. S-1-4-3
EXPLAIN (COSTS false) SELECT (SELECT max(c1) FROM s1.t1 WHERE s1.t1.c1 = 1) FROM s1.t1 WHERE s1.t1.c1 = 1;
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) SELECT (SELECT max(c1) FROM s1.t1 WHERE s1.t1.c1 = 1) FROM s1.t1 WHERE s1.t1.c1 = 1;

/*+BitmapScan(t11)*/
EXPLAIN (COSTS false) SELECT (SELECT max(c1) FROM s1.t1 t11 WHERE t11.c1 = 1) FROM s1.t1 t12 WHERE t12.c1 = 1;
/*+BitmapScan(t12)*/
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
-- refer ut-fdw.sql

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
---- No. S-2-1 some complexity query blocks
----

-- No. S-2-1-1
EXPLAIN (COSTS false)
SELECT max(bmt1.c1), (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.c1 = b1t2.c1 AND b1t1.c1 = b1t3.c1 AND b1t1.c1 = b1t4.c1
)
                    FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4 WHERE bmt1.c1 = bmt2.c1 AND bmt1.c1 = bmt3.c1 AND bmt1.c1 = bmt4.c1
;
/*+SeqScan(bmt1)IndexScan(bmt2 t2_pkey)BitmapScan(bmt3 t3_pkey)TidScan(bmt4)
TidScan(b1t1)SeqScan(b1t2)IndexScan(b1t3 t3_pkey)BitmapScan(b1t4 t4_pkey)
*/
EXPLAIN (COSTS false)
SELECT max(bmt1.c1), (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.c1 = b1t2.c1 AND b1t1.c1 = b1t3.c1 AND b1t1.c1 = b1t4.c1
)
                    FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4 WHERE bmt1.c1 = bmt2.c1 AND bmt1.c1 = bmt3.c1 AND bmt1.c1 = bmt4.c1
;

-- No. S-2-1-2
EXPLAIN (COSTS false)
SELECT max(bmt1.c1), (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.c1 = b1t2.c1 AND b1t1.c1 = b1t3.c1 AND b1t1.c1 = b1t4.c1
), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2, s1.t3 b2t3, s1.t4 b2t4 WHERE b2t1.c1 = b2t2.c1 AND b2t1.c1 = b2t3.c1 AND b2t1.c1 = b2t4.c1
)
                    FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4 WHERE bmt1.c1 = bmt2.c1 AND bmt1.c1 = bmt3.c1 AND bmt1.c1 = bmt4.c1
;
/*+SeqScan(bmt1)IndexScan(bmt2 t2_pkey)BitmapScan(bmt3 t3_pkey)TidScan(bmt4)
TidScan(b1t1)SeqScan(b1t2)IndexScan(b1t3 t3_pkey)BitmapScan(b1t4 t4_pkey)
BitmapScan(b2t1 t1_pkey)TidScan(b2t2)SeqScan(b2t3)IndexScan(b2t4 t4_pkey)
*/
EXPLAIN (COSTS false)
SELECT max(bmt1.c1), (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.c1 = b1t2.c1 AND b1t1.c1 = b1t3.c1 AND b1t1.c1 = b1t4.c1
), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2, s1.t3 b2t3, s1.t4 b2t4 WHERE b2t1.c1 = b2t2.c1 AND b2t1.c1 = b2t3.c1 AND b2t1.c1 = b2t4.c1
)
                    FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4 WHERE bmt1.c1 = bmt2.c1 AND bmt1.c1 = bmt3.c1 AND bmt1.c1 = bmt4.c1
;

-- No. S-2-1-3
EXPLAIN (COSTS false) SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, (SELECT * FROM s1.t4 bmt4) sbmt4 WHERE bmt1.c1 = bmt2.c1 AND bmt1.c1 = bmt3.c1 AND bmt1.c1 = sbmt4.c1;
/*+SeqScan(bmt1)IndexScan(bmt2 t2_pkey)BitmapScan(bmt3 t3_pkey)TidScan(bmt4)
*/
EXPLAIN (COSTS false) SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, (SELECT * FROM s1.t4 bmt4) sbmt4 WHERE bmt1.c1 = bmt2.c1 AND bmt1.c1 = bmt3.c1 AND bmt1.c1 = sbmt4.c1;

-- No. S-2-1-4
EXPLAIN (COSTS false) SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, (SELECT * FROM s1.t3 bmt3) sbmt3, (SELECT * FROM s1.t4 bmt4) sbmt4 WHERE bmt1.c1 = bmt2.c1 AND bmt1.c1 = sbmt3.c1 AND bmt1.c1 = sbmt4.c1;
/*+SeqScan(bmt1)IndexScan(bmt2 t2_pkey)BitmapScan(bmt3 t3_pkey)TidScan(bmt4)
*/
EXPLAIN (COSTS false) SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, (SELECT * FROM s1.t3 bmt3) sbmt3, (SELECT * FROM s1.t4 bmt4) sbmt4 WHERE bmt1.c1 = bmt2.c1 AND bmt1.c1 = sbmt3.c1 AND bmt1.c1 = sbmt4.c1;

-- No. S-2-1-5
EXPLAIN (COSTS false)
SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4 WHERE bmt1.c1 = bmt2.c1 AND bmt1.c1 = bmt3.c1 AND bmt1.c1 = bmt4.c1
  AND bmt1.c1 <> (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.c1 = b1t2.c1 AND b1t1.c1 = b1t3.c1 AND b1t1.c1 = b1t4.c1
)
;
/*+SeqScan(bmt1)IndexScan(bmt2 t2_pkey)BitmapScan(bmt3 t3_pkey)TidScan(bmt4)
TidScan(b1t1)SeqScan(b1t2)IndexScan(b1t3 t3_pkey)BitmapScan(b1t4 t4_pkey)
*/
EXPLAIN (COSTS false)
SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4 WHERE bmt1.c1 = bmt2.c1 AND bmt1.c1 = bmt3.c1 AND bmt1.c1 = bmt4.c1
  AND bmt1.c1 <> (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.c1 = b1t2.c1 AND b1t1.c1 = b1t3.c1 AND b1t1.c1 = b1t4.c1
)
;

-- No. S-2-1-6
EXPLAIN (COSTS false)
SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4 WHERE bmt1.c1 = bmt2.c1 AND bmt1.c1 = bmt3.c1 AND bmt1.c1 = bmt4.c1
  AND bmt1.c1 <> (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.c1 = b1t2.c1 AND b1t1.c1 = b1t3.c1 AND b1t1.c1 = b1t4.c1
) AND bmt1.c1 <> (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2, s1.t3 b2t3, s1.t4 b2t4 WHERE b2t1.c1 = b2t2.c1 AND b2t1.c1 = b2t3.c1 AND b2t1.c1 = b2t4.c1
)
;
/*+SeqScan(bmt1)IndexScan(bmt2 t2_pkey)BitmapScan(bmt3 t3_pkey)TidScan(bmt4)
TidScan(b1t1)SeqScan(b1t2)IndexScan(b1t3 t3_pkey)BitmapScan(b1t4 t4_pkey)
BitmapScan(b2t1 t1_pkey)TidScan(b2t2)SeqScan(b2t3)IndexScan(b2t4 t4_pkey)
*/
EXPLAIN (COSTS false)
SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4 WHERE bmt1.c1 = bmt2.c1 AND bmt1.c1 = bmt3.c1 AND bmt1.c1 = bmt4.c1
  AND bmt1.c1 <> (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.c1 = b1t2.c1 AND b1t1.c1 = b1t3.c1 AND b1t1.c1 = b1t4.c1
) AND bmt1.c1 <> (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2, s1.t3 b2t3, s1.t4 b2t4 WHERE b2t1.c1 = b2t2.c1 AND b2t1.c1 = b2t3.c1 AND b2t1.c1 = b2t4.c1
)
;

-- No. S-2-1-7
EXPLAIN (COSTS false)
WITH c1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.c1 = b1t2.c1 AND b1t1.c1 = b1t3.c1 AND b1t1.c1 = b1t4.c1
)
SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4
, c1
                                                                        WHERE bmt1.c1 = bmt2.c1 AND bmt1.c1 = bmt3.c1 AND bmt1.c1 = bmt4.c1
AND bmt1.c1 = c1.c1
;
/*+SeqScan(bmt1)IndexScan(bmt2 t2_pkey)BitmapScan(bmt3 t3_pkey)TidScan(bmt4)
TidScan(b1t1)SeqScan(b1t2)IndexScan(b1t3 t3_pkey)BitmapScan(b1t4 t4_pkey)
*/
EXPLAIN (COSTS false)
WITH c1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.c1 = b1t2.c1 AND b1t1.c1 = b1t3.c1 AND b1t1.c1 = b1t4.c1
)
SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4
, c1
                                                                        WHERE bmt1.c1 = bmt2.c1 AND bmt1.c1 = bmt3.c1 AND bmt1.c1 = bmt4.c1
AND bmt1.c1 = c1.c1
;

-- No. S-2-1-8
EXPLAIN (COSTS false)
WITH c1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.c1 = b1t2.c1 AND b1t1.c1 = b1t3.c1 AND b1t1.c1 = b1t4.c1
)
, c2 (c1) AS (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2, s1.t3 b2t3, s1.t4 b2t4 WHERE b2t1.c1 = b2t2.c1 AND b2t1.c1 = b2t3.c1 AND b2t1.c1 = b2t4.c1
)
SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4
, c1, c2
                                                                        WHERE bmt1.c1 = bmt2.c1 AND bmt1.c1 = bmt3.c1 AND bmt1.c1 = bmt4.c1
AND bmt1.c1 = c1.c1
AND bmt1.c1 = c2.c1
;
/*+SeqScan(bmt1)IndexScan(bmt2 t2_pkey)BitmapScan(bmt3 t3_pkey)TidScan(bmt4)
TidScan(b1t1)SeqScan(b1t2)IndexScan(b1t3 t3_pkey)BitmapScan(b1t4 t4_pkey)
BitmapScan(b2t1 t1_pkey)TidScan(b2t2)SeqScan(b2t3)IndexScan(b2t4 t4_pkey)
*/
EXPLAIN (COSTS false)
WITH c1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2, s1.t3 b1t3, s1.t4 b1t4 WHERE b1t1.c1 = b1t2.c1 AND b1t1.c1 = b1t3.c1 AND b1t1.c1 = b1t4.c1
)
, c2 (c1) AS (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2, s1.t3 b2t3, s1.t4 b2t4 WHERE b2t1.c1 = b2t2.c1 AND b2t1.c1 = b2t3.c1 AND b2t1.c1 = b2t4.c1
)
SELECT max(bmt1.c1) FROM s1.t1 bmt1, s1.t2 bmt2, s1.t3 bmt3, s1.t4 bmt4
, c1, c2
                                                                        WHERE bmt1.c1 = bmt2.c1 AND bmt1.c1 = bmt3.c1 AND bmt1.c1 = bmt4.c1
AND bmt1.c1 = c1.c1
AND bmt1.c1 = c2.c1
;

----
---- No. S-2-2 the number of the tables per quiry block
----

-- No. S-2-2-1
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
/*+SeqScan(bmt1)
TidScan(b1t1)
BitmapScan(b2t1 t1_pkey)
IndexScan(b3t1 t1_pkey)
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

-- No. S-2-2-2
EXPLAIN (COSTS false)
WITH c1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = b1t2.c1 AND b1t2.ctid = '(1,1)'
)
SELECT max(bmt1.c1), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = b2t2.c1 AND b2t2.ctid = '(1,1)'
)
                    FROM s1.t1 bmt1, s1.t2 bmt2, c1 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)'
AND bmt1.c1 <> (
SELECT max(b3t1.c1) FROM s1.t1 b3t1, s1.t2 b3t2 WHERE b3t1.ctid = '(1,1)' AND b3t1.c1 = b3t2.c1 AND b3t2.ctid = '(1,1)'
)
;
/*+SeqScan(bmt1)IndexScan(bmt2 t2_pkey)
TidScan(b1t1)SeqScan(b1t2)
BitmapScan(b2t1 t1_pkey)TidScan(b2t2)
IndexScan(b3t1 t1_pkey)BitmapScan(b3t2 t2_pkey)
*/
EXPLAIN (COSTS false)
WITH c1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1, s1.t2 b1t2 WHERE b1t1.ctid = '(1,1)' AND b1t1.c1 = b1t2.c1 AND b1t2.ctid = '(1,1)'
)
SELECT max(bmt1.c1), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1, s1.t2 b2t2 WHERE b2t1.ctid = '(1,1)' AND b2t1.c1 = b2t2.c1 AND b2t2.ctid = '(1,1)'
)
                    FROM s1.t1 bmt1, s1.t2 bmt2, c1 WHERE bmt1.ctid = '(1,1)' AND bmt1.c1 = bmt2.c1 AND bmt2.ctid = '(1,1)'
AND bmt1.c1 <> (
SELECT max(b3t1.c1) FROM s1.t1 b3t1, s1.t2 b3t2 WHERE b3t1.ctid = '(1,1)' AND b3t1.c1 = b3t2.c1 AND b3t2.ctid = '(1,1)'
)
;

-- No. S-2-2-3
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
/*+SeqScan(bmt1)IndexScan(bmt2 t2_pkey)BitmapScan(bmt3 t3_pkey)TidScan(bmt4)
TidScan(b1t1)SeqScan(b1t2)IndexScan(b1t3 t3_pkey)BitmapScan(b1t4 t4_pkey)
BitmapScan(b2t1 t1_pkey)TidScan(b2t2)SeqScan(b2t3)IndexScan(b2t4 t4_pkey)
IndexScan(b3t1 t1_pkey)BitmapScan(b3t2 t2_pkey)TidScan(b3t3)SeqScan(b3t4)
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

-- No. S-2-2-4
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
/*+SeqScan(bmt1)IndexScan(bmt2 t2_pkey)BitmapScan(bmt3 t3_pkey)TidScan(bmt4)
TidScan(b1t1)SeqScan(b1t2)IndexScan(b1t3 t3_pkey)BitmapScan(b1t4 t4_pkey)
BitmapScan(b2t1 t1_pkey)
IndexScan(b3t1 t1_pkey)
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
---- No. S-2-3 RULE or VIEW
----

-- No. S-2-3-1
EXPLAIN (COSTS false) UPDATE s1.r1 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+TidScan(t1)SeqScan(t2)IndexScan(t3 t3_pkey)BitmapScan(t4 t4_pkey)
SeqScan(r1)*/
EXPLAIN (COSTS false) UPDATE s1.r1 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
EXPLAIN (COSTS false) UPDATE s1.r1_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+TidScan(b1t1)SeqScan(b1t2)IndexScan(b1t3 t3_pkey)BitmapScan(b1t4 t4_pkey)
SeqScan(r1_)*/
EXPLAIN (COSTS false) UPDATE s1.r1_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-2-3-2
EXPLAIN (COSTS false) UPDATE s1.r2 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+TidScan(t1)SeqScan(t2)IndexScan(t3 t3_pkey)BitmapScan(t4 t4_pkey)
SeqScan(r2)*/
EXPLAIN (COSTS false) UPDATE s1.r2 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
EXPLAIN (COSTS false) UPDATE s1.r2_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+TidScan(b1t1)SeqScan(b1t2)IndexScan(b1t3 t3_pkey)BitmapScan(b1t4 t4_pkey)
BitmapScan(b2t1 t1_pkey)TidScan(b2t2)SeqScan(b2t3)IndexScan(b2t4 t4_pkey)
SeqScan(r2_)*/
EXPLAIN (COSTS false) UPDATE s1.r2_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-2-3-3
EXPLAIN (COSTS false) UPDATE s1.r3 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+TidScan(t1)SeqScan(t2)IndexScan(t3 t3_pkey)BitmapScan(t4 t4_pkey)
SeqScan(r3)*/
EXPLAIN (COSTS false) UPDATE s1.r3 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
EXPLAIN (COSTS false) UPDATE s1.r3_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+TidScan(b1t1)SeqScan(b1t2)IndexScan(b1t3 t3_pkey)BitmapScan(b1t4 t4_pkey)
BitmapScan(b2t1 t1_pkey)TidScan(b2t2)SeqScan(b2t3)IndexScan(b2t4 t4_pkey)
IndexScan(b3t1 t1_pkey)BitmapScan(b3t2 t2_pkey)TidScan(b3t3)SeqScan(b3t4)
SeqScan(r3_)*/
EXPLAIN (COSTS false) UPDATE s1.r3_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-2-3-4
EXPLAIN (COSTS false) SELECT * FROM s1.v1 v1, s1.v1 v2 WHERE v1.c1 = v2.c1;
/*+BitmapScan(v1t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.v1 v1, s1.v1 v2 WHERE v1.c1 = v2.c1;

-- No. S-2-3-5
EXPLAIN (COSTS false) SELECT * FROM s1.v1 v1, s1.v1_ v2 WHERE v1.c1 = v2.c1;
/*+SeqScan(v1t1)BitmapScan(v1t1_)*/
EXPLAIN (COSTS false) SELECT * FROM s1.v1 v1, s1.v1_ v2 WHERE v1.c1 = v2.c1;

-- No. S-2-3-6
EXPLAIN (COSTS false) SELECT * FROM s1.r4 t1, s1.r4 t2 WHERE t1.c1 = t2.c1;
/*+BitmapScan(r4t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.r4 t1, s1.r4 t2 WHERE t1.c1 = t2.c1;

-- No. S-2-3-7
EXPLAIN (COSTS false) SELECT * FROM s1.r4 t1, s1.r5 t2 WHERE t1.c1 = t2.c1;
/*+SeqScan(r4t1)BitmapScan(r5t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.r4 t1, s1.r5 t2 WHERE t1.c1 = t2.c1;

----
---- No. S-2-4 VALUES clause
----

-- No. S-2-4-1
EXPLAIN (COSTS false) SELECT * FROM (VALUES(1,1,1,'1')) AS t1 (c1) WHERE t1.c1 = 1;
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM (VALUES(1,1,1,'1')) AS t1 (c1) WHERE t1.c1 = 1;
/*+SeqScan(*VALUES*)*/
EXPLAIN (COSTS false) SELECT * FROM (VALUES(1,1,1,'1')) AS t1 (c1) WHERE t1.c1 = 1;

-- No. S-2-4-2
EXPLAIN (COSTS false) SELECT * FROM (VALUES(1,1,1,'1'), (3,3,3,'3')) AS t1 (c1, c2, c3, c4), (VALUES(1,1,1,'1'), (2,2,2,'2')) AS t2 (c1, c2) WHERE t1.c1 = t2.c1;
/*+SeqScan(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM (VALUES(1,1,1,'1'), (3,3,3,'3')) AS t1 (c1, c2, c3, c4), (VALUES(1,1,1,'1'), (2,2,2,'2')) AS t2 (c1, c2) WHERE t1.c1 = t2.c1;
/*+SeqScan(*VALUES*)*/
EXPLAIN (COSTS false) SELECT * FROM (VALUES(1,1,1,'1'), (3,3,3,'3')) AS t1 (c1, c2, c3, c4), (VALUES(1,1,1,'1'), (2,2,2,'2')) AS t2 (c1, c2) WHERE t1.c1 = t2.c1;

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

-- No. S-3-1-17
EXPLAIN (COSTS false) SELECT c1 FROM s1.t1 WHERE t1.c1 = 1;
/*+IndexOnlyScan(t1)*/
EXPLAIN (COSTS false) SELECT c1 FROM s1.t1 WHERE t1.c1 = 1;

-- No. S-3-1-18
EXPLAIN (COSTS false) SELECT c1 FROM s1.t1 WHERE t1.c1 >= 1;
/*+IndexOnlyScan(t1)*/
EXPLAIN (COSTS false) SELECT c1 FROM s1.t1 WHERE t1.c1 >= 1;

-- No. S-3-1-19
EXPLAIN (COSTS false) SELECT c1 FROM s1.t1 WHERE t1.c1 = 1;
/*+NoIndexOnlyScan(t1)*/
EXPLAIN (COSTS false) SELECT c1 FROM s1.t1 WHERE t1.c1 = 1;

-- No. S-3-1-20
EXPLAIN (COSTS false) SELECT c1 FROM s1.t1 WHERE t1.c1 >= 1;
/*+NoIndexOnlyScan(t1)*/
EXPLAIN (COSTS false) SELECT c1 FROM s1.t1 WHERE t1.c1 >= 1;

----
---- No. S-3-3 index name specified
----

EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE ti1.c2 = 1 AND ctid = '(1,1)';
SET enable_tidscan TO off;
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE ti1.c2 = 1 AND ctid = '(1,1)';
SET enable_indexscan TO off;
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE ti1.c2 = 1 AND ctid = '(1,1)';
RESET enable_tidscan;
RESET enable_indexscan;

EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 WHERE ti1.c2 >= 1;

-- No. S-3-3-1
/*+IndexScan(ti1 ti1_i3)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE ti1.c2 = 1 AND ctid = '(1,1)';

-- No. S-3-3-2
/*+IndexScan(ti1 ti1_i3 ti1_i2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE ti1.c2 = 1 AND ctid = '(1,1)';

-- No. S-3-3-3
/*+IndexScan(ti1 ti1_i4 ti1_i3 ti1_i2 ti1_i1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE ti1.c2 = 1 AND ctid = '(1,1)';

-- No. S-3-3-4
/*+BitmapScan(ti1 ti1_i3)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE ti1.c2 = 1 AND ctid = '(1,1)';

-- No. S-3-3-5
/*+BitmapScan(ti1 ti1_i3 ti1_i2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE ti1.c2 = 1 AND ctid = '(1,1)';

-- No. S-3-3-6
/*+BitmapScan(ti1 ti1_i4 ti1_i3 ti1_i2 ti1_i1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE ti1.c2 = 1 AND ctid = '(1,1)';

-- No. S-3-3-7
/*+IndexOnlyScan(ti1 ti1_i3)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 WHERE ti1.c2 >= 1;

-- No. S-3-3-8
/*+IndexOnlyScan(ti1 ti1_i3 ti1_i2)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 WHERE ti1.c2 >= 1;

-- No. S-3-3-9
/*+IndexOnlyScan(ti1 ti1_i4 ti1_i3 ti1_i2 ti1_i1)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 WHERE ti1.c2 >= 1;

----
---- No. S-3-4 index type
----

\d s1.ti1
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 < 100 AND c2 = 1 AND lower(c4) = '1' AND to_tsvector('english', c4) @@ 'a & b' AND ctid = '(1,1)';

-- No. S-3-4-1
/*+IndexScan(ti1 ti1_btree)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 < 100 AND c2 = 1 AND lower(c4) = '1' AND to_tsvector('english', c4) @@ 'a & b' AND ctid = '(1,1)';

-- No. S-3-4-2
/*+IndexScan(ti1 ti1_hash)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 100 AND c2 = 1 AND lower(c4) = '1' AND to_tsvector('english', c4) @@ 'a & b' AND ctid = '(1,1)';

-- No. S-3-4-3
/*+IndexScan(ti1 ti1_gist)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 < 100 AND c2 = 1 AND lower(c4) = '1' AND to_tsvector('english', c4) @@ 'a & b' AND ctid = '(1,1)';

-- No. S-3-4-4
/*+IndexScan(ti1 ti1_gin)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 < 100 AND c2 = 1 AND lower(c4) = '1' AND to_tsvector('english', c4) @@ 'a & b' AND ctid = '(1,1)';

-- No. S-3-4-5
/*+IndexScan(ti1 ti1_expr)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 < 100 AND c2 = 1 AND lower(c4) = '1' AND to_tsvector('english', c4) @@ 'a & b' AND ctid = '(1,1)';

-- No. S-3-4-6
/*+IndexScan(ti1 ti1_pred)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 < 100 AND c2 = 1 AND lower(c4) = '1' AND to_tsvector('english', c4) @@ 'a & b' AND ctid = '(1,1)';

-- No. S-3-4-7
/*+IndexScan(ti1 ti1_uniq)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 < 100 AND c2 = 1 AND lower(c4) = '1' AND to_tsvector('english', c4) @@ 'a & b' AND ctid = '(1,1)';

-- No. S-3-4-8
/*+IndexScan(ti1 ti1_multi)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 < 100 AND c2 = 1 AND lower(c4) = '1' AND to_tsvector('english', c4) @@ 'a & b' AND ctid = '(1,1)';

-- No. S-3-4-9
/*+IndexScan(ti1 ti1_ts)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 < 100 AND c2 = 1 AND lower(c4) = '1' AND to_tsvector('english', c4) @@ 'a & b' AND ctid = '(1,1)';

-- No. S-3-4-10
/*+IndexScan(ti1 ti1_pkey)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 < 100 AND c2 = 1 AND lower(c4) = '1' AND to_tsvector('english', c4) @@ 'a & b' AND ctid = '(1,1)';

-- No. S-3-4-11
/*+IndexScan(ti1 ti1_c2_key)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 < 100 AND c2 = 1 AND lower(c4) = '1' AND to_tsvector('english', c4) @@ 'a & b' AND ctid = '(1,1)';

-- No. S-3-4-12
/*+BitmapScan(ti1 ti1_btree)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 < 100 AND c2 = 1 AND lower(c4) = '1' AND to_tsvector('english', c4) @@ 'a & b' AND ctid = '(1,1)';

-- No. S-3-4-13
/*+BitmapScan(ti1 ti1_hash)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 100 AND c2 = 1 AND lower(c4) = '1' AND to_tsvector('english', c4) @@ 'a & b' AND ctid = '(1,1)';

-- No. S-3-4-14
/*+BitmapScan(ti1 ti1_gist)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 < 100 AND c2 = 1 AND lower(c4) = '1' AND to_tsvector('english', c4) @@ 'a & b' AND ctid = '(1,1)';

-- No. S-3-4-15
/*+BitmapScan(ti1 ti1_gin)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 < 100 AND c2 = 1 AND lower(c4) = '1' AND to_tsvector('english', c4) @@ 'a & b' AND ctid = '(1,1)';

-- No. S-3-4-16
/*+BitmapScan(ti1 ti1_expr)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 < 100 AND c2 = 1 AND lower(c4) = '1' AND to_tsvector('english', c4) @@ 'a & b' AND ctid = '(1,1)';

-- No. S-3-4-17
/*+BitmapScan(ti1 ti1_pred)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 < 100 AND c2 = 1 AND lower(c4) = '1' AND to_tsvector('english', c4) @@ 'a & b' AND ctid = '(1,1)';

-- No. S-3-4-18
/*+BitmapScan(ti1 ti1_uniq)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 < 100 AND c2 = 1 AND lower(c4) = '1' AND to_tsvector('english', c4) @@ 'a & b' AND ctid = '(1,1)';

-- No. S-3-4-19
/*+BitmapScan(ti1 ti1_multi)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 < 100 AND c2 = 1 AND lower(c4) = '1' AND to_tsvector('english', c4) @@ 'a & b' AND ctid = '(1,1)';

-- No. S-3-4-20
/*+BitmapScan(ti1 ti1_ts)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 < 100 AND c2 = 1 AND lower(c4) = '1' AND to_tsvector('english', c4) @@ 'a & b' AND ctid = '(1,1)';

-- No. S-3-4-21
/*+BitmapScan(ti1 ti1_pkey)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 < 100 AND c2 = 1 AND lower(c4) = '1' AND to_tsvector('english', c4) @@ 'a & b' AND ctid = '(1,1)';

-- No. S-3-4-22
/*+BitmapScan(ti1 ti1_c2_key)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 < 100 AND c2 = 1 AND lower(c4) = '1' AND to_tsvector('english', c4) @@ 'a & b' AND ctid = '(1,1)';

-- No. S-3-4-23
EXPLAIN (COSTS false) SELECT c1 FROM s1.ti1 WHERE c1 >= 1;
/*+IndexOnlyScan(ti1 ti1_btree)*/
EXPLAIN (COSTS false) SELECT c1 FROM s1.ti1 WHERE c1 >= 1;

-- No. S-3-4-24
EXPLAIN (COSTS false) SELECT c1 FROM s1.ti1 WHERE c1 = 1;
/*+IndexOnlyScan(ti1 ti1_hash)*/
EXPLAIN (COSTS false) SELECT c1 FROM s1.ti1 WHERE c1 = 1;

-- No. S-3-4-25
EXPLAIN (COSTS false) SELECT c1 FROM s1.ti1 WHERE c1 < 1;
/*+IndexOnlyScan(ti1 ti1_gist)*/
EXPLAIN (COSTS false) SELECT c1 FROM s1.ti1 WHERE c1 < 1;

-- No. S-3-4-26
EXPLAIN (COSTS false) SELECT c1 FROM s1.ti1 WHERE c1 = 1;
/*+IndexOnlyScan(ti1 ti1_gin)*/
EXPLAIN (COSTS false) SELECT c1 FROM s1.ti1 WHERE c1 = 1;

-- No. S-3-4-27
EXPLAIN (COSTS false) SELECT c1 FROM s1.ti1 WHERE c1 < 100;
/*+IndexOnlyScan(ti1 ti1_expr)*/
EXPLAIN (COSTS false) SELECT c1 FROM s1.ti1 WHERE c1 < 100;

-- No. S-3-4-28
EXPLAIN (COSTS false) SELECT c4 FROM s1.ti1 WHERE lower(c4) >= '1';
/*+IndexOnlyScan(ti1 ti1_pred)*/
EXPLAIN (COSTS false) SELECT c4 FROM s1.ti1 WHERE lower(c4) >= '1';

-- No. S-3-4-29
EXPLAIN (COSTS false) SELECT c1 FROM s1.ti1 WHERE c1 >= 1;
/*+IndexOnlyScan(ti1 ti1_uniq)*/
EXPLAIN (COSTS false) SELECT c1 FROM s1.ti1 WHERE c1 >= 1;

-- No. S-3-4-30
EXPLAIN (COSTS false) SELECT c1 FROM s1.ti1 WHERE c1 >= 1;
/*+IndexOnlyScan(ti1 ti1_multi)*/
EXPLAIN (COSTS false) SELECT c1 FROM s1.ti1 WHERE c1 >= 1;

-- No. S-3-4-31
EXPLAIN (COSTS false) SELECT c1 FROM s1.ti1 WHERE to_tsvector('english', c4) @@ 'a & b';
/*+IndexOnlyScan(ti1 ti1_ts)*/
EXPLAIN (COSTS false) SELECT c1 FROM s1.ti1 WHERE to_tsvector('english', c4) @@ 'a & b';

-- No. S-3-4-32
EXPLAIN (COSTS false) SELECT c1 FROM s1.ti1 WHERE c1 >= 1;
/*+IndexOnlyScan(ti1 ti1_pkey)*/
EXPLAIN (COSTS false) SELECT c1 FROM s1.ti1 WHERE c1 >= 1;

-- No. S-3-4-33
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 WHERE c2 >= 1;
/*+IndexOnlyScan(ti1 ti1_c2_key)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 WHERE c2 >= 1;

----
---- No. S-3-5 not used index
----

-- No. S-3-5-1
\o results/ut-S.tmpout
/*+IndexScan(ti1 ti1_pred)*/ EXPLAIN (COSTS true) SELECT * FROM s1.ti1 WHERE c1 = 100;
\o
\! sql/maskout.sh results/ut-S.tmpout
-- No. S-3-5-2
\o results/ut-S.tmpout
/*+BitmapScan(ti1 ti1_pred)*/ EXPLAIN (COSTS true) SELECT * FROM s1.ti1 WHERE c1 = 100;
\o
\! sql/maskout.sh results/ut-S.tmpout
-- No. S-3-5-3
\o results/ut-S.tmpout
/*+IndexOnlyScan(ti1 ti1_pred)*/ EXPLAIN (COSTS true) SELECT c1 FROM s1.ti1 WHERE c1 = 100;
\o
\! sql/maskout.sh results/ut-S.tmpout
-- No. S-3-5-4
\o results/ut-S.tmpout
/*+IndexScan(ti1 not_exist)*/ EXPLAIN (COSTS true) SELECT * FROM s1.ti1 WHERE c1 = 100;
\o
\! sql/maskout.sh results/ut-S.tmpout
-- No. S-3-5-5
\o results/ut-S.tmpout
/*+BitmapScan(ti1 not_exist)*/ EXPLAIN (COSTS true) SELECT * FROM s1.ti1 WHERE c1 = 100;
\o
\! sql/maskout.sh results/ut-S.tmpout
-- No. S-3-5-6
\o results/ut-S.tmpout
/*+IndexOnlyScan(ti1 not_exist)*/ EXPLAIN (COSTS true) SELECT c1 FROM s1.ti1 WHERE c1 = 100;
\o
\! sql/maskout.sh results/ut-S.tmpout
-- No. S-3-5-7
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;
\o results/ut-S.tmpout
/*+TidScan(t1)*/ EXPLAIN (COSTS true) SELECT * FROM s1.t1 WHERE t1.c1 = 1;
\o
\! sql/maskout.sh results/ut-S.tmpout
----
---- No. S-3-6 query structure
----

EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1 AND t1.ctid = '(1,1)';

-- No. S-3-6-1
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE c1 = 100;

-- No. S-3-6-2
/*+SeqScan(t1)BitmapScan(t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1 AND t1.ctid = '(1,1)';

-- No. S-3-6-3
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1 AND t1.ctid = '(1,1)';

----
---- No. S-3-7 number of tables in a query block
----

-- No. S-3-7-1
EXPLAIN (COSTS false) 
WITH c1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1 WHERE b1t1.c1 = 1)
SELECT max(b3t1.c1), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1 WHERE b2t1.c1 = 1
                  ) FROM s1.t1 b3t1 WHERE b3t1.c1 = (
SELECT max(b4t1.c1) FROM s1.t1 b4t1 WHERE b4t1.c1 = 1);
/*+SeqScan(b1t1)IndexScan(b2t1 t1_pkey)BitmapScan(b3t1 t1_pkey)TidScan(b4t1)
*/
EXPLAIN (COSTS false) 
WITH c1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1 WHERE b1t1.c1 = 1)
SELECT max(b3t1.c1), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1 WHERE b2t1.c1 = 1
                  ) FROM s1.t1 b3t1 WHERE b3t1.c1 = (
SELECT max(b4t1.c1) FROM s1.t1 b4t1 WHERE b4t1.c1 = 1);

-- No. S-3-7-2
EXPLAIN (COSTS false) 
WITH cte1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1 JOIN s1.t2 b1t2 ON(b1t1.c1 = b1t2.c1) WHERE b1t1.c1 = 1)
SELECT max(b3t1.c1), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1 JOIN s1.t2 b2t2 ON(b2t1.c1 = b2t2.c1) WHERE b2t1.c1 = 1
                  ) FROM s1.t1 b3t1 JOIN s1.t2 b3t2 ON(b3t1.c1 = b3t2.c1) JOIN cte1 ON(b3t1.c1 = cte1.c1) WHERE b3t1.c1 = (
SELECT max(b4t1.c1) FROM s1.t1 b4t1 JOIN s1.t2 b4t2 ON(b4t1.c1 = b4t2.c1) WHERE b4t1.c1 = 1);
/*+SeqScan(b1t1)IndexScan(b2t1 t1_pkey)BitmapScan(b3t1 t1_pkey)TidScan(b4t1)
TidScan(b1t2)SeqScan(b2t2)IndexScan(b3t2 t2_pkey)BitmapScan(b4t2 t2_pkey)
*/
EXPLAIN (COSTS false) 
WITH cte1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1 JOIN s1.t2 b1t2 ON(b1t1.c1 = b1t2.c1) WHERE b1t1.c1 = 1)
SELECT max(b3t1.c1), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1 JOIN s1.t2 b2t2 ON(b2t1.c1 = b2t2.c1) WHERE b2t1.c1 = 1
                  ) FROM s1.t1 b3t1 JOIN s1.t2 b3t2 ON(b3t1.c1 = b3t2.c1) JOIN cte1 ON(b3t1.c1 = cte1.c1) WHERE b3t1.c1 = (
SELECT max(b4t1.c1) FROM s1.t1 b4t1 JOIN s1.t2 b4t2 ON(b4t1.c1 = b4t2.c1) WHERE b4t1.c1 = 1);

-- No. S-3-7-3
EXPLAIN (COSTS false) 
WITH cte1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1 JOIN s1.t2 b1t2 ON(b1t1.c1 = b1t2.c1) WHERE b1t1.c1 = 1)
SELECT max(b3t1.c1), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1 WHERE b2t1.c1 = 1
                  ) FROM s1.t1 b3t1 JOIN s1.t2 b3t2 ON(b3t1.c1 = b3t2.c1) JOIN cte1 ON(b3t1.c1 = cte1.c1) WHERE b3t1.c1 = (
SELECT max(b4t1.c1) FROM s1.t1 b4t1 WHERE b4t1.c1 = 1);
/*+SeqScan(b1t1)IndexScan(b2t1 t1_pkey)BitmapScan(b3t1 t1_pkey)TidScan(b4t1)
TidScan(b1t2)IndexScan(b3t2 t2_pkey)
*/
EXPLAIN (COSTS false) 
WITH cte1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1 JOIN s1.t2 b1t2 ON(b1t1.c1 = b1t2.c1) WHERE b1t1.c1 = 1)
SELECT max(b3t1.c1), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1 WHERE b2t1.c1 = 1
                  ) FROM s1.t1 b3t1 JOIN s1.t2 b3t2 ON(b3t1.c1 = b3t2.c1) JOIN cte1 ON(b3t1.c1 = cte1.c1) WHERE b3t1.c1 = (
SELECT max(b4t1.c1) FROM s1.t1 b4t1 WHERE b4t1.c1 = 1);

----
---- No. S-3-8 inheritance table select/update type
----

-- No. S-3-8-1
EXPLAIN (COSTS false) SELECT * FROM ONLY s1.p1 WHERE c1 = 1;
/*+IndexScan(p1)*/
EXPLAIN (COSTS false) SELECT * FROM ONLY s1.p1 WHERE c1 = 1;

-- No. S-3-8-2
EXPLAIN (COSTS false) SELECT * FROM s1.p1 WHERE c1 = 1;
/*+IndexScan(p1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.p1 WHERE c1 = 1;

-- No. S-3-8-3
EXPLAIN (COSTS false) UPDATE ONLY s1.p1 SET c4 = c4 WHERE c1 = 1;
/*+IndexScan(p1)*/
EXPLAIN (COSTS false) UPDATE ONLY s1.p1 SET c4 = c4 WHERE c1 = 1;
/*+IndexScan(p1 p1_pkey)*/
EXPLAIN (COSTS false) UPDATE ONLY s1.p1 SET c4 = c4 WHERE c1 = 1;

-- No. S-3-8-4
EXPLAIN (COSTS false) UPDATE s1.p1 SET c4 = c4 WHERE c1 = 1;
/*+IndexScan(p1)*/
EXPLAIN (COSTS false) UPDATE s1.p1 SET c4 = c4 WHERE c1 = 1;
/*+IndexScan(p1 p1_pkey)*/
EXPLAIN (COSTS false) UPDATE s1.p1 SET c4 = c4 WHERE c1 = 1;

----
---- No. S-3-9 inheritance table number
----

-- No. S-3-9-1
EXPLAIN (COSTS false) SELECT * FROM s1.p1 WHERE c1 = 1;
/*+IndexScan(p1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.p1 WHERE c1 = 1;

-- No. S-3-9-2
EXPLAIN (COSTS false) SELECT * FROM s1.p2 WHERE c1 = 1;
/*+IndexScan(p2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.p2 WHERE c1 = 1;

----
---- No. S-3-10 inheritance table specified table
----

EXPLAIN (COSTS false) SELECT * FROM s1.p2 WHERE c1 = 1;

-- No. S-3-10-1
/*+IndexScan(p2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.p2 WHERE c1 = 1;

-- No. S-3-10-2
/*+IndexScan(p2c1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.p2 WHERE c1 = 1;

-- No. S-3-10-3
\o results/ut-S.tmpout
EXPLAIN SELECT c4 FROM s1.p1 WHERE c2 * 2 < 100 AND c1 < 10;
\o
\! sql/maskout.sh results/ut-S.tmpout

\o results/ut-S.tmpout
/*+IndexScan(p1 p1_parent)*/ EXPLAIN SELECT c4 FROM s1.p1 WHERE c2 * 2 < 100 AND c1 < 10;
\o
\! sql/maskout.sh results/ut-S.tmpout


-- No. S-3-10-4
\o results/ut-S.tmpout
/*+IndexScan(p1 p1_i2)*/ EXPLAIN SELECT c2 FROM s1.p1 WHERE c2 = 1;
\o
\! sql/maskout.sh results/ut-S.tmpout

-- No. S-3-10-5
\o results/ut-S.tmpout
/*+IndexScan(p2 p2c1_pkey)*/ EXPLAIN (COSTS true) SELECT * FROM s1.p2 WHERE c1 = 1;
\o
\! sql/maskout.sh results/ut-S.tmpout


----
---- No. S-3-12 specified same table
----

-- No. S-3-12-1
/*+IndexScan(ti1) BitmapScan(ti1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-12-2
/*+IndexScan(ti1 ti1_pkey) BitmapScan(ti1 ti1_btree)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-12-3
/*+BitmapScan(ti1) IndexScan(ti1) BitmapScan(ti1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-12-4
/*+BitmapScan(ti1 ti1_hash) IndexScan(ti1 ti1_pkey) BitmapScan(ti1 ti1_btree)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

----
---- No. S-3-13 message output of hint
----

-- No. S-3-13-1
/*+SeqScan(ti1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-13-2
/*+SeqScan(ti1 ti1_pkey)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-13-3
/*+SeqScan(ti1 ti1_pkey ti1_btree)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-13-4
/*+IndexScan(ti1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-13-5
/*+IndexScan(ti1 ti1_pkey)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-13-6
/*+IndexScan(ti1 ti1_pkey ti1_btree)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-13-7
/*+BitmapScan(ti1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-13-8
/*+BitmapScan(ti1 ti1_pkey)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-13-9
/*+BitmapScan(ti1 ti1_pkey ti1_btree)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-13-10
/*+TidScan(ti1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-13-11
/*+TidScan(ti1 ti1_pkey)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-13-12
/*+TidScan(ti1 ti1_pkey ti1_btree)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-13-13
/*+NoSeqScan(ti1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-13-14
/*+NoSeqScan(ti1 ti1_pkey)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-13-15
/*+NoSeqScan(ti1 ti1_pkey ti1_btree)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-13-16
/*+NoIndexScan(ti1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-13-17
/*+NoIndexScan(ti1 ti1_pkey)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-13-18
/*+NoIndexScan(ti1 ti1_pkey ti1_btree)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-13-19
/*+NoBitmapScan(ti1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-13-20
/*+NoBitmapScan(ti1 ti1_pkey)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-13-21
/*+NoBitmapScan(ti1 ti1_pkey ti1_btree)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-13-22
/*+NoTidScan(ti1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-13-23
/*+NoTidScan(ti1 ti1_pkey)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-13-24
/*+NoTidScan(ti1 ti1_pkey ti1_btree)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. S-3-13-25
/*+IndexOnlyScan(ti1)*/
EXPLAIN (COSTS false) SELECT c1 FROM s1.ti1 WHERE c1 >= 1;

-- No. S-3-13-26
/*+IndexOnlyScan(ti1 ti1_pkey)*/
EXPLAIN (COSTS false) SELECT c1 FROM s1.ti1 WHERE c1 >= 1;

-- No. S-3-13-27
/*+IndexOnlyScan(ti1 ti1_pkey ti1_btree)*/
EXPLAIN (COSTS false) SELECT c1 FROM s1.ti1 WHERE c1 >= 1;

-- No. S-3-13-28
/*+NoIndexOnlyScan(ti1)*/
EXPLAIN (COSTS false) SELECT c1 FROM s1.ti1 WHERE c1 = 1;

-- No. S-3-13-29
/*+NoIndexOnlyScan(ti1 ti1_pkey)*/
EXPLAIN (COSTS false) SELECT c1 FROM s1.ti1 WHERE c1 = 1;

-- No. S-3-13-30
/*+NoIndexOnlyScan(ti1 ti1_pkey ti1_btree)*/
EXPLAIN (COSTS false) SELECT c1 FROM s1.ti1 WHERE c1 = 1;


----
---- No. S-3-14 regular expression
----
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c2 = 1;

-- No. S-3-14-1
/*+IndexScanRegexp(ti1 ti1_.*_key)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c2 = 1;

-- No. S-3-14-2
/*+IndexScanRegexp(ti1 ti1_i.)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c2 = 1;

-- No. S-3-14-3
/*+IndexScanRegexp(ti1 no.*_exist)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c2 = 1;

-- No. S-3-14-4
/*+IndexScanRegexp(p1 .*pkey)*/
EXPLAIN (COSTS false) SELECT * FROM s1.p1 WHERE c1 = 1;

-- No. S-3-14-5
/*+IndexScanRegexp(p1 p1.*i)*/
EXPLAIN (COSTS false) SELECT * FROM s1.p1 WHERE c1 = 1;

-- No. S-3-14-6
/*+IndexScanRegexp(p1 no.*_exist)*/
EXPLAIN (COSTS false) SELECT * FROM s1.p1 WHERE c1 = 1;

----
---- No. S-3-15 message output of index candidate
----

-- No. S-3-15-1
/*+IndexScan(ti1 ti1_i1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c2 = 1;
-- No. S-3-15-2
/*+IndexScan(ti1 not_exist)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c2 = 1;
-- No. S-3-15-3
/*+IndexScan(ti1 ti1_i1 ti1_i2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c2 = 1;
-- No. S-3-15-4
/*+IndexScan(ti1 ti1_i1 not_exist)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c2 = 1;
-- No. S-3-15-5
/*+IndexScan(ti1 not_exist1 not_exist2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c2 = 1;

DELETE FROM pg_db_role_setting WHERE setrole = (SELECT oid FROM pg_roles WHERE rolname = current_user);

ALTER SYSTEM SET session_preload_libraries TO DEFAULT;
SELECT pg_reload_conf();
\! rm results/ut-S.tmpout
