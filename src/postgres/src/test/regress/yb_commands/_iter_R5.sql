\set _echo_iter_R5 :ECHO
\set ECHO none
-- Iterate R over R1..R5. Set :Rnext to the next iterator or :_iter_query.
\set R :R1
\i :Rnext
\set R :R2
\i :Rnext
\set R :R3
\i :Rnext
\set R :R4
\i :Rnext
\set R :R5
\i :Rnext
\set ECHO :_echo_iter_R5
