-- This file tests the YB CBO base scans cost model in non-colocated tables.
SET yb_enable_base_scans_cost_model = ON;
SET yb_enable_optimizer_statistics = ON;
CREATE TABLE test (v1 INT, v2 INT, v3 INT);
CREATE INDEX test_index ON test ((v1) HASH, v2 ASC) INCLUDE (v3);
EXPLAIN (COSTS OFF) SELECT * FROM test WHERE v2 > 100;
SELECT * FROM test WHERE v2 > 100;
DROP TABLE test;


-- #24496 : Bad costs for index scan when ANALYZE is not run
CREATE TABLE t1 (k1 INT, v1 INT, PRIMARY KEY (k1 ASC));
INSERT INTO t1 (SELECT s, s FROM generate_series(1, 100000) s);
CREATE INDEX t1_v1 ON t1 (v1 ASC);
SET yb_enable_base_scans_cost_model = ON;
/*+ IndexScan(t1) */ EXPLAIN SELECT * FROM t1 WHERE k1 > 80000;
/*+ IndexOnlyScan(t1 t1_v1) */ EXPLAIN SELECT v1 FROM t1;
/*+ IndexOnlyScan(t1 t1_v1) */ EXPLAIN SELECT v1 FROM t1 WHERE v1 < 50000;

-- ANALYZE produces a rough estimate of the number of rows. This can make 
-- the test flaky. To stabilize the test we write to pg_class.reltuples manually.
SET yb_non_ddl_txn_for_sys_tables_allowed = on;
UPDATE pg_class SET reltuples=100000 WHERE relname LIKE 't1%';
UPDATE pg_yb_catalog_version SET current_version=current_version+1 WHERE db_oid=1;
SET yb_non_ddl_txn_for_sys_tables_allowed = off;

\c yugabyte
SELECT reltuples FROM pg_class where relname LIKE 't1%';

SET yb_enable_base_scans_cost_model = ON;
/*+ IndexScan(t1) */ EXPLAIN SELECT * FROM t1 WHERE k1 > 80000;
/*+ IndexOnlyScan(t1 t1_v1) */ EXPLAIN SELECT v1 FROM t1;
/*+ IndexOnlyScan(t1 t1_v1) */ EXPLAIN SELECT v1 FROM t1 WHERE v1 < 50000;
