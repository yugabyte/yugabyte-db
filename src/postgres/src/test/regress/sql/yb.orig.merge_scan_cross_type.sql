--
-- See yb_merge_scan_schedule for details about the test.  Tests merge scan
-- when the value type does not match the column type: a cross type SAOP
-- (issue #32841).
--
\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/merge_scan_setup.sql'
\i :filename

-- R1's array matches the column type.  R2's float8 array does not match the
-- float4 column, so the bind drops: merge scan errors, and non merge scan
-- returns correct rows through recheck (the values match rows since they are
-- exactly representable in both types).
-- TODO(#32841): the planner should pick a plan that does not throw an error.
SELECT $$'{1.5,2.5}'::real[]$$ AS "R1" \gset
SELECT $$'{1.5,2.5}'::float8[]$$ AS "R2" \gset

\set query ':P :Q SELECT id, f4, x FROM num_tbl WHERE f4 = ANY(:R) ORDER BY id LIMIT 10;'
\i :run_query

-- test yb_enable_advanced_index_cond_fold flag off
SET yb_enable_advanced_index_cond_fold = off;
\i :run_query
RESET yb_enable_advanced_index_cond_fold;
