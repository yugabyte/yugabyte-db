--
-- Test disabling YB Bitmap Scans
--
-- for each combination of yb_enable_bitmapscan and enable_bitmapscan, try
--  1. a case where the planner chooses bitmap scans
--  2. a case where we tell the planner to use bitmap scans
--  3. a case where the alternative option (seq scan) is disabled
--
\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/explainrun.sql'
\i :filename
\set explain 'EXPLAIN (COSTS OFF)'

CREATE TABLE test_disable(a int, b int);
CREATE INDEX ON test_disable(a ASC);
CREATE INDEX ON test_disable(b ASC);

CREATE TEMP TABLE tmp_test_disable(a int, b int);
CREATE INDEX ON tmp_test_disable(a ASC);
CREATE INDEX ON tmp_test_disable(b ASC);

\set query_test_disable 'SELECT * FROM test_disable WHERE a < 5 OR b < 5'
\set hint_test_disable '/*+ BitmapScan(test_disable) */'
\set query_tmp_test_disable 'SELECT * FROM tmp_test_disable WHERE a < 5 OR b < 5'
\set hint_tmp_test_disable '/*+ BitmapScan(tmp_test_disable) */'
\set hint3 '/*+ Set(enable_seqscan false) */'

SET yb_enable_bitmapscan = true;
SET enable_bitmapscan = true;
\set query ':query_test_disable'
\set hint2 ':hint_test_disable'
:explain3
\set query ':query_tmp_test_disable'
\set hint2 ':hint_tmp_test_disable'
:explain3

SET yb_enable_bitmapscan = true;
SET enable_bitmapscan = false;
\set query ':query_test_disable'
\set hint2 ':hint_test_disable'
:explain2
\set query ':query_tmp_test_disable'
\set hint2 ':hint_tmp_test_disable'
:explain2

SET yb_enable_bitmapscan = false;
SET enable_bitmapscan = true;
\set query ':query_test_disable'
\set hint2 ':hint_test_disable'
:explain2
\set query ':query_tmp_test_disable'
\set hint2 ':hint_tmp_test_disable'
:explain3

SET yb_enable_bitmapscan = false;
SET enable_bitmapscan = false;
\set query ':query_test_disable'
\set hint2 ':hint_test_disable'
:explain2
\set query ':query_tmp_test_disable'
\set hint2 ':hint_tmp_test_disable'
:explain2

RESET enable_bitmapscan;
RESET yb_enable_bitmapscan;
