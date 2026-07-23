--
-- See yb_merge_scan_schedule for details about the test.  Tests merge scan
-- with collation mismatches between the clause and the index column.
--
\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/data/explainrun_merge_scan.sql'
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
\set hint1 '/*+IndexScan(tv_tbl idx) Set(yb_max_merge_scan_streams 0)*/'
\set hint2 '/*+IndexScan(tv_tbl idx) Set(yb_max_merge_scan_streams 64)*/'

-- Where the clause collation comes from for each query:
-- - an explicit override matching the index collation.  The clause forms an
--   index condition and merge streams.
\set query 'SELECT i2, i4, n, t FROM tv_tbl WHERE t COLLATE "C" IN (''0'', ''1'', ''2'') ORDER BY i2, i4, n LIMIT 5'
:explain2run2

-- - the bare column carries its default collation, which does not match the
--   index collation.  No index condition is possible, and the hint falls
--   back to a sorted plan.
\set query 'SELECT i2, i4, n, t FROM tv_tbl WHERE t IN (''0'', ''1'', ''2'') ORDER BY i2, i4, n LIMIT 5'
:explain2run2

-- - an explicit override that differs from the index collation.  No index
--   condition is possible, and the hint falls back to a sorted plan.
--   "POSIX" and "C" are distinct collation OIDs with identical semantics.
\set query 'SELECT i2, i4, n, t FROM tv_tbl WHERE t COLLATE "POSIX" IN (''0'', ''1'', ''2'') ORDER BY i2, i4, n LIMIT 5'
:explain2run2

-- Cleanup
DROP INDEX idx;
