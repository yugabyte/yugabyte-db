EXPLAIN SELECT * FROM t1, t2 WHERE t1.id = t2.id;
EXPLAIN SELECT * FROM t1, t2 WHERE t1.val = t2.val;

LOAD 'pg_hint_plan';

EXPLAIN SELECT * FROM t1, t2 WHERE t1.id = t2.id;
EXPLAIN SELECT * FROM t1, t2 WHERE t1.val = t2.val;

/* Test (t1 t2) */
EXPLAIN SELECT * FROM t1, t2 WHERE t1.id = t2.id;
SET pg_hint_plan.enable TO off;
/* Test (t1 t2) */
EXPLAIN SELECT * FROM t1, t2 WHERE t1.id = t2.id;
SET pg_hint_plan.enable TO on;

\q
/* NestLoop (t1 t2) */
EXPLAIN SELECT * FROM t1, t2 WHERE t1.id = t2.id;
/* NestLoop (t1 t2) */
EXPLAIN SELECT * FROM t1, t2 WHERE t1.val = t2.val;

/* HashJoin (t1 t2) */
EXPLAIN SELECT * FROM t1, t2 WHERE t1.id = t2.id;
/* HashJoin (t1 t2) */
EXPLAIN SELECT * FROM t1, t2 WHERE t1.val = t2.val;

/* MergeJoin (t1 t2) */
EXPLAIN SELECT * FROM t1, t2 WHERE t1.id = t2.id;
/* MergeJoin (t1 t2) */
EXPLAIN SELECT * FROM t1, t2 WHERE t1.val = t2.val;

/* NoMergeJoin (t1 t2) */
EXPLAIN SELECT * FROM t1, t2 WHERE t1.id = t2.id;
/* NoHashJoin (t1 t2) */
EXPLAIN SELECT * FROM t1, t2 WHERE t1.val = t2.val;

EXPLAIN SELECT * FROM t1, t2, t3 WHERE t1.id = t2.id AND t2.id = t3.id;
EXPLAIN SELECT * FROM t1, t2, t3 WHERE t1.val = t2.val AND t2.val = t3.val;

/* NestLoop (t1 t2 t3) NestLoop (t1 t3) */
EXPLAIN SELECT * FROM t1, t2, t3 WHERE t1.id = t2.id AND t2.id = t3.id;
/* NestLoop (t1 t2 t3) NestLoop (t1 t3) */
EXPLAIN SELECT * FROM t1, t2, t3 WHERE t1.val = t2.val AND t2.val = t3.val;

EXPLAIN SELECT * FROM t1, t2, t3, t4 WHERE t1.id = t2.id AND t2.id = t3.id AND t3.id = t4.id;
EXPLAIN SELECT * FROM t1, t2, t3, t4 WHERE t1.id = t2.id AND t2.id = t3.id AND t3.id = t4.id;
SET enable_mergejoin TO off;
EXPLAIN SELECT * FROM t1, t2, t3, t4 WHERE t1.id = t2.id AND t2.id = t3.id AND t3.id = t4.id;
EXPLAIN SELECT * FROM t1, t2, t3, t4 WHERE t1.id = t2.id AND t2.id = t3.id AND t3.id = t4.id;
EXPLAIN SELECT * FROM t1, t2, t3, t4 WHERE t1.id = t2.id AND t2.id = t3.id AND t3.id = t4.id;
SET enable_mergejoin TO on;

SET join_collapse_limit TO 10;
EXPLAIN SELECT * FROM t1 CROSS JOIN t2 CROSS JOIN t3 CROSS JOIN t4 WHERE t1.id = t2.id AND t2.id = t3.id AND t3.id = t4.id;
SET join_collapse_limit TO 1;
EXPLAIN SELECT * FROM t1 CROSS JOIN t2 CROSS JOIN t3 CROSS JOIN t4 WHERE t1.id = t2.id AND t2.id = t3.id AND t3.id = t4.id;
EXPLAIN SELECT * FROM t2 CROSS JOIN t3 CROSS JOIN t4 CROSS JOIN t1 WHERE t1.id = t2.id AND t2.id = t3.id AND t3.id = t4.id;
EXPLAIN SELECT * FROM t1 CROSS JOIN (t2 CROSS JOIN t3 CROSS JOIN t4) WHERE t1.id = t2.id AND t2.id = t3.id AND t3.id = t4.id;
EXPLAIN SELECT * FROM t1 JOIN t2 ON (t1.id = t2.id) JOIN t3 ON (t2.id = t3.id) JOIN t4 ON (t3.id = t4.id);

