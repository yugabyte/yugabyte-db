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

-- #20892 : Divide by 0 error in some queries with new cost model when yb_fetch_size_limit is enforced
CREATE TABLE test_20892 (k INT, v VARCHAR(1024));
INSERT INTO test_20892 (SELECT s, repeat(md5(s::text), 32) FROM generate_series(1, 100) s);
ANALYZE test_20892;
set yb_fetch_row_limit = 1024;
set yb_fetch_size_limit = '1MB';
explain (analyze, costs off, summary off, timing off) SELECT 0 FROM test_20892;
SELECT count(*) FROM test_20892;
\d test_20892

set yb_fetch_row_limit = 100;
set yb_fetch_size_limit = '10kB';
explain (analyze, costs off, summary off, timing off) SELECT 0 FROM test_20892;
SELECT count(*) FROM test_20892;
\d test_20892

DROP TABLE test_20892;
