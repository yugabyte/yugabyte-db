CREATE DATABASE base_scans_cost_model WITH COLOCATION = TRUE;
\c base_scans_cost_model

SET yb_enable_base_scans_cost_model = ON;
SET yb_enable_optimizer_statistics = ON;

-- #18689 : Occasional SIGSEGV in queries with inequality filters on multiple
--          index columns
CREATE TABLE test_18689 (k1 INT, k2 INT, k3 INT, PRIMARY KEY (k1, k2, k3));
INSERT INTO test_18689 (SELECT s, s, s FROM generate_series(1, 10000) s);
ANALYZE test_18689;
/*+ IndexScan(test_18689) */ SELECT count(*) FROM test_18689 WHERE k1 > 5 and k1 < 10;
/*+ IndexScan(test_18689) */ SELECT count(*) FROM test_18689 WHERE k1 > 5 and k1 < 10 and k2 > 5;
/*+ IndexScan(test_18689) */ SELECT count(*) FROM test_18689 WHERE k1 > 5 and k1 < 10 and k2 > 5 and k2 < 10;
/*+ IndexScan(test_18689) */ SELECT count(*) FROM test_18689 WHERE k1 > 5 and k1 < 10 and k2 > 5 and k2 < 10 and k3 > 5;
/*+ IndexScan(test_18689) */ SELECT count(*) FROM test_18689 WHERE k1 > 5 and k1 < 10 and k2 > 5 and k2 < 10 and k3 > 5 and k3 < 10;

CREATE TABLE p100000(c1 INT, PRIMARY KEY(c1 ASC));
INSERT INTO p100000 SELECT generate_series(1,100000);
ANALYZE p100000;
EXPLAIN SELECT * FROM p100000 ORDER BY c1 ASC;