EXPLAIN SELECT * FROM v2;
EXPLAIN SELECT * FROM v3 v_3;
EXPLAIN SELECT * FROM v2 v_2, v3 v_3 WHERE v_2.t1_id = v_3.t1_id;

EXPLAIN SELECT * FROM t1 t_1, v2 v_2 WHERE t_1.id = v_2.t1_id;
SET from_collapse_limit TO 1;
EXPLAIN SELECT * FROM t1 t_1, v2 v_2 WHERE t_1.id = v_2.t1_id;

/* test */ EXPLAIN SELECT * FROM t1, t2 WHERE t1.id = t2.id;
/* HashJoin(t1 t2) MergeJoin(t_1 t_2) NestLoop(t2 t1)*/ EXPLAIN SELECT * FROM t1, t2 WHERE t1.id = t2.id;
SET join_collapse_limit TO 10;
/* Set(join_collapse_limit "1") */ EXPLAIN SELECT * FROM t1, t2 WHERE t1.id = t2.id;
/* Set(join_collapse_limit "10") */ EXPLAIN SELECT * FROM t1 CROSS JOIN t2 CROSS JOIN t3 CROSS JOIN t4 WHERE t1.id = t2.id AND t2.id = t3.id AND t3.id = t4.id;
SHOW join_collapse_limit;
/* Set(join_collapse_limit "1") */ EXPLAIN SELECT * FROM t1 CROSS JOIN t2 CROSS JOIN t3 CROSS JOIN t4 WHERE t1.id = t2.id AND t2.id = t3.id AND t3.id = t4.id;
SHOW join_collapse_limit;
/* Set(join_collapse_limit "1") */ EXPLAIN SELECT * FROM t1 CROSS JOIN t2 CROSS JOIN t3 CROSS JOIN t4 WHERE t1.id = t2.id AND t2.id = t3.id AND t3.id = t4.id;
SHOW join_collapse_limit;

/* Set(cursor_tuple_fraction "1") */ EXPLAIN DECLARE c1 CURSOR FOR SELECT * FROM t1, t2 WHERE t1.id = t2.id AND t2.val = 1 ORDER BY t1.id;
/* Set(cursor_tuple_fraction "0.5") */ EXPLAIN DECLARE c1 CURSOR FOR SELECT * FROM t1, t2 WHERE t1.id = t2.id AND t2.val = 1 ORDER BY t1.id;
/* Set (cursor_tuple_fraction "0.4") */ EXPLAIN DECLARE c1 CURSOR FOR SELECT * FROM t1, t2 WHERE t1.id = t2.id AND t2.val = 1 ORDER BY t1.id;

/* Leading (t1 t2 t3 t4) */EXPLAIN SELECT * FROM t1, t2, t3, t4 WHERE t1.id = t2.id AND t2.id = t3.id AND t3.id = t4.id;
/* Leading (t4 t3 t2 t1) */EXPLAIN SELECT * FROM t1, t2, t3, t4 WHERE t1.id = t2.id AND t2.id = t3.id AND t3.id = t4.id;

