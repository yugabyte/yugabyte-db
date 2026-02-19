--
-- See yb_saop_merge_schedule for details about the test.
--

\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/explainrun_saop_merge.sql'
\i :filename

-- No order
-- SAOP merge should not be used.
\set query 'SELECT * FROM parent WHERE r1 IN (0, 1, 2, 3) LIMIT 5'
:explain2

-- Forward scan
\set query 'SELECT * FROM parent WHERE r1 IN (0, 1, 2, 3) ORDER BY r2, r3, p1, p2, n LIMIT 5'
:explain2run2

-- Backward scan
\set query 'SELECT * FROM parent WHERE r1 IN (0, 1, 2, 3) ORDER BY r2 DESC, r3 DESC, p1 DESC, p2 DESC, n LIMIT 5'
:explain2run2

--
-- Partitioned index
--
CREATE INDEX ON parent (p2 ASC, r2, p1, r3, r1);

-- No order
-- SAOP merge should not be used.
\set query 'SELECT * FROM parent WHERE p2 IN (0, 1, 2, 3) LIMIT 5'
:explain2

-- Forward scan
\set query 'SELECT * FROM parent WHERE p2 IN (0, 1, 2, 3) ORDER BY r2, p1, r3, r1, n LIMIT 5'
:explain2run2

-- Backward scan
\set query 'SELECT * FROM parent WHERE p2 IN (0, 1, 2, 3) ORDER BY r2 DESC, p1 DESC, r3 DESC, r1 DESC, n LIMIT 5'
:explain2run2

-- (Drop this index)
DROP INDEX parent_p2_r2_p1_r3_r1_idx;
