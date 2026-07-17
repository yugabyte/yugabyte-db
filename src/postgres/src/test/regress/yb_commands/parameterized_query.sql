\set _echo_parameterized_query :ECHO
\set ECHO none
-- Infrastructure for running a parameterized query in regress tests.
-- Define :query with :P, :Q, :R placeholders, assign parameter values
-- (P1, P2, ...), and run "\i :run_query".  run_query infers which dimensions
-- iterate (those whose bare :P/:Q/:R appears in :query) and how many values
-- each has (the highest contiguously defined P1..P5), nesting P -> Q -> R.
-- See run_query.sql for more details.
--
-- 1D iteration over hints:
--   \set P1 '/*+Set(guc off)*/'
--   \set P2 '/*+Set(guc on)*/'
--   \set query ':P SELECT * FROM t ORDER BY k LIMIT 5;'
--   \i :run_query
--
-- 2D iteration over hints x tables (P outer, Q inner, inferred automatically):
--   \set P1 '/*+Set(guc off)*/'
--   \set P2 '/*+Set(guc on)*/'
--   \set Q1 'table_a'
--   \set Q2 'table_b'
--   \set query ':P SELECT * FROM :Q ORDER BY k LIMIT 5;'
--   \i :run_query
--
-- Explain-then-run pattern (all explains before all runs):
--   \set P1 ':explain'
--   \set P2
--   \set explain 'EXPLAIN (COSTS OFF)'
--   \set query ':P SELECT * FROM t ORDER BY k LIMIT 5;'
--   \i :run_query
--
-- To change a dimension's value count between queries, set/unset the higher
-- slots (e.g. "\set Q3 ..." to go to 3, "\unset Q3" to go back to 2).  A test
-- may override a chain edge for a dependent parameter by setting Pnext/Qnext/
-- Rnext before \i :run_query (see bitmap_hint_from_P.sql).

-- File paths for parameter iteration.
\getenv abs_srcdir PG_ABS_SRCDIR
\set _iter_query :abs_srcdir '/yb_commands/_iter_query.sql'
\set _iter_P2 :abs_srcdir '/yb_commands/_iter_P2.sql'
\set _iter_P3 :abs_srcdir '/yb_commands/_iter_P3.sql'
\set _iter_P4 :abs_srcdir '/yb_commands/_iter_P4.sql'
\set _iter_P5 :abs_srcdir '/yb_commands/_iter_P5.sql'
\set _iter_Q2 :abs_srcdir '/yb_commands/_iter_Q2.sql'
\set _iter_Q3 :abs_srcdir '/yb_commands/_iter_Q3.sql'
\set _iter_Q4 :abs_srcdir '/yb_commands/_iter_Q4.sql'
\set _iter_Q5 :abs_srcdir '/yb_commands/_iter_Q5.sql'
\set _iter_R2 :abs_srcdir '/yb_commands/_iter_R2.sql'
\set _iter_R3 :abs_srcdir '/yb_commands/_iter_R3.sql'
\set _iter_R4 :abs_srcdir '/yb_commands/_iter_R4.sql'
\set _iter_R5 :abs_srcdir '/yb_commands/_iter_R5.sql'
\set run_query :abs_srcdir '/yb_commands/run_query.sql'

-- Scratch file for run_query's dimension detection.  The fixed filename
-- assumes tests using this infrastructure run on serial schedule lines:
-- parallel tests within one pg_regress run share the outputdir and would race
-- on it.  (Concurrent Java tests are fine: each run has its own abs_builddir.)
\getenv abs_builddir PG_ABS_BUILDDIR
\set _cur_query :abs_builddir '/results/yb_regress_cur_query'
\setenv _YB_REGRESS_CUR_QUERY :_cur_query

\set ECHO :_echo_parameterized_query
