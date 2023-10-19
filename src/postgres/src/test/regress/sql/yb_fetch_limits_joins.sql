-- #### nested loop joins ####
CREATE FUNCTION explain_join_tsmall_tlarge() RETURNS SETOF TEXT LANGUAGE plpgsql AS
$$
DECLARE ln TEXT;
BEGIN
    FOR ln IN
        EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF)
        SELECT * FROM t_large JOIN t_small ON t_large.a = t_small.a
    LOOP
        ln := regexp_replace(ln, 'Memory: \S*',  'Memory: xxx');
        ln := regexp_replace(ln, 'Disk: \S*',  'Disk: xxx');
        RETURN NEXT ln;
    END LOOP;
END;
$$;
CREATE FUNCTION explain_join_tlarge_tsmall() RETURNS SETOF TEXT LANGUAGE plpgsql AS
$$
DECLARE ln TEXT;
BEGIN
    FOR ln IN
        EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF)
        SELECT * FROM t_small JOIN t_large ON t_large.a = t_small.a
    LOOP
        ln := regexp_replace(ln, 'Memory: \S*',  'Memory: xxx');
        ln := regexp_replace(ln, 'Disk: \S*',  'Disk: xxx');
        RETURN NEXT ln;
    END LOOP;
END;
$$;

SET yb_fetch_size_limit = 0;
SET yb_fetch_row_limit = 1024;
-- the default behaviour sends 6 RPCs for the outer table
SELECT explain_join_tsmall_tlarge();
SELECT explain_join_tlarge_tsmall();

SET yb_fetch_size_limit = '50kB';
SET yb_fetch_row_limit = 10000;
-- we require more requests to collect the rows of the outer table when it is large.
-- the inner table is unchanged at 1 RPC per row.
SELECT explain_join_tsmall_tlarge();
SELECT explain_join_tlarge_tsmall();

-- #### batch nested loop joins ####
SET yb_bnl_batch_size = 1024;
-- now we can request large amounts of rows from the inner table - these are still bound by the size limit
-- YB_TODO: EXPLAIN Index Cond is not showing: Index Cond: (a = ANY (ARRAY[t_large.a, $1, $2, ..., $1023]))
SELECT explain_join_tsmall_tlarge();
-- YB_TODO: EXPLAIN Index Cond is not showing: Index Cond: (a = ANY (ARRAY[t_small.a, $1, $2, ..., $1023]))
SELECT explain_join_tlarge_tsmall();

SET yb_fetch_size_limit = '500kB';
-- YB_TODO: EXPLAIN Index Cond is not showing: Index Cond: (a = ANY (ARRAY[t_large.a, $1, $2, ..., $1023]))
SELECT explain_join_tsmall_tlarge();
-- YB_TODO: EXPLAIN Index Cond is not showing: Index Cond: (a = ANY (ARRAY[t_small.a, $1, $2, ..., $1023]))
SELECT explain_join_tlarge_tsmall();

-- YB_TODO: tests here onwards fail because proper join is not successfully chosen
SET yb_bnl_batch_size = 1; -- disable BNLJ

-- #### merge joins ####
SET yb_fetch_row_limit = 1024;
SET yb_fetch_size_limit = 0;

-- the default behaviour sends 6 RPCs per table
SELECT /*+ MergeJoin(t_small t_large) */ explain_join_tsmall_tlarge();

SET yb_fetch_row_limit = 0;
-- size limit affects both tables in a Merge Join
SET yb_fetch_size_limit = '500kB';
SELECT /*+ MergeJoin(t_small t_large) */ explain_join_tsmall_tlarge();
SET yb_fetch_size_limit = '50kB';
SELECT /*+ MergeJoin(t_small t_large) */ explain_join_tsmall_tlarge();

-- #### hash joins ####
SET yb_fetch_row_limit = 1024;
SET yb_fetch_size_limit = 0;
-- the default behaviour sends 6 RPCs per table
SELECT /*+ HashJoin(t_small t_large) */ explain_join_tlarge_tsmall();
SELECT /*+ HashJoin(t_small t_large) */ explain_join_tsmall_tlarge();

SET yb_fetch_row_limit = 0;
-- size limit affects both tables in a HashJoin
SET yb_fetch_size_limit = '500kB';
SELECT /*+ HashJoin(t_small t_large) */ explain_join_tlarge_tsmall();
SELECT /*+ HashJoin(t_small t_large) */ explain_join_tsmall_tlarge();
SET yb_fetch_size_limit = '50kB';
SELECT /*+ HashJoin(t_small t_large) */ explain_join_tlarge_tsmall();
SELECT /*+ HashJoin(t_small t_large) */ explain_join_tsmall_tlarge();
