\set _echo_iter_P2 :ECHO
\set ECHO none
-- Iterate P over P1, P2. Set :Pnext to the next iterator or :_iter_query.
\set P :P1
\i :Pnext
\set P :P2
\i :Pnext
\set ECHO :_echo_iter_P2
