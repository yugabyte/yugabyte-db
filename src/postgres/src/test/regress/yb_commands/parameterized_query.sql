\set _echo_pq :ECHO
\set ECHO none
-- Infrastructure for running a parameterized query in regress tests.
-- Define :query with :P, :Q, :R placeholders, assign parameter values
-- (P1, P2, ...), and use iter files to iterate over them.  Each dimension
-- chains to the next via Pnext, Qnext, Rnext (all default to :iter_query).
--
-- 1D iteration over hints:
--   \set P1 '/*+Set(guc off)*/'
--   \set P2 '/*+Set(guc on)*/'
--   \set query ':P SELECT * FROM t ORDER BY k LIMIT 5;'
--   \i :iter_P2
--
-- 2D iteration over hints x tables:
--   \set P1 '/*+Set(guc off)*/'
--   \set P2 '/*+Set(guc on)*/'
--   \set Pnext :iter_Q2
--   \set Q1 'table_a'
--   \set Q2 'table_b'
--   \set query ':P SELECT * FROM :Q ORDER BY k LIMIT 5;'
--   \i :iter_P2
--
-- Explain-then-run pattern (all explains before all runs):
--   \set P1 ':explain'
--   \set P2
--   \set explain 'EXPLAIN (COSTS OFF)'
--   \set query ':P SELECT * FROM t ORDER BY k LIMIT 5;'
--   \i :iter_P2

-- File paths for parameter iteration.
\getenv abs_srcdir PG_ABS_SRCDIR
\set iter_query :abs_srcdir '/yb_commands/iter_query.sql'
\set iter_P2 :abs_srcdir '/yb_commands/iter_P2.sql'
\set iter_P3 :abs_srcdir '/yb_commands/iter_P3.sql'
\set iter_P4 :abs_srcdir '/yb_commands/iter_P4.sql'
\set iter_P5 :abs_srcdir '/yb_commands/iter_P5.sql'
\set iter_Q2 :abs_srcdir '/yb_commands/iter_Q2.sql'
\set iter_Q3 :abs_srcdir '/yb_commands/iter_Q3.sql'
\set iter_Q4 :abs_srcdir '/yb_commands/iter_Q4.sql'
\set iter_Q5 :abs_srcdir '/yb_commands/iter_Q5.sql'
\set iter_R2 :abs_srcdir '/yb_commands/iter_R2.sql'
\set iter_R3 :abs_srcdir '/yb_commands/iter_R3.sql'
\set iter_R4 :abs_srcdir '/yb_commands/iter_R4.sql'
\set iter_R5 :abs_srcdir '/yb_commands/iter_R5.sql'

-- Default terminal iterators: execute the query after the last value.
\set Pnext :iter_query
\set Qnext :iter_query
\set Rnext :iter_query

\set ECHO :_echo_pq
