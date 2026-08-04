--
-- See yb_merge_scan_schedule for details about the test.
--

\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/merge_scan_setup.sql'
\i :filename

-- No order
-- Merge scan should not be used.
\set query ':explain :Q SELECT * FROM parent WHERE r1 IN (0, 1, 2, 3) LIMIT 5;'
\i :iter_Q2

-- Forward scan
\set query ':P :Q SELECT * FROM parent WHERE r1 IN (0, 1, 2, 3) ORDER BY r2, r3, p1, p2, n LIMIT 5;'
\i :iter_P2

-- Backward scan
\set query ':P :Q SELECT * FROM parent WHERE r1 IN (0, 1, 2, 3) ORDER BY r2 DESC, r3 DESC, p1 DESC, p2 DESC, n LIMIT 5;'
\i :iter_P2

--
-- Partitioned index
--
CREATE INDEX ON parent (p2 ASC, r2, p1, r3, r1);

-- No order
-- Merge scan should not be used.
\set query ':explain :Q SELECT * FROM parent WHERE p2 IN (0, 1, 2, 3) LIMIT 5;'
\i :iter_Q2

-- Forward scan
\set query ':P :Q SELECT * FROM parent WHERE p2 IN (0, 1, 2, 3) ORDER BY r2, p1, r3, r1, n LIMIT 5;'
\i :iter_P2

-- Backward scan
\set query ':P :Q SELECT * FROM parent WHERE p2 IN (0, 1, 2, 3) ORDER BY r2 DESC, p1 DESC, r3 DESC, r1 DESC, n LIMIT 5;'
\i :iter_P2

-- (Drop this index)
DROP INDEX parent_p2_r2_p1_r3_r1_idx;
