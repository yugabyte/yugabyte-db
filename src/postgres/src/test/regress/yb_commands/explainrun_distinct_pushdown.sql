\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/explainrun.sql'
\i :filename

\set explain 'EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)'
