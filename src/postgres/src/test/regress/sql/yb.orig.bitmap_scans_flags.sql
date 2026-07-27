--
-- Test disabling YB Bitmap Scans
--
-- for each combination of yb_enable_bitmapscan and enable_bitmapscan, try
--  1. a case where the planner chooses bitmap scans
--  2. a case where we tell the planner to use bitmap scans
--
\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/parameterized_query.sql'
\i :filename

CREATE TABLE test_disable(a int, b int);
CREATE INDEX ON test_disable(a ASC);
CREATE INDEX ON test_disable(b ASC);

CREATE TEMP TABLE tmp_test_disable(a int, b int);
CREATE INDEX ON tmp_test_disable(a ASC);
CREATE INDEX ON tmp_test_disable(b ASC);

\set P1 'test_disable'
\set P2 'tmp_test_disable'
\set Q1
\set Q2 ':hint'
\set bitmap_hint_from_P :abs_srcdir '/yb_commands/bitmap_hint_from_P.sql'
SELECT $$EXPLAIN (COSTS OFF) :Q SELECT * FROM :P WHERE a < 5 OR b < 5;$$ AS query \gset

SET yb_enable_bitmapscan = true;
SET enable_bitmapscan = true;
\set Pnext :bitmap_hint_from_P
\i :run_query

SET yb_enable_bitmapscan = true;
SET enable_bitmapscan = false;
\set Pnext :bitmap_hint_from_P
\i :run_query

SET yb_enable_bitmapscan = false;
SET enable_bitmapscan = true;
\set Pnext :bitmap_hint_from_P
\i :run_query

SET yb_enable_bitmapscan = false;
SET enable_bitmapscan = false;
\set Pnext :bitmap_hint_from_P
\i :run_query

RESET enable_bitmapscan;
RESET yb_enable_bitmapscan;
