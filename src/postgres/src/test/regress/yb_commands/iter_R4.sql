\set _echo_R :ECHO
\set ECHO none
-- Iterate R over R1..R4. Set :Rnext to the next iterator or :iter_query.
\set R :R1
\i :Rnext
\set R :R2
\i :Rnext
\set R :R3
\i :Rnext
\set R :R4
\i :Rnext
\set ECHO :_echo_R
