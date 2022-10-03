-- Queries for the null scan key tests

-- Should return empty results (actual rows=0)
-- The plans should not show any "Recheck"
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
/*+ IndexScan(t1) NestLoop(t2 t1) Leading((t2 t1)) */
SELECT * FROM nulltest t1 JOIN nulltest2 t2 ON t1.a = t2.x;

EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
/*+ IndexScan(t1) NestLoop(t2 t1) Leading((t2 t1)) */
SELECT * FROM nulltest t1 JOIN nulltest2 t2 ON t1.a <= t2.x;

EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
/*+ IndexScan(t1) NestLoop(t2 t1) Leading((t2 t1)) */
SELECT * FROM nulltest t1 JOIN nulltest2 t2 ON t1.a BETWEEN t2.x AND t2.x + 2;

EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
/*+ IndexScan(t1) NestLoop(t2 t1) Leading((t2 t1)) */
SELECT * FROM nulltest t1 JOIN nulltest2 t2 ON (t1.a, t1.b) = (t2.x, t2.y);

EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
/*+ IndexScan(t1) NestLoop(t2 t1) Leading((t2 t1)) */
SELECT * FROM nulltest t1 JOIN nulltest2 t2 ON (t1.a, t1.b) <= (t2.x, t2.y);

EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
/*+ IndexScan(t1) */
SELECT * FROM nulltest t1 WHERE (a, b) <= (null, 1);

EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
/*+ IndexScan(t1) */
SELECT a FROM nulltest t1 WHERE a IN (null, null);


-- Should return 1s
/*+ IndexScan(t1) */
SELECT a FROM nulltest t1 WHERE a IN (null, 1);

EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
/*+ IndexScan(t1) */
SELECT a FROM nulltest t1 WHERE a IN (null, 1);

/*+ IndexScan(t1) */
SELECT a FROM nulltest t1 WHERE (a, b) <= (2, null);

EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
/*+ IndexScan(t1) */
SELECT a FROM nulltest t1 WHERE (a, b) <= (2, null);


-- Should return nulls
/*+ IndexScan(t1) */
SELECT a FROM nulltest t1 WHERE a IS NULL;

EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
/*+ IndexScan(t1) */
SELECT a FROM nulltest t1 WHERE a IS NULL;
