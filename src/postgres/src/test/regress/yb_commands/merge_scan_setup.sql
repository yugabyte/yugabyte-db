\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/parameterized_query.sql'
\i :filename

\set explain 'EXPLAIN (ANALYZE, DIST, VERBOSE, COSTS OFF, SUMMARY OFF, TIMING OFF)'
\set off '/*+Set(yb_max_merge_scan_streams 0)*/'
\set on '/*+Set(yb_max_merge_scan_streams 64)*/'
\set P1 ':explain'
\set P2
\set Q1 ':off'
\set Q2 ':on'
\set Pnext :iter_Q2
