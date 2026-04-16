LOAD 'pg_hint_plan';
SET pg_hint_plan.enable_hint TO on;
SET pg_hint_plan.debug_print TO on;
SET client_min_messages TO LOG;
SET search_path TO public;

----
---- No. G-1-1 RULE definition table
----

-- No. G-1-1-1
EXPLAIN (COSTS false) UPDATE s1.r1 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+
Set(enable_tidscan off)Set(enable_nestloop off)
*/
EXPLAIN (COSTS false) UPDATE s1.r1 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
EXPLAIN (COSTS false) UPDATE s1.r1_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+
Set(enable_tidscan off)Set(enable_nestloop off)
*/
EXPLAIN (COSTS false) UPDATE s1.r1_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. G-1-1-2
EXPLAIN (COSTS false) UPDATE s1.r2 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+
Set(enable_tidscan off)Set(enable_nestloop off)
*/
EXPLAIN (COSTS false) UPDATE s1.r2 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
EXPLAIN (COSTS false) UPDATE s1.r2_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+
Set(enable_tidscan off)Set(enable_nestloop off)
*/
EXPLAIN (COSTS false) UPDATE s1.r2_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';

-- No. G-1-1-3
EXPLAIN (COSTS false) UPDATE s1.r3 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+
Set(enable_tidscan off)Set(enable_nestloop off)
*/
EXPLAIN (COSTS false) UPDATE s1.r3 SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
EXPLAIN (COSTS false) UPDATE s1.r3_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';
/*+
Set(enable_tidscan off)Set(enable_nestloop off)
*/
EXPLAIN (COSTS false) UPDATE s1.r3_ SET c1 = c1 WHERE c1 = 1 AND ctid = '(1,1)';

RESET client_min_messages;

----
---- No. G-2-1 GUC parameter
----

-- No. G-2-1-3
/*+Set(1234567890123456789012345678901234567890123456789012345678901234 1)*/
SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. G-2-1-4
/*+Set(constraint_exclusion 1234567890123456789012345678901234567890123456789012345678901234)*/
SELECT * FROM s1.t1 WHERE t1.c1 = 1;

----
---- No. G-2-2 category of GUC parameter and role
----

-- No. G-2-2-1
SET ROLE regress_super_user;
/*+Set(block_size 16384)*/
SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. G-2-2-2
/*+Set(archive_mode off)*/
SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. G-2-2-3
/*+Set(archive_timeout 0)*/
SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. G-2-2-4
/*+Set(log_connections off)*/
SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. G-2-2-5
/*+Set(log_min_messages WARNING)*/
SELECT * FROM s1.t1 WHERE t1.c1 = 1;
RESET ROLE;

-- No. G-2-2-6
GRANT ALL ON SCHEMA s1 TO PUBLIC;
GRANT SELECT ON ALL TABLES IN SCHEMA s1 TO regress_normal_user;
SET ROLE regress_normal_user;
/*+Set(log_min_messages WARNING)*/
SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. G-2-2-7
/*+Set(enable_seqscan on)*/
SELECT * FROM s1.t1 WHERE t1.c1 = 1;

RESET ROLE;
REVOKE SELECT ON ALL TABLES IN SCHEMA s1 FROM regress_normal_user;
REVOKE ALL ON SCHEMA s1 FROM PUBLIC;

----
---- No. G-2-3 conflict set hint
----

SET client_min_messages TO LOG;
-- No. G-2-3-1
/*+Set(enable_indexscan on)Set(enable_indexscan off)*/
SELECT * FROM s1.t1 WHERE false;

-- No. G-2-3-2
/*+Set(client_min_messages DEBUG5)Set(client_min_messages WARNING)Set(client_min_messages DEBUG2)*/
SELECT * FROM s1.t1 WHERE false;

-- No. G-2-3-3
/*+Set(enable_indexscan on)Set(enable_indexscan o)*/
SELECT * FROM s1.t1 WHERE false;

-- No. G-2-3-4
/*+Set(client_min_messages DEBUG5)Set(client_min_messages WARNING)Set(client_min_messages DEBU)*/
SELECT * FROM s1.t1 WHERE false;

----
---- No. G-2-4 debug message
----

-- No. G-2-4-1
/*+SeqScan(a)IndexScan(a)SeqScan(c)NestLoop(a) */
SELECT * FROM s1.t1 a, s1.t2 b WHERE false;
