EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.val = t2.val;

LOAD 'pg_hint_plan';
SET pg_hint_plan.debug_print TO on;
SET client_min_messages TO LOG;

EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.val = t2.val;

/*+ Test (t1 t2) */
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
SET pg_hint_plan.enable TO off;
/*+ Test (t1 t2) */
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
SET pg_hint_plan.enable TO on;

/*+Set(enable_indexscan off)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
/*+ Set(enable_indexscan off) Set(enable_hashjoin off) */
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;

/*+ 	 Set 	 ( 	 enable_indexscan 	 off 	 ) 	 */
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
/*+ 	 
	 	Set 	 
	 	( 	 
	 	enable_indexscan 	 
	 	off 	 
	 	) 	 
	 	*/	 	
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
/*+ Set(enable_indexscan off)Set(enable_nestloop off)Set(enable_mergejoin off)	 	
	 	Set(enable_seqscan off)
	 	*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
/*+Set(work_mem "1M")*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
/*+Set(work_mem "1MB")*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
/*+Set(work_mem TO "1MB")*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;

/*+SeqScan(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
/*+SeqScan(t1)IndexScan(t2)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
/*+BitmapScan(t2)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
/*+BitmapScan(t2)NoSeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
/*+NoIndexScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;

/*+NoBitmapScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t4 WHERE t1.val < 10;
/*+TidScan(t4)*/
EXPLAIN (COSTS false) SELECT * FROM t3, t4 WHERE t3.id = t4.id AND t4.ctid = '(1,1)';
/*+NoTidScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id AND t1.ctid = '(1,1)';

/*+HashJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
/*+NestLoop(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
/*+NoMergeJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;

/*+MergeJoin(t1 t3)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t3 WHERE t1.val = t3.val;
/*+NestLoop(t1 t3)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t3 WHERE t1.val = t3.val;
/*+NoHashJoin(t1 t3)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t3 WHERE t1.val = t3.val;

/*+MergeJoin(t4 t1 t2 t3)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2, t3, t4 WHERE t1.id = t2.id AND t1.id = t3.id AND t1.id = t4.id;
/*+HashJoin(t3 t4 t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2, t3, t4 WHERE t1.id = t2.id AND t1.id = t3.id AND t1.id = t4.id;
/*+NestLoop(t2 t3 t4 t1) IndexScan(t3)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2, t3, t4 WHERE t1.id = t2.id AND t1.id = t3.id AND t1.id = t4.id;
/*+NoNestLoop(t4 t1 t3 t2)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2, t3, t4 WHERE t1.id = t2.id AND t1.id = t3.id AND t1.id = t4.id;

/*+Leading(t3 t4)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2, t3, t4 WHERE t1.id = t2.id AND t1.id = t3.id AND t1.id = t4.id;
/*+Leading(t3 t4 t1)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2, t3, t4 WHERE t1.id = t2.id AND t1.id = t3.id AND t1.id = t4.id;
/*+Leading(t3 t4 t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2, t3, t4 WHERE t1.id = t2.id AND t1.id = t3.id AND t1.id = t4.id;
/*+Leading(t3 t4 t1 t2 t1)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2, t3, t4 WHERE t1.id = t2.id AND t1.id = t3.id AND t1.id = t4.id;
/*+Leading(t3 t4 t4)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2, t3, t4 WHERE t1.id = t2.id AND t1.id = t3.id AND t1.id = t4.id;

EXPLAIN (COSTS false) SELECT * FROM t1, (VALUES(1,1),(2,2),(3,3)) AS t2(id,val) WHERE t1.id = t2.id;
/*+HashJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM t1, (VALUES(1,1),(2,2),(3,3)) AS t2(id,val) WHERE t1.id = t2.id;
/*+HashJoin(t1 *VALUES*)*/
EXPLAIN (COSTS false) SELECT * FROM t1, (VALUES(1,1),(2,2),(3,3)) AS t2(id,val) WHERE t1.id = t2.id;
/*+HashJoin(t1 *VALUES*) IndexScan(t1) IndexScan(*VALUES*)*/
EXPLAIN (COSTS false) SELECT * FROM t1, (VALUES(1,1),(2,2),(3,3)) AS t2(id,val) WHERE t1.id = t2.id;

-- single table scan hint test
EXPLAIN SELECT (SELECT max(id) FROM t1 v_1 WHERE id < 10), id FROM v1 WHERE v1.id = (SELECT max(id) FROM t1 v_2 WHERE id < 10);
/*+BitmapScan(v_1)*/
EXPLAIN SELECT (SELECT max(id) FROM t1 v_1 WHERE id < 10), id FROM v1 WHERE v1.id = (SELECT max(id) FROM t1 v_2 WHERE id < 10);
/*+BitmapScan(v_2)*/
EXPLAIN SELECT (SELECT max(id) FROM t1 v_1 WHERE id < 10), id FROM v1 WHERE v1.id = (SELECT max(id) FROM t1 v_2 WHERE id < 10);
/*+BitmapScan(t1)*/
EXPLAIN SELECT (SELECT max(id) FROM t1 v_1 WHERE id < 10), id FROM v1 WHERE v1.id = (SELECT max(id) FROM t1 v_2 WHERE id < 10);
/*+BitmapScan(v_1)BitmapScan(v_2)*/
EXPLAIN SELECT (SELECT max(id) FROM t1 v_1 WHERE id < 10), id FROM v1 WHERE v1.id = (SELECT max(id) FROM t1 v_2 WHERE id < 10);
/*+BitmapScan(v_1)BitmapScan(t1)*/
EXPLAIN SELECT (SELECT max(id) FROM t1 v_1 WHERE id < 10), id FROM v1 WHERE v1.id = (SELECT max(id) FROM t1 v_2 WHERE id < 10);
/*+BitmapScan(v_2)BitmapScan(t1)*/
EXPLAIN SELECT (SELECT max(id) FROM t1 v_1 WHERE id < 10), id FROM v1 WHERE v1.id = (SELECT max(id) FROM t1 v_2 WHERE id < 10);
/*+BitmapScan(v_1)BitmapScan(v_2)BitmapScan(t1)*/
EXPLAIN SELECT (SELECT max(id) FROM t1 v_1 WHERE id < 10), id FROM v1 WHERE v1.id = (SELECT max(id) FROM t1 v_2 WHERE id < 10);
