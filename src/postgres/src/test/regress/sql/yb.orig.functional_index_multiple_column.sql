CREATE TABLE t AS SELECT x, x AS y, x AS z FROM generate_series(1, 10) x;

CREATE INDEX t_hcx_hash_y_hash_idx ON t ((yb_hash_code(x), y) HASH);
-- Query #2 should use the index
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) < 4000 AND y > 4;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) = 2675 AND y IN (5, 6);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) < 4000 AND y IN (5, 6); -- this one uses index successfully on master (because it has recheck)
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) = 2675 AND y > 4;

DROP INDEX t_hcx_hash_y_hash_idx;
CREATE INDEX t_hcx_asc_y_asc_idx ON t (yb_hash_code(x) ASC, y ASC);
-- These should all use the index
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) < 4000 AND y > 4;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) = 2675 AND y IN (5, 6);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) < 4000 AND y IN (5, 6);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) = 2675 AND y > 4;

DROP INDEX t_hcx_asc_y_asc_idx;
CREATE INDEX t_hcx_hash_y_asc_idx ON t (yb_hash_code(x) HASH, y ASC);
-- Query #2 and #4 should use the index
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) < 4000 AND y > 4;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) = 2675 AND y IN (5, 6);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) < 4000 AND y IN (5, 6);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) = 2675 AND y > 4;

DROP INDEX t_hcx_hash_y_asc_idx;
CREATE INDEX t_y_hash_hcx_asc_idx ON t (y HASH, yb_hash_code(x) ASC);
-- Query #2 and #3 should use the index
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) < 4000 AND y > 4;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) = 2675 AND y IN (5, 6);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) < 4000 AND y IN (5, 6);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) = 2675 AND y > 4;

DROP INDEX t_y_hash_hcx_asc_idx;
CREATE INDEX t_cbrtx_hash_y_hash_idx ON t ((cbrt(x), y) HASH);
-- Query #2 should use the index
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(x), y FROM t WHERE cbrt(x) < 1.2 AND y < 2;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(x), y FROM t WHERE cbrt(x) = 1 AND y = 1;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(x), y FROM t WHERE cbrt(x) < 1.2 AND y = 1;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(x), y FROM t WHERE cbrt(x) = 1 AND y < 2;

DROP INDEX t_cbrtx_hash_y_hash_idx;
CREATE INDEX t_cbrtx_asc_y_asc_idx ON t (cbrt(x) ASC, y ASC);
-- These should all use the index
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(x), y FROM t WHERE cbrt(x) < 1.2 AND y < 2;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(x), y FROM t WHERE cbrt(x) = 1 AND y = 1;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(x), y FROM t WHERE cbrt(x) < 1.2 AND y = 1;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(x), y FROM t WHERE cbrt(x) = 1 AND y < 2;

DROP INDEX t_cbrtx_asc_y_asc_idx;
CREATE INDEX t_cbrtx_hash_y_asc_idx ON t (cbrt(x) HASH, y ASC);
-- Query #2 and #4 should use the index
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(x), y FROM t WHERE cbrt(x) < 1.2 AND y < 2;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(x), y FROM t WHERE cbrt(x) = 1 AND y = 1;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(x), y FROM t WHERE cbrt(x) < 1.2 AND y = 1;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(x), y FROM t WHERE cbrt(x) = 1 AND y < 2;

DROP INDEX t_cbrtx_hash_y_asc_idx;
CREATE INDEX t_y_hash_cbrtx_asc_idx ON t (y HASH, cbrt(x) ASC);
-- Query #2 and #3 should use the index
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(x), y FROM t WHERE cbrt(x) < 1.2 AND y < 2;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(x), y FROM t WHERE cbrt(x) = 1 AND y = 1;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(x), y FROM t WHERE cbrt(x) < 1.2 AND y = 1;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(x), y FROM t WHERE cbrt(x) = 1 AND y < 2;

DROP INDEX t_y_hash_cbrtx_asc_idx;
CREATE INDEX t_x_hash_y_hash_idx ON t ((x, y) HASH);
-- Query #2 should be able to use the index, but doesn't (even before this diff)
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) < 4000 AND y > 4;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) = 2675 AND y IN (5, 6);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) < 4000 AND y IN (5, 6);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) = 2675 AND y > 4;

DROP INDEX t_x_hash_y_hash_idx;
CREATE INDEX t_x_hash_y_asc_idx ON t (x HASH, y ASC);
-- All should use the index
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) < 4000 AND y > 4;
-- EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) = 2675 AND y IN (5, 6); -- TODO: #17043
-- EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) < 4000 AND y IN (5, 6); -- TODO: #17043
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) = 2675 AND y > 4;

