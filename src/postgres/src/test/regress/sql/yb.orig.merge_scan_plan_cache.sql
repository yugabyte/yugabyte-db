--
-- See yb_merge_scan_schedule for details about the test.  Tests that a merge
-- scan survives plan caching (#32733).
--

-- This table is of no use to other tests, so it is defined here rather than in
-- yb.orig.merge_scan_setup.  bkt4 and bkt3 bucket the rows into four and three
-- streams, and each is the leading key of a different index, so the two
-- queries below reach a merge scan through the primary key (Index Scan) and
-- through a secondary index (Index Only Scan) without needing a hint to pick
-- the index.  val is the sort column, so one stream's rows are contiguous in
-- index order and a lost merge shows up as rows grouped by bucket.
CREATE TABLE pc_tbl (
    k int,
    val int,
    bkt4 int GENERATED ALWAYS AS (k % 4) STORED,
    bkt3 int GENERATED ALWAYS AS (k % 3) STORED,
    PRIMARY KEY (bkt4 ASC, val ASC, k ASC));
-- val cycles at 5, the buckets at 4 and 3, so every bucket holds every val and
-- the streams interleave throughout the merge.
INSERT INTO pc_tbl (k, val) SELECT i, i % 5 FROM generate_series(1, 24) i;
CREATE INDEX NONCONCURRENTLY pc_tbl_bkt3_val_idx ON pc_tbl (bkt3 ASC, val ASC, k ASC);
ANALYZE pc_tbl;

SET enable_sort = off;
SET yb_max_merge_scan_streams = 64;

--
-- Index Scan
--

PREPARE p_iscan AS
    SELECT val, k, bkt4 FROM pc_tbl WHERE bkt4 IN (0, 1, 2, 3) ORDER BY val, k;
EXPLAIN (COSTS OFF)
SELECT val, k, bkt4 FROM pc_tbl WHERE bkt4 IN (0, 1, 2, 3) ORDER BY val, k;
EXPLAIN (COSTS OFF) EXECUTE p_iscan;
SELECT val, k, bkt4 FROM pc_tbl WHERE bkt4 IN (0, 1, 2, 3) ORDER BY val, k;
EXECUTE p_iscan;
DEALLOCATE p_iscan;

--
-- Index Only Scan
--

PREPARE p_ioscan AS
    SELECT val, k, bkt3 FROM pc_tbl WHERE bkt3 IN (0, 1, 2) ORDER BY val, k;
EXPLAIN (COSTS OFF)
SELECT val, k, bkt3 FROM pc_tbl WHERE bkt3 IN (0, 1, 2) ORDER BY val, k;
EXPLAIN (COSTS OFF) EXECUTE p_ioscan;
SELECT val, k, bkt3 FROM pc_tbl WHERE bkt3 IN (0, 1, 2) ORDER BY val, k;
EXECUTE p_ioscan;
DEALLOCATE p_ioscan;

--
-- Bound parameter
--
-- The cases above are parameterless, so they take the generic plan whatever
-- plan_cache_mode says.  This one has a parameter, so it is the shape where
-- plan_cache_mode decides, and the one that reuses a cached generic plan
-- across executions with different bindings.  k <> 100 excludes nothing, so
-- the first two executions match and the third drops exactly one row.
--

PREPARE p_param(int) AS
    SELECT val, k, bkt4 FROM pc_tbl
    WHERE bkt4 IN (0, 1, 2, 3) AND k <> $1 ORDER BY val, k;

SET plan_cache_mode = force_generic_plan;
EXPLAIN (COSTS OFF) EXECUTE p_param(100);
EXPLAIN (COSTS OFF) EXECUTE p_param(100);
EXPLAIN (COSTS OFF) EXECUTE p_param(7);
EXECUTE p_param(100);
EXECUTE p_param(100);
EXECUTE p_param(7);
DEALLOCATE p_param;
RESET plan_cache_mode;

--
-- Pinned SAOP, cached plan (#33965)
--
-- Previously in a cached plan, the executor bound a different SAOP than the
-- one the planner picked.  This test checks that the executor binds the SAOP
-- that the planner pinned.
--
-- When a column has multiple filters, the executor uses a priority algorithm
-- to pick the one to bind, and merge scan overrides that with the SAOP the
-- planner pinned.  Master reaches this shape only with
-- yb_enable_advanced_index_cond_fold off, because folding first merges the two
-- INs into one.  This branch has no condition folding, so no SET is needed.
--
-- The order of the two INs in the query matters and must not be changed.  When
-- the executor cannot find the SAOP the planner picked, it binds the filter
-- listed last.  The wider array is listed last so that this differs from the
-- planner's choice.  Reversed, the two would agree by accident and the test
-- would pass either way.
--
-- The storage counters are the assertion.  Binding the wider array scans all
-- 24 rows and rechecks 12 of them away, so the prepared run would no longer
-- collapse onto the unprepared one.
--

PREPARE p_pinned AS
    SELECT val, k FROM pc_tbl
    WHERE bkt4 IN (0, 1) AND bkt4 IN (0, 1, 2, 3) ORDER BY val, k;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
SELECT val, k FROM pc_tbl
WHERE bkt4 IN (0, 1) AND bkt4 IN (0, 1, 2, 3) ORDER BY val, k;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) EXECUTE p_pinned;
SELECT val, k FROM pc_tbl
WHERE bkt4 IN (0, 1) AND bkt4 IN (0, 1, 2, 3) ORDER BY val, k;
EXECUTE p_pinned;
DEALLOCATE p_pinned;

RESET yb_max_merge_scan_streams;
RESET enable_sort;

-- (Drop the table)
DROP TABLE pc_tbl;
