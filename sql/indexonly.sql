LOAD 'pg_hint_plan';
SET pg_hint_plan.enable_hint TO on;
SET pg_hint_plan.debug_print TO on;
SET client_min_messages TO LOG;
SET search_path TO public;

----
---- No. S-3-1 scan method hint
----

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

EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 WHERE ti1.c2 >= 1;

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

-- No. S-3-5-3
/*+IndexOnlyScan(ti1 ti1_pred)*/
EXPLAIN (COSTS true) SELECT c1 FROM s1.ti1 WHERE c1 = 100;

----
---- No. S-3-6 not exist index
----

-- No. S-3-6-3
/*+IndexOnlyScan(ti1 not_exist)*/
EXPLAIN (COSTS true) SELECT c1 FROM s1.ti1 WHERE c1 = 100;

----
---- No. S-3-13 message output
----

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