DROP INDEX t_x_hash_y_asc_idx;
CREATE INDEX t_y_hash_x_asc_idx ON t (y HASH, x ASC);
-- Query #2 and #3 should use the index
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) < 4000 AND y > 4;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) = 2675 AND y IN (5, 6);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) < 4000 AND y IN (5, 6);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) = 2675 AND y > 4;

DROP INDEX t_y_hash_x_asc_idx;
CREATE INDEX t_x_asc_y_asc_idx ON t (x ASC, y ASC);
-- These should all use the index
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) < 4000 AND y > 4;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) = 2675 AND y IN (5, 6);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) < 4000 AND y IN (5, 6);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) = 2675 AND y > 4;

DROP INDEX t_x_asc_y_asc_idx;
CREATE INDEX t_x_hash_y_hashcode ON t (x HASH, yb_hash_code(y));
-- These should all be able to use the index, with queries #1-4 needing to filter to satisfy the y constraint
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) < 4000 AND y > 4;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) = 2675 AND y IN (5, 6);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) < 4000 AND y IN (5, 6);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) = 2675 AND y > 4;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), yb_hash_code(y) FROM t WHERE yb_hash_code(x) < 4000 AND yb_hash_code(y) > 4;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), yb_hash_code(y) FROM t WHERE yb_hash_code(x) = 2675 AND yb_hash_code(y) = 2675;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), yb_hash_code(y) FROM t WHERE yb_hash_code(x) < 4000 AND yb_hash_code(y) = 2675;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), yb_hash_code(y) FROM t WHERE yb_hash_code(x) = 2675 AND yb_hash_code(y) > 4;

DROP INDEX t_x_hash_y_hashcode;
CREATE INDEX t_x_hash_yz_hashcode ON t (x HASH, yb_hash_code(y, z));
-- These should all be able to use the index
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) < 4000;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) = 2675;
-- These should be all be able to use the index, with a filter to satisfy the y constraint
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) < 4000 AND y > 4;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) = 2675 AND y IN (5, 6);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) < 4000 AND y IN (5, 6);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) = 2675 AND y > 4;
-- These should all be able to use the index
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), yb_hash_code(y, z) FROM t WHERE yb_hash_code(x) > 40000 AND yb_hash_code(y, z) < 2500;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), yb_hash_code(y, z) FROM t WHERE yb_hash_code(x) = 40623 AND yb_hash_code(y, z) = 2181;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), yb_hash_code(y, z) FROM t WHERE yb_hash_code(x) > 40000 AND yb_hash_code(y, z) < 2500;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x), yb_hash_code(y, z) FROM t WHERE yb_hash_code(x) = 40623 AND yb_hash_code(y, z) = 2181;

DROP INDEX t_x_hash_yz_hashcode;
CREATE INDEX t_xy_yb_hash_code ON t (yb_hash_code(x, y));
-- Of the following, only the query with an equality clause on yb_hash_code(x, y) should be able to use the index
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x, x) FROM t WHERE yb_hash_code(x, x) = 2181;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x, x) FROM t WHERE yb_hash_code(x, x) < 2500;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x, y) FROM t WHERE yb_hash_code(x, y) = 2181;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x, y) FROM t WHERE yb_hash_code(x, y) < 2500;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(y, x) FROM t WHERE yb_hash_code(y, x) = 2181;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(y, x) FROM t WHERE yb_hash_code(y, x) < 2500;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(y, y) FROM t WHERE yb_hash_code(y, y) = 2181;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(y, y) FROM t WHERE yb_hash_code(y, y) < 2500;

DROP INDEX t_xy_yb_hash_code;
-- In general, a query on yb_hash_code(x) < 4000 could use either the (x HASH)
-- column of the index or the (yb_hash_code(x) ASC) part of the index.
-- We want to ensure that both are allowed, and the presence of one does not
-- forbid the use of the other. In this index, even though we have yb_hash_code(x|y) columns,
-- we won't use them because the index is ordered by (x HASH, y ASC) first.
CREATE INDEX t_complex_idx ON t (y HASH, y ASC, yb_hash_code(x) ASC, yb_hash_code(y) ASC);
-- These queries will not use the index because yb_hash_code(x) is too deep in the index
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x) FROM t WHERE yb_hash_code(x) < 4000;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(x) FROM t WHERE yb_hash_code(x) = 2675;
-- These queries will use the index because the (y HASH) column can answer the query.
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(y) FROM t WHERE yb_hash_code(y) < 4000;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(y) FROM t WHERE yb_hash_code(y) = 2675;

DROP TABLE t;
