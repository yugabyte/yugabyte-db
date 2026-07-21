\set _echo_iter_P4 :ECHO
\set ECHO none
-- Iterate P over P1..P4. Set :Pnext to the next iterator or :_iter_query.
\set P :P1
\i :Pnext
\set P :P2
\i :Pnext
\set P :P3
\i :Pnext
\set P :P4
\i :Pnext
\set ECHO :_echo_iter_P4
