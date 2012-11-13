LOAD 'pg_hint_plan';
SET pg_hint_plan.debug_print TO on;
SET client_min_messages TO LOG;

--
-- IndexOnlyScan hint test
--
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 t_1 WHERE c2 < 1000;

/*+IndexOnlyScan(t_1)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 t_1 WHERE c2 < 1000;

/*+IndexOnlyScan(t_1 ti1_i1)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 t_1 WHERE c2 < 1000;

/*+IndexOnlyScan(t_1 ti1_i2 ti1_i1)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 t_1 WHERE c2 < 1000;

/*+IndexOnlyScan(t_1 ti1_pred)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 t_1 WHERE c2 < 1000;

/*+IndexOnlyScan(t_1 ti1_i)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 t_1 WHERE c2 < 1000;

/*+IndexScan(t_1)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 t_1 WHERE c2 < 1000;

--
-- NoIndexOnlyScan hint test
--
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 t_1 WHERE c2 < 10;

/*+NoIndexOnlyScan(t_1)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 t_1 WHERE c2 < 10;

/*+NoIndexOnlyScan(t_1 ti1_i1)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 t_1 WHERE c2 < 10;

/*+NoIndexScan(t_1)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 t_1 WHERE c2 < 10;

