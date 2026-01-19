--
-- Test YB's automatic insertion of SAOP cond.
--

\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/explainrun_saop_merge.sql'
\i :filename

\set off '/*+Set(enable_parallel_append off) Set(yb_max_saop_merge_streams 0)*/'
\set on '/*+Set(enable_parallel_append off) Set(yb_max_saop_merge_streams 64)*/'

SET yb_enable_derived_saops = true;

--
-- Derive from generated column
--

-- No order
-- SAOP merge should not be used.
\set query 'SELECT * FROM bkt_tbl'
:explain2

-- No limit
\set query 'SELECT * FROM bkt_tbl ORDER BY r1, r2, r3'
:explain2

-- Forward scan
\set query 'SELECT * FROM bkt_tbl ORDER BY r1, r2, r3, n LIMIT 5'
:explain2run2

-- Backward scan
\set query 'SELECT * FROM bkt_tbl ORDER BY r1 DESC, r2 DESC, r3 DESC, n LIMIT 5'
:explain2run2

-- DISTINCT
\set query 'SELECT DISTINCT r1, r2, r3 FROM bkt_tbl ORDER BY r1, r2, r3 LIMIT 5'
:explain2run2

-- GROUP BY
\set query 'SELECT COUNT(*), r1, r2, r3 FROM bkt_tbl GROUP BY r1, r2, r3 ORDER BY r1, r2, r3 LIMIT 5'
:explain2run2

-- Explicit SAOP smaller than derived SAOP
\set query 'SELECT r1, r2, r3, n, bkt FROM bkt_tbl WHERE bkt IN (1, 2) ORDER BY r1, r2, r3, n LIMIT 5'
:explain2run2

-- Explicit SAOP larger than derived SAOP
\set query 'SELECT r1, r2, r3, n, bkt FROM bkt_tbl WHERE bkt IN (1, 2, 3, 4) ORDER BY r1, r2, r3, n LIMIT 5'
:explain2run2

CREATE INDEX NONCONCURRENTLY ON bkt_tbl (bkt ASC, r3, r2, r1)
SPLIT AT VALUES (
    (1),
    (2),
    (2, 2),
    (2, 2, 2),
    (2, 2, 2, 2),
    (3));