EXPLAIN SELECT * FROM t1, t2 WHERE t1.id < 1 AND t2.id < 1 AND t1.id = t2.id;
/* SeqScan (t1) */
EXPLAIN SELECT * FROM t1, t2 WHERE t1.id < 1 AND t2.id < 1 AND t1.id = t2.id;
/* SeqScan (t2) */
EXPLAIN SELECT * FROM t1, t2 WHERE t1.id < 1 AND t2.id < 1 AND t1.id = t2.id;
/* SeqScan (t1) SeqScan (t2) */
EXPLAIN SELECT * FROM t1, t2 WHERE t1.id < 1 AND t2.id < 1 AND t1.id = t2.id;
/* BitmapScan(t1) */
EXPLAIN SELECT * FROM t1, t2 WHERE t1.id < 1 AND t2.id < 1 AND t1.id = t2.id;
/* BitmapScan(t2) */
EXPLAIN SELECT * FROM t1, t2 WHERE t1.id < 1 AND t2.id < 1 AND t1.id = t2.id;
/* BitmapScan (t1) BitmapScan (t2) */
EXPLAIN SELECT * FROM t1, t2 WHERE t1.id < 1 AND t2.id < 1 AND t1.id = t2.id;
-- TID SCAN試験
EXPLAIN SELECT * FROM t1, t4 WHERE t1.id < 1 AND t4.id < 1 AND t1.id = t4.id AND t1.ctid = '(1, 1)' AND t4.ctid = '(1, 1)';
/* TidScan(t4) */
EXPLAIN SELECT * FROM t1, t4 WHERE t1.id < 1 AND t4.id < 1 AND t1.id = t4.id AND t1.ctid = '(1, 1)' AND t4.ctid = '(1, 1)';
/* IndexScan(t1) TidScan(t4)*/
EXPLAIN SELECT * FROM t1, t4 WHERE t1.id < 1 AND t4.id < 1 AND t1.id = t4.id AND t1.ctid = '(1, 1)' AND t4.ctid = '(1, 1)';
-- NO～試験
EXPLAIN SELECT * FROM t1, t4 WHERE t1.id < 1 AND t4.id < 1 AND t1.id = t4.id AND t1.ctid = '(1, 1)' AND t4.ctid = '(1, 1)';
/* NoTidScan(t1) */
EXPLAIN SELECT * FROM t1, t4 WHERE t1.id < 1 AND t4.id < 1 AND t1.id = t4.id AND t1.ctid = '(1, 1)' AND t4.ctid = '(1, 1)';
/* NoSeqScan(t4) NoTidScan(t1) */
EXPLAIN SELECT * FROM t1, t4 WHERE t1.id < 1 AND t4.id < 1 AND t1.id = t4.id AND t1.ctid = '(1, 1)' AND t4.ctid = '(1, 1)';
/* NoSeqScan(t4) NoTidScan(t1) NoNestLoop(t1 t4)*/
EXPLAIN SELECT * FROM t1, t4 WHERE t1.id < 1 AND t4.id < 1 AND t1.id = t4.id AND t1.ctid = '(1, 1)' AND t4.ctid = '(1, 1)';

EXPLAIN SELECT * FROM t1, t5 WHERE t1.id = t5.id AND t1.id < 1000 AND t5.val < 10;
/* NoIndexScan(t1) */
EXPLAIN SELECT * FROM t1, t5 WHERE t1.id = t5.id AND t1.id < 1000 AND t5.val < 10;
/* NoBitmapScan(t5) NoIndexScan(t1) */
EXPLAIN SELECT * FROM t1, t5 WHERE t1.id = t5.id AND t1.id < 1000 AND t5.val < 10;

--EXPLAIN SELECT * FROM t1, t2 WHERE t1.id = t2.id;
--EXPLAIN SELECT * FROM t1, t4 WHERE t1.id = t4.id;

EXPLAIN SELECT * FROM t1, t3 WHERE t1.id = t3.id;
/* IndexScan (t1) */
EXPLAIN SELECT * FROM t1, t3 WHERE t1.id = t3.id;
/* IndexScan (t3) */
EXPLAIN SELECT * FROM t1, t3 WHERE t1.id = t3.id;
/* SeqScan (t1) IndexScan (t3) */
EXPLAIN SELECT * FROM t1, t3 WHERE t1.id = t3.id;
/* SeqScan (t3) IndexScan (t1) */
EXPLAIN SELECT * FROM t1, t3 WHERE t1.id = t3.id;
/* IndexScan (t1) IndexScan (t3) */
EXPLAIN SELECT * FROM t1, t3 WHERE t1.id = t3.id;

