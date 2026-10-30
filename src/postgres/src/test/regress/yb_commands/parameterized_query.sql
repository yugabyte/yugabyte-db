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

-- File paths for parameter iteration.  They default to the yb_commands dir
-- under PG_ABS_SRCDIR; a test outside src/test/regress (e.g. a contrib test)
-- sets yb_commands_dir to this directory before including this file.
\getenv abs_srcdir PG_ABS_SRCDIR
\if :{?yb_commands_dir}
\else
\set yb_commands_dir :abs_srcdir '/yb_commands'
\endif
\set _iter_query :yb_commands_dir '/_iter_query.sql'
\set _iter_P2 :yb_commands_dir '/_iter_P2.sql'
\set _iter_P3 :yb_commands_dir '/_iter_P3.sql'
\set _iter_P4 :yb_commands_dir '/_iter_P4.sql'
\set _iter_P5 :yb_commands_dir '/_iter_P5.sql'
\set _iter_Q2 :yb_commands_dir '/_iter_Q2.sql'
\set _iter_Q3 :yb_commands_dir '/_iter_Q3.sql'
\set _iter_Q4 :yb_commands_dir '/_iter_Q4.sql'
\set _iter_Q5 :yb_commands_dir '/_iter_Q5.sql'
\set _iter_R2 :yb_commands_dir '/_iter_R2.sql'
\set _iter_R3 :yb_commands_dir '/_iter_R3.sql'
\set _iter_R4 :yb_commands_dir '/_iter_R4.sql'
\set _iter_R5 :yb_commands_dir '/_iter_R5.sql'
\set run_query :yb_commands_dir '/run_query.sql'

-- Scratch files, written under PG_ABS_BUILDDIR, which should be a private tmp
-- dir for each Java test invocation.  The fixed filenames assume tests using
-- this infrastructure run on serial schedule lines: parallel tests within one
-- pg_regress run would share them and race.  The psql vars are for \o (which
-- interpolates psql vars); the _YB_REGRESS_* env vars are for \! (which does
-- not interpolate psql vars).
\getenv abs_builddir PG_ABS_BUILDDIR
\set _cur_query :abs_builddir '/results/yb_regress_cur_query'
\set _cur_out :abs_builddir '/results/yb_regress_cur_out'
\set _prev_out :abs_builddir '/results/yb_regress_prev_out'
\setenv _YB_REGRESS_CUR_QUERY :_cur_query
\setenv _YB_REGRESS_CUR_OUT :_cur_out
\setenv _YB_REGRESS_PREV_OUT :_prev_out

\set ECHO :_echo_parameterized_query
