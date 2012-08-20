LOAD 'pg_hint_plan';
SET pg_hint_plan.enable TO on;
SET pg_hint_plan.debug_print TO on;
SET client_min_messages TO LOG;
SET search_path TO public;

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

-- No. J-1-3-1
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
-- refer fdw.sql

-- No. J-1-6-7
EXPLAIN (COSTS false) SELECT * FROM s1.f1() t1, s1.f1() t2 WHERE t1.c1 = t2.c1;
/*+HashJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.f1() t1, s1.f1() t2 WHERE t1.c1 = t2.c1;

-- No. J-1-6-8
EXPLAIN (COSTS false) SELECT * FROM (VALUES(1,1,1,'1'), (2,2,2,'2'), (3,3,3,'3')) AS t1 (c1, c2, c3, c4), (VALUES(1,1,1,'1'), (2,2,2,'2'), (3,3,3,'3')) AS t2 (c1, c2, c3, c4) WHERE t1.c1 = t2.c1;
/*+NestLoop(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM (VALUES(1,1,1,'1'), (2,2,2,'2'), (3,3,3,'3')) AS t1 (c1, c2, c3, c4), (VALUES(1,1,1,'1'), (2,2,2,'2'), (3,3,3,'3')) AS t2 (c1, c2, c3, c4) WHERE t1.c1 = t2.c1;
/*+NestLoop(*VALUES* *VALUES*)*/
EXPLAIN (COSTS false) SELECT * FROM (VALUES(1,1,1,'1'), (2,2,2,'2'), (3,3,3,'3')) AS t1 (c1, c2, c3, c4), (VALUES(1,1,1,'1'), (2,2,2,'2'), (3,3,3,'3')) AS t2 (c1, c2, c3, c4) WHERE t1.c1 = t2.c1;

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
/*+MergeJoin(t1 t2)NestLoop(st1 st2)*/
EXPLAIN (COSTS true) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1 AND t1.c1 = (SELECT max(st1.c1) FROM s1.t1 st1, s1.t2 st2 WHERE st1.c1 = st2.c1);

EXPLAIN (COSTS false) SELECT * FROM s1.t1, (SELECT t2.c1 FROM s1.t2) st2 WHERE t1.c1 = st2.c1;
/*+HashJoin(t1 st2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, (SELECT t2.c1 FROM s1.t2) st2 WHERE t1.c1 = st2.c1;
/*+HashJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, (SELECT t2.c1 FROM s1.t2) st2 WHERE t1.c1 = st2.c1;

\q

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

-- No. S-3-4-10
/*+BitmapScan(ti1 ti1_pkey)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 < 100 AND c2 = 1 AND lower(c4) = '1' AND to_tsvector('english', c4) @@ 'a & b' AND ctid = '(1,1)';

-- No. S-3-4-11
/*+BitmapScan(ti1 ti1_c2_key)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 < 100 AND c2 = 1 AND lower(c4) = '1' AND to_tsvector('english', c4) @@ 'a & b' AND ctid = '(1,1)';

----
---- No. S-3-5 not used index
----

-- No. S-3-5-1
/*+IndexScan(ti1 ti1_pred)*/
EXPLAIN (COSTS true) SELECT * FROM s1.ti1 WHERE c1 = 100;

-- No. S-3-5-2
/*+BitmapScan(ti1 ti1_pred)*/
EXPLAIN (COSTS true) SELECT * FROM s1.ti1 WHERE c1 = 100;

----
---- No. S-3-6 not exist index
----

-- No. S-3-6-1
/*+IndexScan(ti1 not_exist)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 100;

-- No. S-3-6-2
/*+BitmapScan(ti1 not_exist)*/
EXPLAIN (COSTS false) SELECT * FROM s1.ti1 WHERE c1 = 100;

----
---- No. S-3-7 query structure
----

EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1 AND t1.ctid = '(1,1)';

-- No. S-3-7-1
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE c1 = 100;

-- No. S-3-7-2
/*+SeqScan(t1)BitmapScan(t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1 AND t1.ctid = '(1,1)';

-- No. S-3-7-3
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1 AND t1.ctid = '(1,1)';

----
---- No. S-3-8 query structure
----

-- No. S-3-8-1
EXPLAIN (COSTS false) 
WITH c1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1 WHERE b1t1.c1 = 1)
SELECT max(b3t1.c1), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1 WHERE b2t1.c1 = 1
                  ) FROM s1.t1 b3t1 WHERE b3t1.c1 = (
SELECT max(b4t1.c1) FROM s1.t1 b4t1 WHERE b4t1.c1 = 1);
/*+BitmapScan(b1t1)BitmapScan(b2t1)BitmapScan(b3t1)BitmapScan(b4t1)*/
EXPLAIN (COSTS false) 
WITH c1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1 WHERE b1t1.c1 = 1)
SELECT max(b3t1.c1), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1 WHERE b2t1.c1 = 1
                  ) FROM s1.t1 b3t1 WHERE b3t1.c1 = (
SELECT max(b4t1.c1) FROM s1.t1 b4t1 WHERE b4t1.c1 = 1);

