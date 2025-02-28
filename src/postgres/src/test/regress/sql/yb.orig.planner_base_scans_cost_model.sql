-- This file tests the YB CBO base scans cost model in non-colocated tables.
SET yb_enable_base_scans_cost_model = ON;
SET yb_enable_optimizer_statistics = ON;
CREATE TABLE test (v1 INT, v2 INT, v3 INT);
CREATE INDEX test_index ON test ((v1) HASH, v2 ASC) INCLUDE (v3);
EXPLAIN (COSTS OFF) SELECT * FROM test WHERE v2 > 100;
SELECT * FROM test WHERE v2 > 100;
DROP TABLE test;


--------------------------------------------------------------------------------
-- #24496 : Bad costs for index scan when ANALYZE is not run
--------------------------------------------------------------------------------
CREATE TABLE t1 (k1 INT, v1 INT, PRIMARY KEY (k1 ASC));
INSERT INTO t1 (SELECT s, s FROM generate_series(1, 100000) s);
CREATE INDEX t1_v1 ON t1 (v1 ASC);
SET yb_enable_base_scans_cost_model = ON;
/*+ IndexScan(t1) */ EXPLAIN (COSTS OFF) SELECT * FROM t1 WHERE k1 > 80000;
/*+ IndexOnlyScan(t1 t1_v1) */ EXPLAIN (COSTS OFF) SELECT v1 FROM t1;
/*+ IndexOnlyScan(t1 t1_v1) */ EXPLAIN (COSTS OFF) SELECT v1 FROM t1 WHERE v1 < 50000;

-- ANALYZE produces a rough estimate of the number of rows. This can make 
-- the test flaky. To stabilize the test we write to pg_class.reltuples manually.
SET yb_non_ddl_txn_for_sys_tables_allowed = on;
UPDATE pg_class SET reltuples=100000 WHERE relname LIKE 't1%';
UPDATE pg_yb_catalog_version SET current_version=current_version+1 WHERE db_oid=1;
SET yb_non_ddl_txn_for_sys_tables_allowed = off;

\c yugabyte
SELECT reltuples FROM pg_class where relname LIKE 't1%';

SET yb_enable_base_scans_cost_model = ON;
/*+ IndexScan(t1) */ EXPLAIN (COSTS OFF) SELECT * FROM t1 WHERE k1 > 80000;
/*+ IndexOnlyScan(t1 t1_v1) */ EXPLAIN (COSTS OFF) SELECT v1 FROM t1;
/*+ IndexOnlyScan(t1 t1_v1) */ EXPLAIN (COSTS OFF) SELECT v1 FROM t1 WHERE v1 < 50000;


--------------------------------------------------------------------------------
-- #24916 : Partial Index clause is not included in estimate for data transfer costs
--------------------------------------------------------------------------------
CREATE TABLE t_24916 (v1 INT, v2 INT, v3 TEXT);
INSERT INTO t_24916 SELECT s1, s2, repeat('a', 10000) FROM generate_series(1, 40) s1, generate_series(1, 40) s2;
CREATE INDEX t_24916_partial_idx ON t_24916 (v2 ASC) WHERE v1 = 1;
CREATE INDEX t_24916_full_idx_v1_v2 ON t_24916 (v1 ASC, v2 ASC);
ANALYZE t_24916;
SET yb_enable_base_scans_cost_model = ON;

-- Partial Index Scan should be preferred over full index scan or seq scan
EXPLAIN (COSTS OFF) SELECT * FROM t_24916 WHERE v1 = 1 AND v2 < 5;


--------------------------------------------------------------------------------
-- #25682 : Estimated seeks and nexts value can overflow in a large table
--------------------------------------------------------------------------------
CREATE TABLE t_25682 (k1 INT, v1 INT, PRIMARY KEY (k1 ASC));
CREATE INDEX t_25682_idx on t_25682 (v1 ASC);

-- Simluate a large table by setting reltuples in pg_class to 4B row
SET yb_non_ddl_txn_for_sys_tables_allowed = ON;
UPDATE pg_class SET reltuples=4000000000 WHERE relname LIKE '%t_25682%';
UPDATE pg_yb_catalog_version SET current_version=current_version+1 WHERE db_oid=1;
SET yb_non_ddl_txn_for_sys_tables_allowed = OFF;

SET yb_enable_base_scans_cost_model = ON;

/*+ SeqScan(t_25682) */ EXPLAIN (DEBUG, COSTS OFF) SELECT * FROM t_25682 WHERE k1 > 0;
/*+ IndexScan(t_25682 t_25682_pkey) */EXPLAIN (DEBUG, COSTS OFF) SELECT * FROM t_25682 WHERE k1 > 0;
/*+ IndexScan(t_25682 t_25682_idx) */EXPLAIN (DEBUG, COSTS OFF) SELECT * FROM t_25682 WHERE v1 > 0;
/*+ IndexOnlyScan(t_25682 t_25682_idx) */EXPLAIN (DEBUG, COSTS OFF) SELECT v1 FROM t_25682 WHERE v1 > 0;
