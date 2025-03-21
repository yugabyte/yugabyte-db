--
-- Test the LOCK statement
--

-- directory paths and dlsuffix are passed to us in environment variables
\getenv libdir PG_LIBDIR
\getenv dlsuffix PG_DLSUFFIX

\set regresslib :libdir '/regress' :dlsuffix

CREATE FUNCTION test_atomic_ops()
    RETURNS bool
    AS :'regresslib'
    LANGUAGE C;
