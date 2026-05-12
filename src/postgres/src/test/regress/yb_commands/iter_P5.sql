\set _echo_P :ECHO
\set ECHO none
-- Iterate P over P1..P5. Set :Pnext to the next iterator or :iter_query.
\set P :P1
\i :Pnext
\set P :P2
\i :Pnext
\set P :P3
\i :Pnext
\set P :P4
\i :Pnext
\set P :P5
\i :Pnext
\set ECHO :_echo_P
