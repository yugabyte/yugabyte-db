--
-- YB Bitmap Scans on System Tables (bitmap index scans + YB bitmap table scans)
--
\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/explainrun.sql'
\i :filename
\set explain 'EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)'
\set hint1 '/*+ BitmapScan(pg_authid) */'

SET yb_enable_bitmapscan = true;
SET enable_bitmapscan = true;

SELECT $$
SELECT rolname FROM pg_authid WHERE rolname LIKE 'pg_%' OR rolname LIKE 'yb_%' ORDER BY rolname
$$ AS query \gset
:explain1run1

SELECT $$
SELECT spcname FROM pg_tablespace WHERE spcowner NOT IN (
    SELECT oid FROM pg_roles WHERE rolname = 'postgres' OR rolname LIKE 'pg_%' OR rolname LIKE 'yb_%')
$$ AS query \gset
:explain1run1

SET yb_enable_expression_pushdown = false;

SELECT $$
SELECT spcname FROM pg_tablespace WHERE spcowner NOT IN (
    SELECT oid FROM pg_roles WHERE rolname = 'postgres' OR rolname LIKE 'pg_%' OR rolname LIKE 'yb_%')
$$ AS query \gset
:explain1run1

RESET yb_enable_expression_pushdown;
RESET yb_enable_bitmapscan;
RESET enable_bitmapscan;
