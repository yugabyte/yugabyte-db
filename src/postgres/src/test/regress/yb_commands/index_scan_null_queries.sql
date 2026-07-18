-- Queries for the null scan key tests

\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/parameterized_query.sql'
\i :filename
\set P1 ':explain'
\set P2
\set explain 'EXPLAIN (ANALYZE, COSTS OFF, DIST ON, TIMING OFF, SUMMARY OFF)'

SET client_min_messages = DEBUG1;
\set YB_DISABLE_ERROR_PREFIX on
SET yb_bnl_batch_size to 1;

-- Should return empty results (actual rows=0)
-- The plans should not show any "Recheck"
\set Q1 '/*+ IndexScan(t1) NestLoop(t2 t1) Leading((t2 t1)) */'
\set query ':explain :Q1 SELECT * FROM nulltest t1 JOIN nulltest2 t2 ON t1.a = t2.x;'
:query

\set query ':explain :Q1 SELECT * FROM nulltest t1 JOIN nulltest2 t2 ON t1.a <= t2.x;'
:query

\set query ':explain :Q1 SELECT * FROM nulltest t1 JOIN nulltest2 t2 ON t1.a BETWEEN t2.x AND t2.x + 2;'
:query

\set query ':explain :Q1 SELECT * FROM nulltest t1 JOIN nulltest2 t2 ON (t1.a, t1.b) = (t2.x, t2.y);'
:query

\set query ':explain :Q1 SELECT * FROM nulltest t1 JOIN nulltest2 t2 ON (t1.a, t1.b) <= (t2.x, t2.y);'
:query

\set Q1 '/*+ IndexScan(t1) */'
\set Q2 '/*+ IndexScan(t1 i_nulltest_ba) */'
\set query ':explain :Q SELECT * FROM nulltest t1 WHERE (a, b) <= (null, 1);'
\i :iter_Q2

\set query ':explain :Q SELECT a FROM nulltest t1 WHERE a IN (null, null);'
\i :iter_Q2


-- Should return 1s
\set Q1 '/*+ IndexScan(t1) */'
\set Q2 '/*+ IndexScan(t1 i_nulltest_ba) */'
\set query ':P :Q SELECT a FROM nulltest t1 WHERE a IN (null, 1);'
\set Pnext :iter_Q2
\i :iter_P2

/*+ IndexScan(t1) */
\set query ':P :Q SELECT a FROM nulltest t1 WHERE (a, b) <= (2, null);'
\i :iter_P2


-- Should return nulls
\set Q1 '/*+ IndexScan(t1) */'
\set query ':P :Q1 SELECT a FROM nulltest t1 WHERE a IS NULL;'
\set Pnext :iter_query
\i :iter_P2

RESET client_min_messages;
\unset YB_DISABLE_ERROR_PREFIX
