--
-- YB Bitmap Scans on System Tables (bitmap index scans + YB bitmap table scans)
--
\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/parameterized_query.sql'
\i :filename
\set P1 ':explain'
\set P2
\set explain 'EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)'
\set Q1 '/*+ BitmapScan(pg_authid) */'

SET yb_enable_bitmapscan = true;
SET enable_bitmapscan = true;

SELECT $$
:P :Q1 SELECT rolname FROM pg_authid WHERE rolname LIKE 'pg_%' OR rolname LIKE 'yb_%' ORDER BY rolname;
$$ AS query \gset
\i :iter_P2

SELECT $$
:P :Q1
SELECT spcname FROM pg_tablespace WHERE spcowner NOT IN (
    SELECT oid FROM pg_roles WHERE rolname = 'postgres' OR rolname LIKE 'pg_%' OR rolname LIKE 'yb_%');
$$ AS query \gset
\i :iter_P2

SET yb_enable_expression_pushdown = false;

SELECT $$
:P :Q1
SELECT spcname FROM pg_tablespace WHERE spcowner NOT IN (
    SELECT oid FROM pg_roles WHERE rolname = 'postgres' OR rolname LIKE 'pg_%' OR rolname LIKE 'yb_%');
$$ AS query \gset
\i :iter_P2

RESET yb_enable_expression_pushdown;
RESET yb_enable_bitmapscan;
RESET enable_bitmapscan;
