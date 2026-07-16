-- Terminal iterator: executes :query, echoing the substituted query text.
\set _echo_iter_query :ECHO
\set ECHO queries
\set YB_DISABLE_ERROR_PREFIX on
:query
\set ECHO :_echo_iter_query