-- No. S-3-8-2
EXPLAIN (COSTS false) 
WITH cte1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1 JOIN s1.t2 b1t2 ON(b1t1.c1 = b1t2.c1) WHERE b1t1.c1 = 1)
SELECT max(b3t1.c1), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1 JOIN s1.t2 b2t2 ON(b2t1.c1 = b2t2.c1) WHERE b2t1.c1 = 1
                  ) FROM s1.t1 b3t1 JOIN s1.t2 b3t2 ON(b3t1.c1 = b3t2.c1) JOIN cte1 ON(b3t1.c1 = cte1.c1) WHERE b3t1.c1 = (
SELECT max(b4t1.c1) FROM s1.t1 b4t1 JOIN s1.t2 b4t2 ON(b4t1.c1 = b4t2.c1) WHERE b4t1.c1 = 1);
/*+BitmapScan(b1t1)BitmapScan(b2t1)BitmapScan(b3t1)BitmapScan(b4t1)BitmapScan(b1t2)BitmapScan(b2t2)BitmapScan(b3t2)BitmapScan(b4t2)*/
EXPLAIN (COSTS false) 
WITH cte1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1 JOIN s1.t2 b1t2 ON(b1t1.c1 = b1t2.c1) WHERE b1t1.c1 = 1)
SELECT max(b3t1.c1), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1 JOIN s1.t2 b2t2 ON(b2t1.c1 = b2t2.c1) WHERE b2t1.c1 = 1
                  ) FROM s1.t1 b3t1 JOIN s1.t2 b3t2 ON(b3t1.c1 = b3t2.c1) JOIN cte1 ON(b3t1.c1 = cte1.c1) WHERE b3t1.c1 = (
SELECT max(b4t1.c1) FROM s1.t1 b4t1 JOIN s1.t2 b4t2 ON(b4t1.c1 = b4t2.c1) WHERE b4t1.c1 = 1);

-- No. S-3-8-3
EXPLAIN (COSTS false) 
WITH cte1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1 JOIN s1.t2 b1t2 ON(b1t1.c1 = b1t2.c1) WHERE b1t1.c1 = 1)
SELECT max(b3t1.c1), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1 WHERE b2t1.c1 = 1
                  ) FROM s1.t1 b3t1 JOIN s1.t2 b3t2 ON(b3t1.c1 = b3t2.c1) JOIN cte1 ON(b3t1.c1 = cte1.c1) WHERE b3t1.c1 = (
SELECT max(b4t1.c1) FROM s1.t1 b4t1 WHERE b4t1.c1 = 1);
/*+BitmapScan(b1t1)BitmapScan(b2t1)BitmapScan(b3t1)BitmapScan(b4t1)BitmapScan(b1t2)BitmapScan(b3t2)*/
EXPLAIN (COSTS false) 
WITH cte1 (c1) AS (
SELECT max(b1t1.c1) FROM s1.t1 b1t1 JOIN s1.t2 b1t2 ON(b1t1.c1 = b1t2.c1) WHERE b1t1.c1 = 1)
SELECT max(b3t1.c1), (
SELECT max(b2t1.c1) FROM s1.t1 b2t1 WHERE b2t1.c1 = 1
                  ) FROM s1.t1 b3t1 JOIN s1.t2 b3t2 ON(b3t1.c1 = b3t2.c1) JOIN cte1 ON(b3t1.c1 = cte1.c1) WHERE b3t1.c1 = (
SELECT max(b4t1.c1) FROM s1.t1 b4t1 WHERE b4t1.c1 = 1);

----
---- No. S-3-9 inheritance table select type
----

-- No. S-3-9-1
EXPLAIN (COSTS false) SELECT * FROM ONLY s1.p1 WHERE c1 = 1;
/*+IndexScan(p1)*/
EXPLAIN (COSTS false) SELECT * FROM ONLY s1.p1 WHERE c1 = 1;

-- No. S-3-9-2
EXPLAIN (COSTS false) SELECT * FROM s1.p1 WHERE c1 = 1;
/*+IndexScan(p1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.p1 WHERE c1 = 1;

----
---- No. S-3-10 inheritance table number
----

-- No. S-3-10-1
EXPLAIN (COSTS false) SELECT * FROM s1.p1 WHERE c1 = 1;
/*+IndexScan(p1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.p1 WHERE c1 = 1;

-- No. S-3-10-2
EXPLAIN (COSTS false) SELECT * FROM s1.p2 WHERE c1 = 1;
/*+IndexScan(p2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.p2 WHERE c1 = 1;

----
---- No. S-3-11 inheritance table specified table
----

EXPLAIN (COSTS false) SELECT * FROM s1.p2 WHERE c1 = 1;

-- No. S-3-11-1
/*+IndexScan(p2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.p2 WHERE c1 = 1;

-- No. S-3-11-2
/*+IndexScan(p2c1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.p2 WHERE c1 = 1;

-- No. S-3-11-3
/*+IndexScan(p2 p2_pkey p2c1_pkey p2c1c1_pkey)*/
EXPLAIN (COSTS false) SELECT * FROM s1.p2 WHERE c1 = 1;

-- No. S-3-11-4
/*+IndexScan(p2 p2c1_pkey)*/
EXPLAIN (COSTS true) SELECT * FROM s1.p2 WHERE c1 = 1;

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
---- No. S-3-13 message output
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

