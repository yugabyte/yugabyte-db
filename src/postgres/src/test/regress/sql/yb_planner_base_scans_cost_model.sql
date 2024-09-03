-- This file tests the YB CBO base scans cost model in non-colocated tables.
SET yb_enable_base_scans_cost_model = ON;
SET yb_enable_optimizer_statistics = ON;
CREATE TABLE test (v1 INT, v2 INT, v3 INT);
CREATE INDEX test_index ON test ((v1) HASH, v2 ASC) INCLUDE (v3);
EXPLAIN (COSTS OFF) SELECT * FROM test WHERE v2 > 100;
SELECT * FROM test WHERE v2 > 100;
DROP TABLE test;
