\getenv abs_srcdir PG_ABS_SRCDIR
\set pq_param :abs_srcdir '/yb_commands/parameterized_query.sql'
\i :pq_param
\set P1 'EXPLAIN (COSTS OFF)'
\set P2
-- The ORDER BY on the primary key with a parallel scan should produce a Gather Merge plan.
\set query ':P SELECT count(*) FROM (SELECT k FROM pss_test WHERE pss_snapshot_fn(k) = k ORDER BY k) t;'
BEGIN;
\i :iter_P2
COMMIT;
-- Also run outside a transaction block.
\i :iter_P2
