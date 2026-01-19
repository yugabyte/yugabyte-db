-- Queries for the null scan key tests

\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/explainrun.sql'
\i :filename
\set explain 'EXPLAIN (ANALYZE, COSTS OFF, DIST ON, TIMING OFF, SUMMARY OFF)'

SET client_min_messages = DEBUG1;
\set YB_DISABLE_ERROR_PREFIX on
SET yb_bnl_batch_size to 1;

-- Should return empty results (actual rows=0)
-- The plans should not show any "Recheck"
\set hint1 '/*+ IndexScan(t1) NestLoop(t2 t1) Leading((t2 t1)) */'
\set query 'SELECT * FROM nulltest t1 JOIN nulltest2 t2 ON t1.a = t2.x'
:explain1

\set query 'SELECT * FROM nulltest t1 JOIN nulltest2 t2 ON t1.a <= t2.x'
:explain1

\set query 'SELECT * FROM nulltest t1 JOIN nulltest2 t2 ON t1.a BETWEEN t2.x AND t2.x + 2'
:explain1

\set query 'SELECT * FROM nulltest t1 JOIN nulltest2 t2 ON (t1.a, t1.b) = (t2.x, t2.y)'
:explain1

\set query 'SELECT * FROM nulltest t1 JOIN nulltest2 t2 ON (t1.a, t1.b) <= (t2.x, t2.y)'
:explain1

\set hint1 '/*+ IndexScan(t1) */'
\set hint2 '/*+ IndexScan(t1 i_nulltest_ba) */'
\set query 'SELECT * FROM nulltest t1 WHERE (a, b) <= (null, 1)'
:explain2

\set query 'SELECT a FROM nulltest t1 WHERE a IN (null, null)'
:explain2


-- Should return 1s
\set hint1 '/*+ IndexScan(t1) */'
\set hint2 '/*+ IndexScan(t1 i_nulltest_ba) */'
\set query 'SELECT a FROM nulltest t1 WHERE a IN (null, 1)'
:explain2run2

/*+ IndexScan(t1) */
\set query 'SELECT a FROM nulltest t1 WHERE (a, b) <= (2, null)'
:explain2run2


-- Should return nulls
\set hint1 '/*+ IndexScan(t1) */'
\set query 'SELECT a FROM nulltest t1 WHERE a IS NULL'
:explain1run1

RESET client_min_messages;
\unset YB_DISABLE_ERROR_PREFIX
