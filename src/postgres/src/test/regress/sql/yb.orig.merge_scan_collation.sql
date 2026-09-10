--
-- See yb_merge_scan_schedule for details about the test.  Tests merge scan
-- with collation mismatches between the clause and the index column.
--
\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/merge_scan_setup.sql'
\i :filename

-- The explicit "C" collation on the index column differs from the default
-- collation of the underlying t column, so a query clause on t matches the
-- index expression while its comparison collation may not match the index
-- collation.  All collations involved compare the single-character digit
-- values identically, so correct query results are the same in every case.
CREATE INDEX NONCONCURRENTLY idx ON tv_tbl (t COLLATE "C" ASC, i2, i4)
SPLIT AT VALUES (('0'), ('1'), ('2'), ('3'));

-- Pin the index with a hint so that ineligible cases show their fallback
-- plan instead of a cost-based choice.
\set Q1 '/*+IndexScan(tv_tbl idx) Set(yb_max_merge_scan_streams 0)*/'
\set Q2 '/*+IndexScan(tv_tbl idx) Set(yb_max_merge_scan_streams 64)*/'

-- Where the clause collation comes from for each parameter:
-- - R1: an explicit override matching the index collation.  The clause forms
--   an index condition and merge streams.
-- - R2: the bare column carries its default collation, which does not match
--   the index collation.  No index condition is possible, and the hint falls
--   back to a sorted plan.
-- - R3: an explicit override that differs from the index collation.  No
--   index condition is possible, and the hint falls back to a sorted plan.
--   "POSIX" and "C" are distinct collation OIDs with identical semantics.
SELECT $$COLLATE "C" IN ('0', '1', '2')$$ AS "R1" \gset
SELECT $$IN ('0', '1', '2')$$ AS "R2" \gset
SELECT $$COLLATE "POSIX" IN ('0', '1', '2')$$ AS "R3" \gset

\set query ':P :Q SELECT i2, i4, n, t FROM tv_tbl WHERE t :R ORDER BY i2, i4, n LIMIT 5;'
\i :run_query

-- Cleanup
DROP INDEX idx;
