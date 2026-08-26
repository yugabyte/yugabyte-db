\set _echo_iter_Q3 :ECHO
\set ECHO none
-- Iterate Q over Q1..Q3. Set :Qnext to the next iterator or :_iter_query.
\set Q :Q1
\i :Qnext
\set Q :Q2
\i :Qnext
\set Q :Q3
\i :Qnext
\set ECHO :_echo_iter_Q3
