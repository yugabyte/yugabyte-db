-- #### Test scans on the table, primary key index and the secondary index without targets #### --
CREATE TABLE t_kv(k int, v int, PRIMARY KEY(k ASC));
INSERT INTO t_kv SELECT x, x FROM generate_series(1, 10) AS x;
INSERT INTO t_kv VALUES (11, 1); -- test duplicate key
INSERT INTO t_kv VALUES (12, NULL); -- test null values

-- yb seq scan
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) /*+ SeqScan(t_kv) */ SELECT k FROM t_kv;
/*+ SeqScan(t_kv) */ SELECT k FROM t_kv;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) /*+ SeqScan(t_kv) */ SELECT FROM t_kv;
/*+ SeqScan(t_kv) */ SELECT FROM t_kv;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) /*+ SeqScan(t_kv) */ SELECT count(1) FROM t_kv;
/*+ SeqScan(t_kv) */ SELECT count(1) FROM t_kv;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) /*+ SeqScan(t_kv) */ SELECT count(*) FROM t_kv;
/*+ SeqScan(t_kv) */ SELECT count(*) FROM t_kv;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) /*+ SeqScan(t_kv) */ SELECT 1 FROM t_kv;
/*+ SeqScan(t_kv) */ SELECT 1 FROM t_kv;

-- seq scan
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT FROM t_kv;
SELECT FROM t_kv;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT k FROM t_kv;
SELECT k FROM t_kv;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT count(1) FROM t_kv;
SELECT count(1) FROM t_kv;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT count(*) FROM t_kv;
SELECT count(*) FROM t_kv;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT 1 FROM t_kv;
SELECT 1 FROM t_kv;

-- index only scan on the primary index
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) /*+ IndexOnlyScan(t_kv) */ SELECT FROM t_kv;
/*+ IndexOnlyScan(t_kv) */ SELECT FROM t_kv;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) /*+ IndexOnlyScan(t_kv) */ SELECT k FROM t_kv;
/*+ IndexOnlyScan(t_kv) */ SELECT k FROM t_kv;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) /*+ IndexOnlyScan(t_kv) */ SELECT count(1) FROM t_kv;
/*+ IndexOnlyScan(t_kv) */ SELECT count(1) FROM t_kv;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) /*+ IndexOnlyScan(t_kv) */ SELECT count(*) FROM t_kv;
/*+ IndexOnlyScan(t_kv) */ SELECT count(*) FROM t_kv;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) /*+ IndexOnlyScan(t_kv) */ SELECT 1 FROM t_kv;
/*+ IndexOnlyScan(t_kv) */ SELECT 1 FROM t_kv;

CREATE INDEX t_vi ON t_kv (v asc);

-- index only scan on the secondary index
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) /*+ IndexOnlyScan(t_kv t_vi) */ SELECT FROM t_kv;
/*+ IndexOnlyScan(t_kv t_vi) */ SELECT FROM t_kv;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) /*+ IndexOnlyScan(t_kv t_vi) */ SELECT k FROM t_kv;
/*+ IndexOnlyScan(t_kv t_vi) */ SELECT k FROM t_kv;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) /*+ IndexOnlyScan(t_kv t_vi) */ SELECT count(1) FROM t_kv;
/*+ IndexOnlyScan(t_kv t_vi) */ SELECT count(1) FROM t_kv;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) /*+ IndexOnlyScan(t_kv t_vi) */ SELECT count(*) FROM t_kv;
/*+ IndexOnlyScan(t_kv t_vi) */ SELECT count(*) FROM t_kv;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) /*+ IndexOnlyScan(t_kv t_vi) */ SELECT 1 FROM t_kv;
/*+ IndexOnlyScan(t_kv t_vi) */ SELECT 1 FROM t_kv;

-- #### Test Deleting #### --

-- seq scan
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) DELETE FROM t_kv;
INSERT INTO t_kv SELECT x, x FROM generate_series(1, 10) AS x;
SELECT * FROM t_kv;

-- yb seq scan
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) /*+ SeqScan(t_kv) */ DELETE FROM t_kv;
INSERT INTO t_kv SELECT x, x FROM generate_series(1, 10) AS x;
/*+ SeqScan(t_kv) */ SELECT * FROM t_kv;

-- index only scan on the primary index
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) /*+ IndexOnlyScan(t_kv t_kv_pkey) */ DELETE FROM t_kv;
INSERT INTO t_kv SELECT x, x FROM generate_series(1, 10) AS x;
/*+ IndexOnlyScan(t_kv t_kv_pkey) */ SELECT * FROM t_kv;

-- index only scan on the secondary index
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) /*+ IndexOnlyScan(t_kv t_vi) */ DELETE FROM t_kv;
INSERT INTO t_kv SELECT x, x FROM generate_series(1, 10) AS x;
/*+ IndexOnlyScan(t_kv t_vi) */ SELECT * FROM t_kv;

DROP TABLE t_kv;
