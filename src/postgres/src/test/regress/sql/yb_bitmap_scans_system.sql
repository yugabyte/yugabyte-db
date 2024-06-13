--
-- YB Bitmap Scans on System Tables (bitmap index scans + YB bitmap table scans)
--
SET yb_explain_hide_non_deterministic_fields = true;
SET yb_enable_bitmapscan = true;
SET enable_bitmapscan = true;

/*+ BitmapScan(pg_authid) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF)
SELECT * FROM pg_authid WHERE rolname LIKE 'pg_%' OR rolname LIKE 'yb_%' ORDER BY rolname;
/*+ BitmapScan(pg_authid) */
SELECT * FROM pg_authid WHERE rolname LIKE 'pg_%' OR rolname LIKE 'yb_%' ORDER BY rolname;

/*+ BitmapScan(pg_roles) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF) SELECT spcname FROM pg_tablespace WHERE spcowner NOT IN (
    SELECT oid FROM pg_roles WHERE rolname = 'postgres' OR rolname LIKE 'pg_%' OR rolname LIKE 'yb_%');
/*+ BitmapScan(pg_roles) */ SELECT spcname FROM pg_tablespace WHERE spcowner NOT IN (
    SELECT oid FROM pg_roles WHERE rolname = 'postgres' OR rolname LIKE 'pg_%' OR rolname LIKE 'yb_%');

SET yb_enable_expression_pushdown = false;

/*+ BitmapScan(pg_roles) */ EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF) SELECT spcname FROM pg_tablespace WHERE spcowner NOT IN (
    SELECT oid FROM pg_roles WHERE rolname = 'postgres' OR rolname LIKE 'pg_%' OR rolname LIKE 'yb_%');
/*+ BitmapScan(pg_roles) */ SELECT spcname FROM pg_tablespace WHERE spcowner NOT IN (
    SELECT oid FROM pg_roles WHERE rolname = 'postgres' OR rolname LIKE 'pg_%' OR rolname LIKE 'yb_%');

RESET yb_enable_expression_pushdown;
RESET yb_explain_hide_non_deterministic_fields;
RESET yb_enable_bitmapscan;
RESET enable_bitmapscan;
