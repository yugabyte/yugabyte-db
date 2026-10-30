--
-- See yb_merge_scan_schedule for details about the test.
--

\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/merge_scan_setup.sql'
\i :filename

-- No order
-- Merge scan should not be used.
\set query ':explain :Q SELECT * FROM h3r2n WHERE h1 = 6 AND h2 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) AND h3 = 1 LIMIT 5;'
\i :run_query

-- =, IN, sort...
-- Merge scan should not be used.
\set query ':explain :Q SELECT h1, h3, r1, r2, n, h2 FROM h3r2n WHERE h1 = 6 AND h2 IN (1, 3, 5, 7, 9) ORDER BY h3, r1, r2, n LIMIT 5;'
\i :run_query

-- =, IN, =, sort...
\set query ':P :Q SELECT h1, h3, r1, r2, n, h2 FROM h3r2n WHERE h1 = 1 AND h2 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) AND h3 = 1 ORDER BY r1, r2, n LIMIT 5;'
\i :run_query

-- =, IN, =/sort, sort...
\set query ':P :Q SELECT h1, h3, r1, r2, n, h2 FROM h3r2n WHERE h1 = 1 AND h2 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) AND h3 = 1 ORDER BY h3, r1, r2, n LIMIT 5;'
\i :run_query

-- =, =, IN, sort...
\set query ':P :Q SELECT h1, h2, r1, r2, n, h3 FROM h3r2n WHERE h1 = 1 AND h2 = 8 AND h3 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) ORDER BY r1, r2, n LIMIT 5;'
\i :run_query

-- =, =, IN/sort, sort...
-- Merge scan should not be used.
\set query ':explain :Q SELECT h1, h2, h3, r1, r2, n FROM h3r2n WHERE h1 = 1 AND h2 = 8 AND h3 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) ORDER BY h3, r1, r2, n LIMIT 5;'
\i :run_query

-- =/sort, IN, IN, sort...
\set query ':P :Q SELECT h1, r1, r2, n, h2, h3 FROM h3r2n WHERE h1 = 2 AND h2 IN (3, 4, 5, 6, 7) AND h3 IN (6, 7, 8, 9, 10) ORDER BY h1, r1, r2, n LIMIT 5;'
\i :run_query

-- yb_hash_code inequality
\set query ':P :Q SELECT r1, r2, n, yb_hash_code(h1, h2, h3), h1, h2, h3 FROM h3r2n WHERE yb_hash_code(h1, h2, h3) < 12283 AND h1 IN (5, 7) AND h2 IN (1, 3, 7) AND h3 IN (3, 6, 7) ORDER BY r1, r2, n LIMIT 5;'
\i :run_query

-- yb_hash_code equality
-- Third hint is to use merge scan as the second hint ends up using sort.
\set query ':P :Q SELECT r1, r2, n, yb_hash_code(h1, h2, h3), h1, h2, h3 FROM h3r2n WHERE yb_hash_code(h1, h2, h3) = 28655 AND h1 IN (5, 7) AND h2 IN (1, 3, 7) AND h3 IN (3, 6, 7) ORDER BY r1, r2, n LIMIT 5;'
\set Q3 '/*+Set(enable_sort off) Set(yb_max_merge_scan_streams 64)*/'
\i :run_query

-- yb_hash_code IN
-- Third hint is to use merge scan as the second hint ends up using sort.
\set query ':P :Q SELECT r1, r2, n, yb_hash_code(h1, h2, h3), h1, h2, h3 FROM h3r2n WHERE yb_hash_code(h1, h2, h3) IN (17834, 28655, 32412) AND h1 IN (5, 7) AND h2 IN (1, 3, 7) AND h3 IN (3, 6, 7) ORDER BY r1, r2, n LIMIT 5;'
\i :run_query

-- yb_hash_code equality on a partial index implying the hash columns
-- The predicate implies h1 = :h1, h2 = :h2, and h3 = :h3, so they are not index
-- conditions, and the yb_hash_code equality does not bind the hash columns.  A
-- merge scan needs a condition on every hash column, so it errors.
-- Third hint pins the partial index and turns sort off so that the merge scan
-- is chosen.
-- TODO(#34120): this should run without error.
SELECT h1, h2, h3, yb_hash_code(h1, h2, h3) AS hc FROM h3r2n ORDER BY n LIMIT 1 \gset
CREATE INDEX NONCONCURRENTLY h3r2n_part_idx ON h3r2n ((h1, h2, h3) HASH, r1 ASC, r2 ASC) WHERE h1 = :h1 AND h2 = :h2 AND h3 = :h3;
\set query ':P :Q SELECT r2, n, r1, h1, h2, h3 FROM h3r2n WHERE yb_hash_code(h1, h2, h3) = :hc AND h1 = :h1 AND h2 = :h2 AND h3 = :h3 AND r1 IN (1, 3, 5, 7, 9) ORDER BY r2, n LIMIT 5;'
\set Q3 '/*+IndexScan(h3r2n h3r2n_part_idx) Set(enable_sort off) Set(yb_max_merge_scan_streams 64)*/'
\i :run_query
\unset Q3
DROP INDEX h3r2n_part_idx;

-- #30096: Merge scan shouldn't be used in a parallel scan.
-- Explain without ANALYZE because a parallel query does not necessarily get
-- the workers the planner asked for, so the per worker row counts, loop
-- counts, and sort memory that ANALYZE prints vary from run to run.
\set explain 'EXPLAIN (VERBOSE, COSTS OFF)'
\set query ':explain :Q SELECT * FROM h3r2n WHERE h1 = 1 AND h2 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) AND h3 = 1 ORDER BY r1, r2;'
\set Q3 '/*+Parallel(h3r2n 2) Set(yb_enable_parallel_scan_hash_sharded true) Set(yb_parallel_range_rows 1) Set(yb_test_force_parallel force) Set(yb_max_merge_scan_streams 0)*/'
\set Q4 '/*+Parallel(h3r2n 2) Set(yb_enable_parallel_scan_hash_sharded true) Set(yb_parallel_range_rows 1) Set(yb_test_force_parallel force) Set(yb_max_merge_scan_streams 64)*/'
\i :run_query

-- Same thing with backwards scan.
\set query ':explain :Q SELECT * FROM h3r2n WHERE h1 = 1 AND h2 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) AND h3 = 1 ORDER BY r1 DESC, r2 DESC;'
\i :run_query
