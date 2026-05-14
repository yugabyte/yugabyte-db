--
-- See yb_merge_scan_schedule for details about the test.
--

\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/merge_scan_setup.sql'
\i :filename

-- No order
-- Merge scan should not be used.
\set query ':explain :Q SELECT * FROM h3r2n WHERE h1 = 6 AND h2 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) AND h3 = 1 LIMIT 5;'
\i :iter_Q2

-- =, IN, sort...
-- Merge scan should not be used.
\set query ':explain :Q SELECT h1, h3, r1, r2, n, h2 FROM h3r2n WHERE h1 = 6 AND h2 IN (1, 3, 5, 7, 9) ORDER BY h3, r1, r2, n LIMIT 5;'
\i :iter_Q2

-- =, IN, =, sort...
\set query ':P :Q SELECT h1, h3, r1, r2, n, h2 FROM h3r2n WHERE h1 = 1 AND h2 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) AND h3 = 1 ORDER BY r1, r2, n LIMIT 5;'
\i :iter_P2

-- =, IN, =/sort, sort...
\set query ':P :Q SELECT h1, h3, r1, r2, n, h2 FROM h3r2n WHERE h1 = 1 AND h2 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) AND h3 = 1 ORDER BY h3, r1, r2, n LIMIT 5;'
\i :iter_P2

-- =, =, IN, sort...
\set query ':P :Q SELECT h1, h2, r1, r2, n, h3 FROM h3r2n WHERE h1 = 1 AND h2 = 8 AND h3 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) ORDER BY r1, r2, n LIMIT 5;'
\i :iter_P2

-- =, =, IN/sort, sort...
-- Merge scan should not be used.
\set query ':explain :Q SELECT h1, h2, h3, r1, r2, n FROM h3r2n WHERE h1 = 1 AND h2 = 8 AND h3 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) ORDER BY h3, r1, r2, n LIMIT 5;'
\i :iter_Q2

-- =/sort, IN, IN, sort...
\set query ':P :Q SELECT h1, r1, r2, n, h2, h3 FROM h3r2n WHERE h1 = 2 AND h2 IN (3, 4, 5, 6, 7) AND h3 IN (6, 7, 8, 9, 10) ORDER BY h1, r1, r2, n LIMIT 5;'
\i :iter_P2

-- yb_hash_code inequality
\set query ':P :Q SELECT r1, r2, n, yb_hash_code(h1, h2, h3), h1, h2, h3 FROM h3r2n WHERE yb_hash_code(h1, h2, h3) < 12283 AND h1 IN (5, 7) AND h2 IN (1, 3, 7) AND h3 IN (3, 6, 7) ORDER BY r1, r2, n LIMIT 5;'
\i :iter_P2

-- yb_hash_code equality
-- Third hint is to use merge scan as the second hint ends up using sort.
\set query ':P :Q SELECT r1, r2, n, yb_hash_code(h1, h2, h3), h1, h2, h3 FROM h3r2n WHERE yb_hash_code(h1, h2, h3) = 28655 AND h1 IN (5, 7) AND h2 IN (1, 3, 7) AND h3 IN (3, 6, 7) ORDER BY r1, r2, n LIMIT 5;'
\set Q3 '/*+Set(enable_sort off) Set(yb_max_merge_scan_streams 64)*/'
\set Pnext :iter_Q3
\i :iter_P2

-- yb_hash_code IN
-- Third hint is to use merge scan as the second hint ends up using sort.
\set query ':P :Q SELECT r1, r2, n, yb_hash_code(h1, h2, h3), h1, h2, h3 FROM h3r2n WHERE yb_hash_code(h1, h2, h3) IN (17834, 28655, 32412) AND h1 IN (5, 7) AND h2 IN (1, 3, 7) AND h3 IN (3, 6, 7) ORDER BY r1, r2, n LIMIT 5;'
\i :iter_P2

-- #30096: Merge scan shouldn't be used in a parallel scan.
\set query ':explain :Q SELECT * FROM h3r2n WHERE h1 = 1 AND h2 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) AND h3 = 1 ORDER BY r1, r2;'
\set Q3 '/*+Parallel(h3r2n 2) Set(yb_enable_parallel_scan_hash_sharded true) Set(yb_parallel_range_rows 1) Set(yb_max_merge_scan_streams 0)*/'
\set Q4 '/*+Parallel(h3r2n 2) Set(yb_enable_parallel_scan_hash_sharded true) Set(yb_parallel_range_rows 1) Set(yb_max_merge_scan_streams 64)*/'
\i :iter_Q4

-- Same thing with backwards scan.
\set query ':explain :Q SELECT * FROM h3r2n WHERE h1 = 1 AND h2 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) AND h3 = 1 ORDER BY r1 DESC, r2 DESC;'
\i :iter_Q4
