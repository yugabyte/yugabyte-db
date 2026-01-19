\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/explainrun.sql'
\i :filename

\set explain 'EXPLAIN (ANALYZE, DIST, VERBOSE, COSTS OFF, SUMMARY OFF, TIMING OFF)'
\set off '/*+Set(yb_max_saop_merge_streams 0)*/'
\set on '/*+Set(yb_max_saop_merge_streams 64)*/'
\set hint1 ':off'
\set hint2 ':on'
