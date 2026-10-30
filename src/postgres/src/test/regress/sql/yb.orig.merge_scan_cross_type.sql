--
-- See yb_merge_scan_schedule for details about the test.  Tests merge scan
-- when the value type does not match the column type: a cross type SAOP
-- (issue #32841) and a cross type equality (issue #33450).
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

-- Same as above but for cross type equality, on four indexes created up front.
-- Q1 is the non merge scan baseline on the first index, and Q2 to Q5 merge scan
-- on one index each:
-- - Q2, on (i2, f4, id).
-- - Q3, with the cross type equality on a hash column.
-- - Q4, with a duplicate index column.  f4 = :R becomes an index condition on
--   the first f4 only, and the second f4 holds the same value in every index
--   row.  The second f4 should not be declared a stream key, so R1 runs without
--   error while R2 still errors on the first f4.
-- - Q5, on a partial index whose predicate implies the equality.  The planner
--   drops the index condition f4 = :R, and every index row carries the constant
--   anyway, so f4 should not be declared a stream key.  Neither R errors.
-- TODO(#33450): the planner should pick a plan that does not throw an error.
SELECT $$'2.5'::real$$ AS "R1" \gset
SELECT $$'2.5'::float8$$ AS "R2" \gset
CREATE INDEX NONCONCURRENTLY num_tbl_i2_f4_id_idx ON num_tbl (i2 ASC, f4 ASC, id ASC);
CREATE INDEX NONCONCURRENTLY num_tbl_f4_hash_idx ON num_tbl (f4 HASH, i2 ASC, id ASC);
CREATE INDEX NONCONCURRENTLY num_tbl_dup_idx ON num_tbl (i2 ASC, f4 ASC, f4 ASC, id ASC);
CREATE INDEX NONCONCURRENTLY num_tbl_part_idx ON num_tbl (i2 ASC, f4 ASC, id ASC) WHERE f4 = '2.5'::real;
\set Q1 '/*+IndexScan(num_tbl num_tbl_i2_f4_id_idx) Set(yb_max_merge_scan_streams 0)*/'
\set Q2 '/*+IndexScan(num_tbl num_tbl_i2_f4_id_idx) Set(yb_max_merge_scan_streams 64)*/'
\set Q3 '/*+IndexScan(num_tbl num_tbl_f4_hash_idx) Set(yb_max_merge_scan_streams 64)*/'
\set Q4 '/*+IndexScan(num_tbl num_tbl_dup_idx) Set(yb_max_merge_scan_streams 64)*/'
\set Q5 '/*+IndexScan(num_tbl num_tbl_part_idx) Set(yb_max_merge_scan_streams 64)*/'

\set query ':P :Q SELECT f4, id, i2, x FROM num_tbl WHERE i2 IN (1, 2) AND f4 = :R ORDER BY id LIMIT 10;'
\i :run_query

-- test yb_enable_advanced_index_cond_fold flag off
SET yb_enable_advanced_index_cond_fold = off;
\i :run_query
RESET yb_enable_advanced_index_cond_fold;
\unset Q3
\unset Q4
\unset Q5
DROP INDEX num_tbl_i2_f4_id_idx;
DROP INDEX num_tbl_f4_hash_idx;
DROP INDEX num_tbl_dup_idx;
DROP INDEX num_tbl_part_idx;

-- An integer equality whose value the column's type cannot hold, which no row
-- matches.  The scan returns no rows without binding it, so the merge scan does
-- not error on the unbound stream key.  R1 fits the smallint column i2, and R2
-- does not.
SELECT $$1$$ AS "R1" \gset
SELECT $$100000$$ AS "R2" \gset
CREATE INDEX NONCONCURRENTLY num_tbl_f4_i2_id_idx ON num_tbl (f4 ASC, i2 ASC, id ASC);
\set Q1 '/*+IndexScan(num_tbl num_tbl_f4_i2_id_idx) Set(yb_max_merge_scan_streams 0)*/'
\set Q2 '/*+IndexScan(num_tbl num_tbl_f4_i2_id_idx) Set(enable_sort off) Set(yb_max_merge_scan_streams 64)*/'
\set query ':P :Q SELECT id, f4, i2 FROM num_tbl WHERE f4 = ANY(\'{1.5,2.5}\'::real[]) AND i2 = :R ORDER BY id LIMIT 10;'
\i :run_query

-- The same with the value as a parameter of a generic plan, known only when the
-- scan runs.
SET plan_cache_mode = force_generic_plan;
PREPARE oor(int) AS /*+IndexScan(num_tbl num_tbl_f4_i2_id_idx) Set(enable_sort off) Set(yb_max_merge_scan_streams 64)*/ SELECT id, f4, i2 FROM num_tbl WHERE f4 = ANY('{1.5,2.5}'::real[]) AND i2 = $1 ORDER BY id LIMIT 10;
\set query ':P EXECUTE oor(:R);'
\i :run_query
DEALLOCATE oor;
RESET plan_cache_mode;
DROP INDEX num_tbl_f4_i2_id_idx;
