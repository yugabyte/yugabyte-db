\set _echo_run_query :ECHO
\set ECHO none
-- Auto-inferring entry point: run :query, inferring the iteration structure
-- from the query and the defined P/Q/R params.
--
-- A dimension iterates iff its bare placeholder (:P/:Q/:R, not :P1 etc.)
-- appears in :query; its value count is the highest contiguously defined
-- P1..P5 (a param set to the empty string via "\set P2" still counts).
-- Dimensions nest P -> Q -> R (P outermost), skipping absent ones.  A test may
-- override any edge by pre-setting Pnext/Qnext/Rnext before "\i :run_query"
-- (used by the dependent-parameter helpers).  run_query fills only the edges
-- left unset and \unsets all three afterwards, so each run re-infers from
-- scratch and any override is strictly run-scoped.

-- Clear collapse state left by any earlier run, even one that died mid-run:
-- collapse never crosses runs.
\! rm -f "$_YB_REGRESS_PREV_OUT"

-- Capture the query template to a file and detect dimensions.  ":P"/":Q"/":R"
-- count only when the next char is not an identifier char, so ":Q1" (a fixed
-- literal) is excluded while ":Q" (the iterated dim) matches.  "[:]P" prevents
-- psql interpolation while the regex still matches ":P".
\o :_cur_query
\qecho :query
\o
\set _use_p `grep -qE '[:]P([^0-9A-Za-z_]|$)' "$_YB_REGRESS_CUR_QUERY" && echo true || echo false`
\set _use_q `grep -qE '[:]Q([^0-9A-Za-z_]|$)' "$_YB_REGRESS_CUR_QUERY" && echo true || echo false`
\set _use_r `grep -qE '[:]R([^0-9A-Za-z_]|$)' "$_YB_REGRESS_CUR_QUERY" && echo true || echo false`

-- Count of contiguously defined values per dimension, via nested :{?PN} tests.
\set _count_p 0
\if :{?P1}
\set _count_p 1
\if :{?P2}
\set _count_p 2
\if :{?P3}
\set _count_p 3
\if :{?P4}
\set _count_p 4
\if :{?P5}
\set _count_p 5
\endif
\endif
\endif
\endif
\endif
\set _count_q 0
\if :{?Q1}
\set _count_q 1
\if :{?Q2}
\set _count_q 2
\if :{?Q3}
\set _count_q 3
\if :{?Q4}
\set _count_q 4
\if :{?Q5}
\set _count_q 5
\endif
\endif
\endif
\endif
\endif
\set _count_r 0
\if :{?R1}
\set _count_r 1
\if :{?R2}
\set _count_r 2
\if :{?R3}
\set _count_r 3
\if :{?R4}
\set _count_r 4
\if :{?R5}
\set _count_r 5
\endif
\endif
\endif
\endif
\endif

-- Guardrails:
-- - in a used dimension, a value defined above a gap (e.g. P4 set while P3 is
--   unset) would silently never run
-- - a used dimension with fewer than two values has nothing to iterate
-- - a :query with no placeholders at all likely forgot them (run a one-off
--   with plain ":query" instead)
-- Report any of these and skip the run, like a query error.  Unused dimensions
-- are not checked (their values are irrelevant to this query).
\set _params_ok true
\if :_use_p
\set _max_p 0
\if :{?P5}
\set _max_p 5
\elif :{?P4}
\set _max_p 4
\elif :{?P3}
\set _max_p 3
\elif :{?P2}
\set _max_p 2
\elif :{?P1}
\set _max_p 1
\endif
\set _gap_p `[ :_max_p -gt :_count_p ] && echo true || echo false`
\set _few_p `[ :_count_p -le 1 ] && echo true || echo false`
\if :_gap_p
\set _gap_p `expr :_count_p + 1`
\set _error_msg 'run_query: error: P' :_max_p ' is set but P' :_gap_p ' is not'
\echo :_error_msg
\set _params_ok false
\elif :_few_p
\set _error_msg 'run_query: error: :P is in the query but fewer than two P values are set'
\echo :_error_msg
\set _params_ok false
\endif
\endif
\if :_use_q
\set _max_q 0
\if :{?Q5}
\set _max_q 5
\elif :{?Q4}
\set _max_q 4
\elif :{?Q3}
\set _max_q 3
\elif :{?Q2}
\set _max_q 2
\elif :{?Q1}
\set _max_q 1
\endif
\set _gap_q `[ :_max_q -gt :_count_q ] && echo true || echo false`
\set _few_q `[ :_count_q -le 1 ] && echo true || echo false`
\if :_gap_q
\set _gap_q `expr :_count_q + 1`
\set _error_msg 'run_query: error: Q' :_max_q ' is set but Q' :_gap_q ' is not'
\echo :_error_msg
\set _params_ok false
\elif :_few_q
\set _error_msg 'run_query: error: :Q is in the query but fewer than two Q values are set'
\echo :_error_msg
\set _params_ok false
\endif
\endif
\if :_use_r
\set _max_r 0
\if :{?R5}
\set _max_r 5
\elif :{?R4}
\set _max_r 4
\elif :{?R3}
\set _max_r 3
\elif :{?R2}
\set _max_r 2
\elif :{?R1}
\set _max_r 1
\endif
\set _gap_r `[ :_max_r -gt :_count_r ] && echo true || echo false`
\set _few_r `[ :_count_r -le 1 ] && echo true || echo false`
\if :_gap_r
\set _gap_r `expr :_count_r + 1`
\set _error_msg 'run_query: error: R' :_max_r ' is set but R' :_gap_r ' is not'
\echo :_error_msg
\set _params_ok false
\elif :_few_r
\set _error_msg 'run_query: error: :R is in the query but fewer than two R values are set'
\echo :_error_msg
\set _params_ok false
\endif
\endif
\set _nodims `[ ':_use_p/:_use_q/:_use_r' = 'false/false/false' ] && echo true || echo false`
\if :_nodims
\set _error_msg 'run_query: error: :query has no :P/:Q/:R placeholders'
\echo :_error_msg
\set _params_ok false
\endif

-- Fill chain edges (inner to outer), keeping any user override already set.
\if :{?Rnext}
\else
\set Rnext :_iter_query
\endif
\if :{?Qnext}
\else
\if :_use_r
\set Qnext :abs_srcdir '/yb_commands/_iter_R' :_count_r '.sql'
\else
\set Qnext :_iter_query
\endif
\endif
\if :{?Pnext}
\else
\if :_use_q
\set Pnext :abs_srcdir '/yb_commands/_iter_Q' :_count_q '.sql'
\elif :_use_r
\set Pnext :abs_srcdir '/yb_commands/_iter_R' :_count_r '.sql'
\else
\set Pnext :_iter_query
\endif
\endif

-- Enter at the outermost used dimension.
\if :_use_p
\set _entry_iter :abs_srcdir '/yb_commands/_iter_P' :_count_p '.sql'
\elif :_use_q
\set _entry_iter :abs_srcdir '/yb_commands/_iter_Q' :_count_q '.sql'
\elif :_use_r
\set _entry_iter :abs_srcdir '/yb_commands/_iter_R' :_count_r '.sql'
\endif
\if :_params_ok
\i :_entry_iter
\endif

-- Reset chain edges so the next run re-infers (overrides are run-scoped).
\unset Pnext
\unset Qnext
\unset Rnext

\set ECHO :_echo_run_query
