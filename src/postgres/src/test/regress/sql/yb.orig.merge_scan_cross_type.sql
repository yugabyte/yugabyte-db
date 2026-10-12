--
-- See yb_merge_scan_schedule for details about the test.  Tests merge scan
-- when the value type does not match the column type: a cross type SAOP
-- (issue #32841) and a cross type equality (issue #33450).
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

-- Same as above but for cross type equality, on four indexes created up front.
-- hint1 is the non merge scan baseline on the first index, and hint2 to hint5
-- merge scan on one index each:
-- - hint2, on (i2, f4, id).
-- - hint3, with the cross type equality on a hash column.
-- - hint4, with a duplicate index column.  The f4 equality becomes an index
--   condition on the first f4 only, and the second f4 holds the same value in
--   every index row.  The second f4 should not be declared a stream key, so the
--   real value runs without error while the float8 value still errors on the
--   first f4.
-- - hint5, on a partial index whose predicate implies the equality.  The
--   planner drops the f4 equality index condition, and every index row carries
--   the constant anyway, so f4 should not be declared a stream key.  Neither
--   value errors.
-- TODO(#33450): the planner should pick a plan that does not throw an error.
CREATE INDEX NONCONCURRENTLY num_tbl_i2_f4_id_idx ON num_tbl (i2 ASC, f4 ASC, id ASC);
CREATE INDEX NONCONCURRENTLY num_tbl_f4_hash_idx ON num_tbl (f4 HASH, i2 ASC, id ASC);
CREATE INDEX NONCONCURRENTLY num_tbl_dup_idx ON num_tbl (i2 ASC, f4 ASC, f4 ASC, id ASC);
CREATE INDEX NONCONCURRENTLY num_tbl_part_idx ON num_tbl (i2 ASC, f4 ASC, id ASC) WHERE f4 = '2.5'::real;
\set hint1 '/*+IndexScan(num_tbl num_tbl_i2_f4_id_idx) Set(yb_max_merge_scan_streams 0)*/'
\set hint2 '/*+IndexScan(num_tbl num_tbl_i2_f4_id_idx) Set(yb_max_merge_scan_streams 64)*/'
\set hint3 '/*+IndexScan(num_tbl num_tbl_f4_hash_idx) Set(yb_max_merge_scan_streams 64)*/'
\set hint4 '/*+IndexScan(num_tbl num_tbl_dup_idx) Set(yb_max_merge_scan_streams 64)*/'
\set hint5 '/*+IndexScan(num_tbl num_tbl_part_idx) Set(yb_max_merge_scan_streams 64)*/'
\set query 'SELECT f4, id, i2, x FROM num_tbl WHERE i2 IN (1, 2) AND f4 = ''2.5''::real ORDER BY id LIMIT 10'
:explain5run5
\set query 'SELECT f4, id, i2, x FROM num_tbl WHERE i2 IN (1, 2) AND f4 = ''2.5''::float8 ORDER BY id LIMIT 10'
:explain5run5
DROP INDEX num_tbl_i2_f4_id_idx;
DROP INDEX num_tbl_f4_hash_idx;
DROP INDEX num_tbl_dup_idx;
DROP INDEX num_tbl_part_idx;

-- An integer equality whose value the column's type cannot hold, which no row
-- matches.  The scan returns no rows without binding it, so the merge scan does
-- not error on the unbound stream key.  1 fits the smallint column i2, and
-- 100000 does not.
CREATE INDEX NONCONCURRENTLY num_tbl_f4_i2_id_idx ON num_tbl (f4 ASC, i2 ASC, id ASC);
\set hint1 '/*+IndexScan(num_tbl num_tbl_f4_i2_id_idx) Set(yb_max_merge_scan_streams 0)*/'
\set hint2 '/*+IndexScan(num_tbl num_tbl_f4_i2_id_idx) Set(enable_sort off) Set(yb_max_merge_scan_streams 64)*/'
\set query 'SELECT id, f4, i2 FROM num_tbl WHERE f4 = ANY(''{1.5,2.5}''::real[]) AND i2 = 1 ORDER BY id LIMIT 10'
:explain2run2
\set query 'SELECT id, f4, i2 FROM num_tbl WHERE f4 = ANY(''{1.5,2.5}''::real[]) AND i2 = 100000 ORDER BY id LIMIT 10'
:explain2run2

-- The same with the value as a parameter of a generic plan, known only when the
-- scan runs.
SET plan_cache_mode = force_generic_plan;
PREPARE oor(int) AS /*+IndexScan(num_tbl num_tbl_f4_i2_id_idx) Set(enable_sort off) Set(yb_max_merge_scan_streams 64)*/ SELECT id, f4, i2 FROM num_tbl WHERE f4 = ANY('{1.5,2.5}'::real[]) AND i2 = $1 ORDER BY id LIMIT 10;
\set hint1 ''
\set query 'EXECUTE oor(1)'
:explain1run1
\set query 'EXECUTE oor(100000)'
:explain1run1
DEALLOCATE oor;
RESET plan_cache_mode;
DROP INDEX num_tbl_f4_i2_id_idx;
