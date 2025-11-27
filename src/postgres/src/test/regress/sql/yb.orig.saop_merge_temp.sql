--
-- See yb_saop_merge_schedule for details about the test.
--

\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/explainrun_saop_merge.sql'
\i :filename

CREATE TEMP TABLE tmp (
    r1 int2,
    r2 int8,
    r3 int,
    r4 numeric,
    r5 float8,
    n int GENERATED ALWAYS AS ((r1 + r2 * 10 + r3 * 100 + r4 * 1000 + r5 * 10000)::int) STORED,
    PRIMARY KEY (r1, r2, r3, r4, r5));
INSERT INTO tmp SELECT r1, r2, r3, r4, r5 FROM r5n;

-- Temp table
-- SAOP merge should not be used.
SET enable_bitmapscan = off;
\set query 'SELECT * FROM tmp WHERE r1 IN (0, 1, 2, 3) ORDER BY r2, r3, r4, n LIMIT 5'
:explain2
RESET enable_bitmapscan;
