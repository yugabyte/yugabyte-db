-- Test null hash scan key

-- the test tables are created in yb_index_serial_schedule
-- \i sql/yb.orig.index_scan_null_create.sql

SET client_min_messages=error;

DROP INDEX IF EXISTS i_nulltest_a;
CREATE INDEX i_nulltest_a ON nulltest (a HASH);

DROP INDEX IF EXISTS i_nulltest_ba;
CREATE INDEX i_nulltest_ba ON nulltest ((b, a) HASH);

\i sql/yb.orig.index_scan_null_queries.sql
