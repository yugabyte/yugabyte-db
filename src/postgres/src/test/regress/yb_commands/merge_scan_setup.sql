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

-- Small response pages make the execution metrics of the plan variants
-- distinct without growing the tables: at the default page size their rows
-- come back in one read request either way.
SET yb_fetch_row_limit = 16;
