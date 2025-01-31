--
-- CREATE_VIEW
-- Virtual class definitions
--	(this also tests the query rewrite system)
--

-- directory paths and dlsuffix are passed to us in environment variables
\getenv abs_srcdir PG_ABS_SRCDIR
\getenv libdir PG_LIBDIR
\getenv dlsuffix PG_DLSUFFIX

\set regresslib :libdir '/regress' :dlsuffix

CREATE FUNCTION interpt_pp(path, path)
    RETURNS point
    AS :'regresslib'
    LANGUAGE C STRICT;

CREATE TABLE real_city (
	pop			int4,
	cname		text,
	outline 	path
);
