\set _echo_Q :ECHO
\set ECHO none
-- Iterate Q over Q1..Q4. Set :Qnext to the next iterator or :iter_query.
\set Q :Q1
\i :Qnext
\set Q :Q2
\i :Qnext
\set Q :Q3
\i :Qnext
\set Q :Q4
\i :Qnext
\set ECHO :_echo_Q
