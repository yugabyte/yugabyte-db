\set _echo_iter_Q5 :ECHO
\set ECHO none
-- Iterate Q over Q1..Q5. Set :Qnext to the next iterator or :_iter_query.
\set Q :Q1
\i :Qnext
\set Q :Q2
\i :Qnext
\set Q :Q3
\i :Qnext
\set Q :Q4
\i :Qnext
\set Q :Q5
\i :Qnext
\set ECHO :_echo_iter_Q5
