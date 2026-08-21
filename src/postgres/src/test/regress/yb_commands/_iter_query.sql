-- Terminal iterator: executes :query, echoing the substituted query text.
-- Consecutive identical (>1 non-empty line) results within one \i :run_query
-- collapse to a single "(output identical to previous)" marker.  The echoed
-- query label always prints, so you still see which iterations ran.
\set _echo_iter_query :ECHO

-- Run the query.  ECHO queries writes the label to stdout via puts(); \o
-- redirects only the result, so the label is never captured and always prints.
\set ECHO queries
\set YB_DISABLE_ERROR_PREFIX on
\o :_cur_out
:query
\o
\set ECHO :_echo_iter_query

-- Collapse when all of the following hold:
-- - the result has more than one non-empty line
-- - the result is byte-identical to the previous result in this run
\set _collapse `[ "$(grep -c . "$_YB_REGRESS_CUR_OUT")" -gt 1 ] && cmp -s "$_YB_REGRESS_CUR_OUT" "$_YB_REGRESS_PREV_OUT" 2>/dev/null && echo true || echo false`
\if :_collapse
\echo '(output identical to previous)'
\else
\! cat "$_YB_REGRESS_CUR_OUT"
\endif

-- This result becomes the previous, for the next comparison.
\! cp -f "$_YB_REGRESS_CUR_OUT" "$_YB_REGRESS_PREV_OUT"
