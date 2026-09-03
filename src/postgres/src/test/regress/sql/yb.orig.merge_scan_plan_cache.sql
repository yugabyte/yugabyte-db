--
-- See yb_merge_scan_schedule for details about the test.  Tests that a merge
-- scan survives plan caching (#32733).
--

\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/parameterized_query.sql'
\i :filename

\set explain 'EXPLAIN (COSTS OFF)'

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

\set stmt 'SELECT val, k, bkt4 FROM pc_tbl WHERE bkt4 IN (0, 1, 2, 3) ORDER BY val, k;'
PREPARE p_iscan AS :stmt
\set P1 ':explain'
\set P2
\set Q1 :stmt
\set Q2 'EXECUTE p_iscan;'
\set query ':P :Q'
\i :run_query
DEALLOCATE p_iscan;

--
-- Index Only Scan
--

\set stmt 'SELECT val, k, bkt3 FROM pc_tbl WHERE bkt3 IN (0, 1, 2) ORDER BY val, k;'
PREPARE p_ioscan AS :stmt
\set Q1 :stmt
\set Q2 'EXECUTE p_ioscan;'
\i :run_query
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
\set Q1 'EXECUTE p_param(100);'
\set Q2 'EXECUTE p_param(100);'
\set Q3 'EXECUTE p_param(7);'
\i :run_query
\unset Q3
DEALLOCATE p_param;
RESET plan_cache_mode;

RESET yb_max_merge_scan_streams;
RESET enable_sort;

-- (Drop the table)
DROP TABLE pc_tbl;
