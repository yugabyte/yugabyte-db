CREATE TABLE test_with_pk (a INT PRIMARY KEY, h INT);
INSERT INTO test_with_pk SELECT x, x FROM generate_series(1, 10) x;

-- This test verifies:
--  * yb_hash_code range queries
--  * yb_hash_code equality queries
--  * cbrt range queries
--  * cbrt equality queries
-- on the following cases
--  * on the primary key
--  * on a regular column
--  * on a regular column that has a hash index
--  * on a regular column that has an asc index
--  * on a regular column that has a hash index on yb_hash_code and a hash
--    index on cbrt
--  * on a regular column that has an asc index on yb_hash_code and an asc
--    index on cbrt
-- cbrt was selected as an example of a simple, standard function, not because
-- of any special attributes. For each of these 4 * 6 cases, they are tested
-- with seqscans disabled and with seqscans enabled. This documents their
-- preferred plan, and also ensures that if the query planner allows them to
-- use an index, the result is still correct.

---- PRIMARY KEY ----
SET enable_seqscan = false;

-- we can pushdown the yb_hash_code call to the pk index
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(a) FROM test_with_pk WHERE yb_hash_code(a) < 4000;
SELECT yb_hash_code(a) FROM test_with_pk WHERE yb_hash_code(a) < 4000;

-- we can pushdown the yb_hash_code call to the pk index
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(a) FROM test_with_pk WHERE yb_hash_code(a) = 2675;
SELECT yb_hash_code(a) FROM test_with_pk WHERE yb_hash_code(a) = 2675;

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(a) FROM test_with_pk WHERE cbrt(a) < 1.2;
SELECT cbrt(a) FROM test_with_pk WHERE cbrt(a) < 1.2;

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(a) FROM test_with_pk WHERE cbrt(a) = 1;
SELECT cbrt(a) FROM test_with_pk WHERE cbrt(a) = 1;

SET enable_seqscan = true;

-- we can pushdown the yb_hash_code call to the pk index
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(a) FROM test_with_pk WHERE yb_hash_code(a) < 4000;
SELECT yb_hash_code(a) FROM test_with_pk WHERE yb_hash_code(a) < 4000;

-- we can pushdown the yb_hash_code call to the pk index
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(a) FROM test_with_pk WHERE yb_hash_code(a) = 2675;
SELECT yb_hash_code(a) FROM test_with_pk WHERE yb_hash_code(a) = 2675;

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(a) FROM test_with_pk WHERE cbrt(a) < 1.2;
SELECT cbrt(a) FROM test_with_pk WHERE cbrt(a) < 1.2;

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(a) FROM test_with_pk WHERE cbrt(a) = 1;
SELECT cbrt(a) FROM test_with_pk WHERE cbrt(a) = 1;

---- REGULAR COLUMN ----
SET enable_seqscan = false;
-- for these queries, we use seqscan (even when disabled)
-- because there is no other option

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) < 4000;
SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) < 4000;

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) = 2675;
SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) = 2675;

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) < 1.2;
SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) < 1.2;

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) = 1;
SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) = 1;

---- HASH INDEX ON THE COLUMN ----
CREATE INDEX t_b_hash_idx ON test_with_pk(h);
SET enable_seqscan = false;

-- we can pushdown yb_hash_code(h) on a hash index on h because the index is ordered by hash code
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) < 4000;
SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) < 4000;

-- we can pushdown yb_hash_code(h) on a hash index on h because the index is ordered by hash code
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) = 2675;
SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) = 2675;

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) < 1.2;
SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) < 1.2;

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) = 1;
SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) = 1;

SET enable_seqscan = true;

-- we prefer using the index over a seq scan
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) < 4000;
SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) < 4000;

-- we prefer using the index over a seq scan
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) = 2675;
SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) = 2675;

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) < 1.2;
SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) < 1.2;

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) = 1;
SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) = 1;

---- ASC INDEX ON THE COLUMN ----
DROP INDEX t_b_hash_idx;
CREATE INDEX t_b_asc_idx ON test_with_pk(h ASC);

SET enable_seqscan = false;

-- cannot pushdown a range yb_hash_code(h) clause on a ascending index on h because the index is not ordered by hash code
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) < 4000;
SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) < 4000;

-- cannot pushdown an equality yb_hash_code(h) clause on a ascending index on h because the index is not ordered by hash code
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) = 2675;
SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) = 2675;

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) < 1.2;
SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) < 1.2;

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) = 1;
SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) = 1;

SET enable_seqscan = true;

-- we prefer filtering rows from the seq scan instead of the index scan
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) < 4000;
SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) < 4000;

-- we prefer filtering rows from the seq scan instead of the index scan
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) = 2675;
SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) = 2675;

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) < 1.2;
SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) < 1.2;

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) = 1;
SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) = 1;

---- HASH INDEX ON yb_hash_code(h), HASH INDEX ON cbrt(h) ----
DROP INDEX t_b_asc_idx;
CREATE INDEX t_b_hash_code_idx ON test_with_pk(yb_hash_code(h));
CREATE INDEX t_b_cbrt_idx ON test_with_pk(cbrt(h));

SET enable_seqscan = false;

-- cannot use a hash index for a range clause on yb_hash_code(h) because it's ordered by yb_hash_code(yb_hash_code(h))
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) < 4000;
SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) < 4000;

-- can use a hash index on yb_hash_code for a yb_hash_code equality clause
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) = 2675;
SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) = 2675;

-- cannot use a hash index on cbrt for a cbrt range clause
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) < 1.2;
SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) < 1.2;

-- can use a hash index on cbrt for a cbrt equality clause
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) = 1;
SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) = 1;

SET enable_seqscan = true;

-- cannot use a hash index for a range clause on yb_hash_code(h) because it's ordered by yb_hash_code(yb_hash_code(h))
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) < 4000;
SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) < 4000;

-- can use a hash index on yb_hash_code for a yb_hash_code equality clause
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) = 2675;
SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) = 2675;

-- cannot use a hash index on cbrt for a cbrt range clause
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) < 1.2;
SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) < 1.2;

-- can use a hash index on cbrt for a cbrt equality clause
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) = 1;
SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) = 1;

---- ASC INDEX ON yb_hash_code(h), ASC INDEX on cbrt(h) ----
DROP INDEX t_b_hash_code_idx;
DROP INDEX t_b_cbrt_idx;
CREATE INDEX t_b_hash_code_asc_idx ON test_with_pk(yb_hash_code(h) ASC);
CREATE INDEX t_b_cbrt_asc_idx ON test_with_pk(cbrt(h) ASC);

SET enable_seqscan = false;

-- can use the ascending index on yb_hash_code for an equality clause
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) < 4000;
SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) < 4000;

-- can use the ascending index on yb_hash_code for an equality clause
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) = 2675;
SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) = 2675;

-- can use the ascending index on cbrt for an equality clause
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) < 1.2;
SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) < 1.2;

-- can use the ascending index on cbrt for an equality clause
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) = 1;
SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) = 1;

SET enable_seqscan = true;
-- for each case, we prefer to use the indexes instead of a seq scan

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) < 4000;
SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) < 4000;

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) = 2675;
SELECT yb_hash_code(h) FROM test_with_pk WHERE yb_hash_code(h) = 2675;

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) < 1.2;
SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) < 1.2;

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) = 1;
SELECT cbrt(h) FROM test_with_pk WHERE cbrt(h) = 1;