EXPLAIN SELECT * FROM t1 WHERE id < 10000;
/* IndexScan (t1) */
EXPLAIN SELECT * FROM t1 WHERE id < 10000;

EXPLAIN SELECT * FROM t1 WHERE id = 1;
/* SeqScan (t1) */
EXPLAIN SELECT * FROM t1 WHERE id = 1;

EXPLAIN SELECT * FROM t1, t4 WHERE t1.id > 100;
/* IndexScan (t1) */
EXPLAIN SELECT * FROM t1, t4 WHERE t1.id > 100;
/* IndexScan (t4) */
EXPLAIN SELECT * FROM t1, t4 WHERE t1.id > 100;
/* Set (enable_seqscan off) Set (enable_indexscan off) */
EXPLAIN SELECT * FROM t1, t4 WHERE t1.id > 100;

CREATE INDEX t1_val ON t1 (val);
CREATE INDEX t1_val_id ON t1 (val, id);
EXPLAIN SELECT * FROM t1, t3 WHERE t1.val = t3.val;
/* IndexScan (t1 t1_val_id) */
EXPLAIN SELECT * FROM t1, t3 WHERE t1.val = t3.val;
/* IndexScan (t1 t1_pkey) */
EXPLAIN SELECT * FROM t1, t3 WHERE t1.val = t3.val;
/* IndexScan (t1 t1_val_id t1_val) */
EXPLAIN SELECT * FROM t1, t3 WHERE t1.val = t3.val;
/* IndexScan (t1 t2_val) */
EXPLAIN SELECT * FROM t1, t3 WHERE t1.val = t3.val;

SET pg_hint_plan.debug_print TO true;
/* HashJoin(a b c A B C z y x Z Y X) HashJoin (t1 t3) MergeJoin(a b c A B C z y x Z Y X) MergeJoin (t3 t1) NestLoop(a b c A B C z y x Z Y X) */
EXPLAIN SELECT * FROM t1, t3 WHERE t1.val = t3.val;
CREATE INDEX t3_val ON t3 (val);
/* Leading(t3) IndexScan(t4) Set (cursor_tuple_fraction "1.0") Leading(t3 t1 t4) HashJoin(t3 t1) BitmapScan(t3) HashJoin(a b c) IndexScan(t2) MergeJoin(A B C) MergeJoin (B C A) NestLoop(b c a) MergeJoin(c a b) IndexScan(t3) NestLoop(C A B) IndexScan(t1) SeqScan(t1) SeqScan(t1) SeqScan(t1) MergeJoin(t3 t1) Leading(t2 t1 t4)*/
EXPLAIN SELECT * FROM t1, t3 WHERE t1.val = t3.val ORDER BY t3.val;

EXPLAIN SELECT * FROM t1 WHERE t1.val < 10000;
/* Set(enable_seqscan "off") */
EXPLAIN SELECT * FROM t1 WHERE t1.val < 10000;
/* IndexScan(t1) */
EXPLAIN SELECT * FROM t1 WHERE t1.val < 10000;

EXPLAIN SELECT * FROM v1 WHERE v1.val < 10000;
/* Set(enable_seqscan "off") */
EXPLAIN SELECT * FROM v1 WHERE v1.val < 10000;
/* IndexScan(t1) */
EXPLAIN SELECT * FROM v1 WHERE v1.val < 10000;

EXPLAIN SELECT * FROM v4;
/* Set(enable_seqscan "off") */
EXPLAIN SELECT * FROM v4;
/* BitmapScan(t2) */
EXPLAIN SELECT * FROM v2;
/* BitmapScan(t_3) */
EXPLAIN SELECT * FROM v4;

EXPLAIN SELECT * FROM t1, t2, t3, t4 WHERE t1.id = t2.id AND t2.id = t3.id AND t3.id = t4.id;
/* NestLoop(t1 t2) NoNestLoop(t1 t2 t4) Leading(t1 t2 t3 t4) */
EXPLAIN SELECT * FROM t1, t2, t3, t4 WHERE t1.id = t2.id AND t2.id = t3.id AND t3.id = t4.id;
