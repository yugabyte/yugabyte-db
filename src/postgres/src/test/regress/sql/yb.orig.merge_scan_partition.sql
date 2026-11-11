--
-- See yb_merge_scan_schedule for details about the test.
--

\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/merge_scan_setup.sql'
\i :filename

-- No order
-- Merge scan should not be used.
\set query ':explain :Q SELECT * FROM parent WHERE r1 IN (0, 1, 2, 3) LIMIT 5;'
\i :run_query

-- Forward scan
\set query ':P :Q SELECT * FROM parent WHERE r1 IN (0, 1, 2, 3) ORDER BY r2, r3, p1, p2, n LIMIT 5;'
\i :run_query

-- Backward scan
\set query ':P :Q SELECT * FROM parent WHERE r1 IN (0, 1, 2, 3) ORDER BY r2 DESC, r3 DESC, p1 DESC, p2 DESC, n LIMIT 5;'
\i :run_query

-- =-var equivalence covering a gap between sort columns
-- r2 = r3 makes r3 redundant for the ORDER BY, and the merge also sorts on r3.
-- The filter runs in storage, or locally with expression pushdown off.
\set query ':P :Q SELECT r2, p1, p2, n, r1, r3 FROM parent WHERE r1 IN (0, 1, 2, 3) AND r2 = r3 ORDER BY r2, p1, p2, n LIMIT 5;'
\i :run_query
SET yb_enable_expression_pushdown = off;
\i :run_query
RESET yb_enable_expression_pushdown;

--
-- Partitioned index
--
CREATE INDEX ON parent (p2 ASC, r2, p1, r3, r1);

-- No order
-- Merge scan should not be used.
\set query ':explain :Q SELECT * FROM parent WHERE p2 IN (0, 1, 2, 3) LIMIT 5;'
\i :run_query

-- Forward scan
\set query ':P :Q SELECT * FROM parent WHERE p2 IN (0, 1, 2, 3) ORDER BY r2, p1, r3, r1, n LIMIT 5;'
\i :run_query

-- Backward scan
\set query ':P :Q SELECT * FROM parent WHERE p2 IN (0, 1, 2, 3) ORDER BY r2 DESC, p1 DESC, r3 DESC, r1 DESC, n LIMIT 5;'
\i :run_query

-- (Drop this index)
DROP INDEX parent_p2_r2_p1_r3_r1_idx;
