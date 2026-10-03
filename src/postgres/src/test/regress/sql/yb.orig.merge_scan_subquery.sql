--
-- See yb_merge_scan_schedule for details about the test.  Tests merge scan on
-- an index scan planned below a nonzero range table offset -- a subquery, a
-- CTE, a set operation arm -- where the pinned SAOPs have to be moved onto the
-- flat range table along with the rest of the node.
--

\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/merge_scan_setup.sql'
\i :filename

-- What is under test is deparsing of the plan, not its execution, so drop
-- ANALYZE and DIST: merge sorting of the streams makes the storage counters
-- vary run to run, and the :P2 iteration still runs each query.
\set explain 'EXPLAIN (VERBOSE, COSTS OFF)'

SET yb_enable_derived_saops = true;

-- sq_tbl is of no use to other tests, so it is defined here rather than in
-- yb.orig.merge_scan_setup.  Its shape matters: the bucket column sits at
-- attnum 5, past the end of the narrow subqueries below, so a stream key Var
-- left at its subquery-local varno resolves to a column that does not exist.
CREATE TABLE sq_tbl (
    k int,
    a int,
    b int,
    v int,
    bkt int GENERATED ALWAYS AS (yb_hash_code(k) % 3) STORED,
    PRIMARY KEY (bkt ASC, a, b));
INSERT INTO sq_tbl (k, a, b, v) SELECT i, i, i, i FROM generate_series(1, 100) i;
CREATE INDEX NONCONCURRENTLY sq_tbl_bkt_v_idx ON sq_tbl (bkt ASC, v);
ANALYZE sq_tbl;

--
-- Subquery
--
\set query ':P :Q SELECT * FROM (SELECT a, b, k FROM sq_tbl ORDER BY a, b LIMIT 5) x;'
\i :run_query

--
-- CTE
--
\set query ':P :Q WITH x AS MATERIALIZED (SELECT a, b FROM sq_tbl ORDER BY a, b LIMIT 5) SELECT * FROM x;'
\i :run_query

--
-- Set operation arm
--
\set query ':P :Q SELECT * FROM (SELECT k, a FROM sq_tbl WHERE k = 1 UNION ALL (SELECT a, b FROM sq_tbl ORDER BY a, b LIMIT 5)) x;'
\i :run_query

--
-- Index only scan
--
-- Second hint is to encourage merge scan of the secondary index.
\set query ':P :Q SELECT * FROM (SELECT v FROM sq_tbl ORDER BY v LIMIT 5) x;'
\set Q2 '/*+IndexOnlyScan(sq_tbl sq_tbl_bkt_v_idx) Set(yb_max_merge_scan_streams 64)*/'
\i :run_query
\set Q2 ':on'

--
-- Bucket attnum within the subquery's column count
--
-- The stale varno then resolves rather than erroring, and the Merge Cond
-- silently names the subquery's fifth column instead of sq_tbl.bkt.
\set query ':P :Q SELECT * FROM (SELECT a AS s1, b AS s2, k AS s3, v AS s4, k AS s5 FROM sq_tbl ORDER BY a, b LIMIT 5) x;'
\i :run_query

-- (Drop the table)
DROP TABLE sq_tbl;
