-- Test null non-hash scan key

-- the test tables are created in yb.orig.index_scan_null_create

SET client_min_messages=error;

DROP INDEX IF EXISTS i_nulltest_a;
CREATE INDEX i_nulltest_a ON nulltest (a ASC);

DROP INDEX IF EXISTS i_nulltest_ba;
CREATE INDEX i_nulltest_ba ON nulltest (b ASC, a ASC);

\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/index_scan_null_queries.sql'
\i :filename
