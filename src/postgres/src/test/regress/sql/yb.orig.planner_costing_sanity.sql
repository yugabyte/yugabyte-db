-- This file tests that the YB CBO costing code itself evaluates without
-- errors, as opposed to which plans the cost model chooses.
\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/parameterized_query.sql'
\i :filename

SET yb_enable_cbo = ON;
\set explain 'EXPLAIN (COSTS OFF)'
\set P1 ':explain'
\set P2

--------------------------------------------------------------------------------
-- #35015 : name = ANY (text[]) on an analyzed, indexed name column failed at
-- plan time with "compressed pglz data is corrupt" when costing the index
-- scan.  The failure needs a value in the column's histogram whose first byte
-- looks like a compressed-text header, such as 'r'.  Serial and parallel
-- index scans both reach the SAOP estimate today; run both in case
-- partial-path costing is ever split out.
--------------------------------------------------------------------------------
CREATE TABLE t_35015 (n name, PRIMARY KEY (n ASC));
INSERT INTO t_35015 VALUES ('r1'), ('r2');
ANALYZE t_35015;
\set Q1 '/*+ IndexScan(t_35015) */'
\set Q2 '/*+ IndexScan(t_35015) Parallel(t_35015 2 hard) Set(yb_enable_parallel_scan_range_sharded true) Set(yb_parallel_range_rows 1) */'
\set query ':P :Q SELECT count(*) FROM t_35015 WHERE n = ANY (ARRAY[''r1'', ''r2'']);'
\i :run_query
\unset Q1
\unset Q2
DROP TABLE t_35015;
