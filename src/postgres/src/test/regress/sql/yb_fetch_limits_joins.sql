-- #### nested loop joins ####
-- disable printing of all non-deterministic fields in EXPLAIN
SET yb_explain_hide_non_deterministic_fields = true;

SET yb_fetch_size_limit = 0;
SET yb_fetch_row_limit = 1024;
-- the default behaviour sends 6 RPCs for the outer table
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF) SELECT * FROM t_large JOIN t_small ON t_large.a = t_small.a;
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF) SELECT * FROM t_small JOIN t_large ON t_large.a = t_small.a;

SET yb_fetch_size_limit = '50kB';
SET yb_fetch_row_limit = 10000;
-- we require more requests to collect the rows of the outer table when it is large.
-- the inner table is unchanged at 1 RPC per row.
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF) SELECT * FROM t_large JOIN t_small ON t_large.a = t_small.a;
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF) SELECT * FROM t_small JOIN t_large ON t_large.a = t_small.a;

-- #### batch nested loop joins ####
SET yb_bnl_batch_size = 1024;
-- now we can request large amounts of rows from the inner table - these are still bound by the size limit
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF) SELECT * FROM t_large JOIN t_small ON t_large.a = t_small.a;
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF) SELECT * FROM t_small JOIN t_large ON t_large.a = t_small.a;

SET yb_fetch_size_limit = '500kB';
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF) SELECT * FROM t_large JOIN t_small ON t_large.a = t_small.a;
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF) SELECT * FROM t_small JOIN t_large ON t_large.a = t_small.a;

SET yb_bnl_batch_size = 1; -- disable BNLJ

-- #### merge joins ####
SET yb_fetch_row_limit = 1024;
SET yb_fetch_size_limit = 0;

-- the default behaviour sends 6 RPCs per table
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF) /*+ MergeJoin(t_small t_large) */ SELECT * FROM t_large JOIN t_small ON t_large.a = t_small.a;

SET yb_fetch_row_limit = 0;
-- size limit affects both tables in a Merge Join
SET yb_fetch_size_limit = '500kB';
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF) /*+ MergeJoin(t_small t_large) */ SELECT * FROM t_large JOIN t_small ON t_large.a = t_small.a;
SET yb_fetch_size_limit = '50kB';
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF) /*+ MergeJoin(t_small t_large) */ SELECT * FROM t_large JOIN t_small ON t_large.a = t_small.a;

-- #### hash joins ####
SET yb_fetch_row_limit = 1024;
SET yb_fetch_size_limit = 0;
-- the default behaviour sends 6 RPCs per table
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF) /*+ HashJoin(t_small t_large) */ SELECT * FROM t_small JOIN t_large ON t_large.a = t_small.a;
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF) /*+ HashJoin(t_small t_large) */ SELECT * FROM t_large JOIN t_small ON t_large.a = t_small.a;

SET yb_fetch_row_limit = 0;
-- size limit affects both tables in a HashJoin
SET yb_fetch_size_limit = '500kB';
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF) /*+ HashJoin(t_small t_large) */ SELECT * FROM t_small JOIN t_large ON t_large.a = t_small.a;
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF) /*+ HashJoin(t_small t_large) */ SELECT * FROM t_large JOIN t_small ON t_large.a = t_small.a;
SET yb_fetch_size_limit = '50kB';
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF) /*+ HashJoin(t_small t_large) */ SELECT * FROM t_small JOIN t_large ON t_large.a = t_small.a;
EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF) /*+ HashJoin(t_small t_large) */ SELECT * FROM t_large JOIN t_small ON t_large.a = t_small.a;
