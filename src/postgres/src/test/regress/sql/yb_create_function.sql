--
-- Yugabyte created functions
--

-- directory path and dlsuffix are passed to us in environment variables
\getenv libdir PG_LIBDIR
\getenv dlsuffix PG_DLSUFFIX

\set regresslib :libdir '/regress' :dlsuffix

CREATE FUNCTION bigname_in(cstring)
   RETURNS bigname
   AS :'regresslib'
   LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION bigname_out(bigname)
   RETURNS cstring
   AS :'regresslib'
   LANGUAGE C STRICT IMMUTABLE;