-- No limit
-- TODO(#29078): this likely should use SAOP merge.
\set query 'SELECT * FROM bkt_tbl ORDER BY r3, r2, r1'
:explain2

-- Forward scan
\set query 'SELECT * FROM bkt_tbl ORDER BY r3, r2, r1, n LIMIT 5'
:explain2run2

-- Backward scan
\set query 'SELECT * FROM bkt_tbl ORDER BY r3 DESC, r2 DESC, r1 DESC, n LIMIT 5'
:explain2run2

--
-- Derive from secondary index expression, expression being a prefix
--
CREATE INDEX NONCONCURRENTLY ON r5n ((yb_hash_code(r2, r3, r4) % 3) ASC, r2, r3, r4)
SPLIT AT VALUES (
    (1),
    (2),
    (2, 2),
    (2, 2, 2),
    (2, 2, 2, 2),
    (3));

-- No limit
-- TODO(#29078): this likely should use SAOP merge.
\set query 'SELECT * FROM r5n ORDER BY r2, r3'
:explain2

-- Forward scan
\set query 'SELECT * FROM r5n ORDER BY r2, r3, n LIMIT 5'
:explain2run2

-- Explicit SAOP smaller than derived SAOP
\set query 'SELECT r2, r3, n, yb_hash_code(r2, r3, r4) % 3 FROM r5n WHERE yb_hash_code(r2, r3, r4) % 3 in (0, 2) ORDER BY r2, r3, n LIMIT 5'
:explain2run2

-- Explicit SAOP larger than derived SAOP
\set query 'SELECT r2, r3, n, yb_hash_code(r2, r3, r4) % 3 FROM r5n WHERE yb_hash_code(r2, r3, r4) % 3 in (0, 2, 4, 8) ORDER BY r2, r3, n LIMIT 5'
:explain2run2

-- Expression bound to constant
-- SAOP merge should not be used.
\set query 'SELECT yb_hash_code(r2, r3, r4) % 3, r2, r3, n FROM r5n WHERE yb_hash_code(r2, r3, r4) % 3 = 1 ORDER BY r2, r3, n LIMIT 5'
:explain2

-- Range filter; no ORDER BY
-- SAOP merge should not be used.
\set query 'SELECT * FROM r5n WHERE r2 > 1 LIMIT 5'
:explain2

-- (Drop this index)
DROP INDEX r5n_expr_r2_r3_r4_idx;

--
-- Derive from secondary index expression, expression not being a prefix,
-- modulus being negative.
--
CREATE INDEX NONCONCURRENTLY ON r5n (r1 ASC, (yb_hash_code(r1, r3, r4) % -5) ASC, r3, r4)
SPLIT AT VALUES (
    (1),
    (2),
    (2, 2),
    (2, 2, 2),
    (2, 2, 2, 2),
    (3));

-- Expression in sort, so not derived
-- SAOP merge should not be used.
\set query 'SELECT r1, yb_hash_code(r1, r3, r4) % -5, r3, n FROM r5n WHERE r1 = 1 ORDER BY yb_hash_code(r1, r3, r4) % -5, r3, n LIMIT 5'
:explain2

-- Expression in sort, so not derived (v2)
\set query 'SELECT yb_hash_code(r1, r3, r4) % -5, r3, n, r1 FROM r5n WHERE r1 in (0, 1, 2) ORDER BY yb_hash_code(r1, r3, r4) % -5, r3, n LIMIT 5'
:explain2run2

-- Derived
-- Third hint is to use the expression index.
\set query 'SELECT r1, r3, r4, n FROM r5n WHERE r1 = 1 ORDER BY r3, r4, n LIMIT 5'
\set hint3 '/*+IndexScan(r5n r5n_r1_expr_r3_r4_idx) Set(enable_parallel_append off) Set(yb_max_saop_merge_streams 64)*/'
:explain3run3

-- Following queries send various numbers of requests/scan various number of
-- rows due to non-deterministic order of equal rows in merge sort.  Hide their
-- DIST.
\set explain 'EXPLAIN (ANALYZE, VERBOSE, COSTS OFF, SUMMARY OFF, TIMING OFF)'

-- Derived with multiple stream keys
\set query 'SELECT r3, r4, n, r1 FROM r5n WHERE r1 IN (0, 1, 2) ORDER BY r3, r4, n LIMIT 5'
:explain2run2

-- (Reset the explain change)
\set explain 'EXPLAIN (ANALYZE, DIST, VERBOSE, COSTS OFF, SUMMARY OFF, TIMING OFF)'

-- (Drop this index)
DROP INDEX r5n_r1_expr_r3_r4_idx;

--
-- Derive from partitioned secondary index expression
--
CREATE INDEX NONCONCURRENTLY ON parent ((yb_hash_code(r2, r3) % 3) ASC, r2, r3)
SPLIT AT VALUES (
    (1),
    (2),
    (2, 2),
    (2, 2, 2),
    (3));

-- Parent
\set query 'SELECT r2, r3, n FROM parent ORDER BY r2, r3, n LIMIT 5'
:explain2run2

-- Child
\set query 'SELECT r2, r3, n FROM child1 ORDER BY r2, r3, n LIMIT 5'
:explain2run2

-- Grandchild
\set query 'SELECT r2, r3, n FROM child1b ORDER BY r2, r3, n LIMIT 5'
:explain2run2

-- (Drop this index)
DROP INDEX parent_expr_r2_r3_idx;

--
-- Joins
--
CREATE INDEX NONCONCURRENTLY ON r5n ((yb_hash_code(r2, r3, r4) % 3) ASC, r2, r3, r4)
SPLIT AT VALUES (
    (1),
    (2),
    (2, 2),
    (2, 2, 2),
    (2, 2, 2, 2),
    (3));
CREATE INDEX NONCONCURRENTLY ON parent ((yb_hash_code(r2, r3) % 3) ASC, r2, r3)
SPLIT AT VALUES (
    (1),
    (2),
    (2, 2),
    (2, 2, 2),
    (3));

-- Derive from r5n
\set query 'SELECT r5n.r2, r5n.r3, parent.r3, r5n.n, parent.n FROM parent JOIN r5n ON r5n.r3 = parent.r3 ORDER BY r5n.r2 LIMIT 5'
:explain2run2

-- Derive from parent
\set query 'SELECT parent.r2, r5n.r3, parent.r3, r5n.n, parent.n FROM parent JOIN r5n ON r5n.r3 = parent.r3 ORDER BY parent.r2 LIMIT 5'
:explain2run2

-- 2-hop =-const equivalence
-- Third hint is to show that when a JOIN doesn't care about the ordering of
-- the index scan, SAOP merge is not used.
-- Fourth hint is to show SAOP merge in a merge join.
-- Fifth hint is to show SAOP merge in the inner side of a merge join.
-- TODO(#29030): fifth hint should use SAOP merge.
\set query 'SELECT DISTINCT ON (r5n.r3, r5n.r4) parent.p1, r5n.r2, r5n.r3, parent.r3, r5n.n, parent.n FROM r5n JOIN parent ON r5n.r3 = parent.r3 WHERE parent.p1 = 9 AND r5n.r2 IN (3, 4, 5) LIMIT 5'
\set hint3 '/*+Leading((parent r5n)) Set(enable_parallel_append off) Set(yb_max_saop_merge_streams 64)*/'
\set hint4 '/*+MergeJoin(parent r5n) Set(enable_parallel_append off) Set(yb_max_saop_merge_streams 64)*/'
\set hint5 '/*+MergeJoin(parent r5n) Leading((parent r5n)) Set(enable_parallel_append off) Set(yb_max_saop_merge_streams 64)*/'
:explain5
