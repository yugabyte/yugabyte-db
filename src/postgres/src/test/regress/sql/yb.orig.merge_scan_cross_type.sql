--
-- See yb_merge_scan_schedule for details about the test.  Tests merge scan
-- when the value type does not match the column type: a cross type SAOP
-- (issue #32841).
--
\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/data/explainrun_merge_scan.sql'
\i :filename

-- The real array matches the column type.  The float8 array does not match
-- the float4 column, so the bind drops: merge scan errors, and non merge scan
-- returns correct rows through recheck (the values match rows since they are
-- exactly representable in both types).
-- TODO(#32841): the planner should pick a plan that does not throw an error.
\set query 'SELECT id, f4, x FROM num_tbl WHERE f4 = ANY(''{1.5,2.5}''::real[]) ORDER BY id LIMIT 10'
:explain2run2
\set query 'SELECT id, f4, x FROM num_tbl WHERE f4 = ANY(''{1.5,2.5}''::float8[]) ORDER BY id LIMIT 10'
:explain2run2
