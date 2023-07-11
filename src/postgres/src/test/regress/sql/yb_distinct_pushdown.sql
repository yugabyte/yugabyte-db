CREATE TABLE distinct_pushdown_table(r1 INT, r2 INT, PRIMARY KEY(r1 ASC, r2 ASC));
INSERT INTO distinct_pushdown_table (SELECT 1, i FROM GENERATE_SERIES(1, 1000) AS i);

-- Disable DISTINCT pushdown
SET yb_enable_distinct_pushdown TO off;

-- Must pull even duplicate rows without pushdown. Verify that using EXPLAIN ANALYZE
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r1 FROM distinct_pushdown_table WHERE r1 <= 10;

-- Enable DISTINCT pushdown
SET yb_enable_distinct_pushdown TO on;

-- Must pull fewer rows with pushdown. Verify that using EXPLAIN ANALYZE
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r1 FROM distinct_pushdown_table WHERE r1 <= 10;

DROP TABLE distinct_pushdown_table;
