-- Split at 1, ... to ensure that the value r1 = 1 is present in more than one tablet.
-- See #18101.
CREATE TABLE t(r1 INT, r2 INT, r3 INT, r4 INT, v INT, PRIMARY KEY(r1 ASC, r2 ASC, r3 ASC, r4 ASC)) SPLIT AT VALUES ((1, 1, 1, 500));
INSERT INTO t (SELECT 1, i%3, 2-i%3, i, i/3 FROM GENERATE_SERIES(1, 1000) AS i);
-- Add one more distinct value to catch bugs that arise only with more than one distinct value.
INSERT INTO t (SELECT 2, i%3, 2-i%3, i, i/3 FROM GENERATE_SERIES(1, 1000) AS i);

-- These tests illustrate some similarities and differences uniqkeys have with sortkeys.
--
-- Many of the differences arise from the fact that DISTINCT eliminates rows while
-- ORDER BY does not do that by itself. This fact has an important consequence that
-- sorting commutes with filters while uniq does not necessarily do that.
--
-- Differences
-- ===========
-- 1) uniqkeys are still useful even if only a subset of the index prefix is queried.
--    However, ORDER BY requires the complete prefix of the index to be requested
--    a partial sort is not useful for further sorting.
--    On the other hand, a partial DISTINCT helps eliminate irrelevant rows early on.
-- 2) Equivalence keys have limited utility with uniqkeys.
--    The most obvious example is when r1 = r2. Do not exclude r2 from the distinct prefix
--    since distinct values of r1 may not retrieve correct values of r2 from LSM.
--    This is because the scan module picks the first DISTINCT r1 row and not
--    the ones where r1 = r2.
--    While this behavior may change in the future to account for non-index predicates,
--    it still cannot rule out local predicates on the postgres side.
--    On the other hand, ORDER BY r1, r2 can be reduced to ORDER BY r1 irrespective
--    of additional predicates when r1 = r2.
--    All that said, r1 = r2 AND r2 = 1 is equivalent to r1 = 1 AND r2 = 1.
-- 3) Another major difference is in the handling of constants. Constants can be completely
--    eliminated in ORDER BY. For example, r2 = 1 ORDER BY r1, r2 is equivalent to ORDER BY r1
--    since there at most one distinct value of r2, so no explicit sorting is necessary.
--    However, the table might have multiple tuples with r2 = 1 and distinct values of r1.
--    Hence, a unique node is still necessary even when r2 is excluded from the prefix.
-- 4) Moreover, it is neither necessary nor sufficient for r2 to be equivalent to constant.
--    Instead, the scan requires that r2 = 1 be an index clause since otherwise the corresponding
--    tuple may be missed by nature of the distinct index scan. One example is when an index scan occurs
--    as part of the inner relation of a nested loop join operation.
--    More generally, a distinct index scan can be used even with Merge and Hash Joins
--    when the inner relation tuples must be unique.
-- 5) As another example, a DISTINCT on a non key column cannot be eliminated despite being
--    equal to a constant, e.g. v = 1, since currently only index clauses seep past
--    the DISTINCT operation.
-- 6) Distinct Index Scan may also be generated in the presence of simple non-volatile targets
--    and clauses. This is unlike sort where volatile clauses do not affect sort order.
--    However, the scan must generate distinct values of all target and clause references
--    (bar several exceptions).
-- 7) Adding onto comment (2), when r1 = r2, while DocDB cannot use a prefix of length 1,
--    postgres itself can treat r1 and r2 as equivalent. So query DISTINCT r1 is the same as
--    DISTINCT r2.

-- Do not eliminate r2, see comment (2) above.
-- However, since r1 is equivalent to r2, the query still requests a distinct prefix regardless, see comment (7).
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r2 FROM t WHERE r1 = r2;
SELECT DISTINCT r2 FROM t WHERE r1 = r2;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r1, r2 FROM t WHERE r1 = r2;
SELECT DISTINCT r1, r2 FROM t WHERE r1 = r2;

-- Eliminate r3 when equal to a constant, see comment (3) above.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r3 FROM t WHERE r3 = 1;
SELECT DISTINCT r3 FROM t WHERE r3 = 1;
-- Out-of-range predicate.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r3 FROM t WHERE r3 = 5;
SELECT DISTINCT r3 FROM t WHERE r3 = 5;
-- Other targets.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r2, r3 FROM t WHERE r3 = 1;
SELECT DISTINCT r2, r3 FROM t WHERE r3 = 1;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r2, r3 FROM t WHERE r3 = 5;
SELECT DISTINCT r2, r3 FROM t WHERE r3 = 5;

-- Can infer that r2 = r3, r3 = 1 <=> r2 = 1, r3 = 1.
-- Thus eliminating both r2 and r3.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r2, r3 FROM t WHERE r2 = r3 AND r3 = 1;
SELECT DISTINCT r2, r3 FROM t WHERE r2 = r3 AND r3 = 1;

-- Cannot eliminate a non-index key that is equal to a constant, see comment (5) above.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT v FROM t WHERE v = 1;
SELECT DISTINCT v FROM t WHERE v = 1;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r1, v FROM t WHERE v = 1;
SELECT DISTINCT r1, v FROM t WHERE v = 1;

-- Non-index predicates, see comment (6) above.
-- r1 = r2 tested above is a non-index predicate as well.
-- Here are some more tests to include more expressions.

-- Support product expressions.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r1 FROM t WHERE r1 * r1 = 1;
SELECT DISTINCT r1 FROM t WHERE r1 * r1 = 1;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r1 FROM t WHERE r2 * r2 = 1;
SELECT DISTINCT r1 FROM t WHERE r2 * r2 = 1;

-- Support expressions in targets.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r1 * r2 AS r FROM t;
SELECT DISTINCT r1 * r2 AS r FROM t;

-- Support expressions that can have duplicate values even when the arguments are DISTINCT.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT 0 * r1 AS r FROM t;
SELECT DISTINCT 0 * r1 AS r FROM t;

-- Do not generate distinct index paths in the presence of volatile expressions.
-- In general, volatile expressions may have side effects, so they need to be run
-- for each tuple. Cannot use a distinct index scan in that case.
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r1 FROM t WHERE r1 * RANDOM() > 1;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r1 * RANDOM() AS r FROM t;

-- Test range clauses as well while here.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r1 FROM t WHERE r1 > 1;
SELECT DISTINCT r1 FROM t WHERE r1 > 1;
-- r2 need not be part of the distinct prefix since the only clause is an index clause
-- and index clauses currently execute before DISTINCT.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r1 FROM t WHERE r2 > 1;
SELECT DISTINCT r1 FROM t WHERE r2 > 1;
-- Cannot eliminate r2 here since the query requires all distinct values of r2 and there isn't
-- one distinct value of r2 unlike constants.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r2 FROM t WHERE r2 < 5;
SELECT DISTINCT r2 FROM t WHERE r2 < 5;

-- Now, execute some scalar array operations.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r1 FROM t WHERE r1 IN (1, 2);
SELECT DISTINCT r1 FROM t WHERE r1 IN (1, 2);
-- Out-of-range query.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r1 FROM t WHERE r1 IN (1, 2);
SELECT DISTINCT r1 FROM t WHERE r1 IN (3, 5);
-- Do not include index clause references in the prefix.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r1 FROM t WHERE r2 IN (1, 2);
SELECT DISTINCT r1 FROM t WHERE r2 IN (1, 2);
-- Eliminate r2 from the prefix since it is a constant.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r1, r2 FROM t WHERE r1 IN (1, 2) AND r2 = 2;
SELECT DISTINCT r1, r2 FROM t WHERE r1 IN (1, 2) AND r2 = 2;

-- LSM indexes support IN index clauses on lower key columns and still support
-- sorting.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r1, r2 FROM t WHERE r2 IN (1, 2) ORDER BY r1, r2;
SELECT DISTINCT r1, r2 FROM t WHERE r2 IN (1, 2) ORDER BY r1, r2;

-- Unique node still necessary for range columns equal to constant.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r1 FROM t WHERE r1 = 1;
SELECT DISTINCT r1 FROM t WHERE r1 = 1;

DROP TABLE t;

-- h in th refers to hash table.
CREATE TABLE th(h1 INT, h2 INT, r1 INT, r2 INT, v INT, PRIMARY KEY((h1, h2) HASH, r1 ASC, r2 ASC)) SPLIT INTO 16 TABLETS;
INSERT INTO th (SELECT 1, i%3, 2-i%3, i, i/3 FROM GENERATE_SERIES(1, 1000) AS i);
INSERT INTO th (SELECT 2, i%3, 2-i%3, i, i/3 FROM GENERATE_SERIES(1, 1000) AS i);

-- Implementing DISTINCT support for hash columns requires that
--
-- 1) Distinct Prefix spans all hash columns. While DocDB stores hash columns
--    in sorted order in the index, DocDB also prepends a hash_code that is a
--    hash of all the hash columns. This structure makes it so that skipping
--    over duplicate prefix of hash columns is simply not possible.
--    For example, consider a scenario where the first column is degenerate
--    with all values being equal. The iterator cannot skip to the end since
--    the hash code changes with changing values of the second hash column.
-- 2) Hash ordering is leakproof. Despite all the differences mentioned
--    between sorting and uniq, these two operations are closely related.
--    DocDB is able to skip over duplicate elements precisely because the
--    columns are sorted by the prefix. Supporting hash columns for uniq
--    exposes the risk of considering hash columns for sorting. It does not
--    help that the hash columns are deceptively sorted. The hash_code column
--    at the start makes it so that the leading hash column is not sorted
--    similar to how a lower range column is not sorted. Except a query can
--    request an ordering of the index prefix but not the hash code.
-- 3) Unlike tables with a leading range column, tables with leading hash
--    columns are separated cleanly by virtue of hash code. Hence, no unique
--    node is necessary here on top unlike range column exclusive prefixes.

-- All hash columns.
-- Isn't necessary to stick a unique node on top since hash columns
-- separate cleanly across tables.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT h1, h2 FROM th;
SELECT DISTINCT h1, h2 FROM th;

-- Strict prefix of hash columns.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT h1 FROM th;
SELECT DISTINCT h1 FROM th;

-- Subset of the prefix of all hash columns.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT h2 FROM th;
SELECT DISTINCT h2 FROM th;

-- Both hash and range columns.
-- Prefix is still not sorted just because a range column is selected.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT h1, h2, r1 FROM th;
SELECT DISTINCT h1, h2, r1 FROM th;

-- Range columns only.
-- Includes hash columns since they come first.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r1 FROM th;
SELECT DISTINCT r1 FROM th;

-- Avoid classifying hash columns as sortable.
-- Guard rails meant to prevent DISTINCT logic from
-- marking hash columns as sortable.
SET yb_explain_hide_non_deterministic_fields = true;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT h1 FROM th ORDER BY h1;
SELECT DISTINCT h1 FROM th ORDER BY h1;
-- Once all the hash columns are set, range columns are returned in sorted order as usual.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r1 FROM th WHERE h1 = 1 AND h2 = 1 ORDER BY r1;
SELECT DISTINCT r1 FROM th WHERE h1 = 1 AND h2 = 1 ORDER BY r1;
-- Not the case if any of the hash columns are not set.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r1 FROM th WHERE h1 = 1 ORDER BY r1;
SELECT DISTINCT r1 FROM th WHERE h1 = 1 ORDER BY r1;

-- Hash columns constant.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT h1, h2 FROM th WHERE h1 = 1 AND h2 = 1;
SELECT DISTINCT h1, h2 FROM th WHERE h1 = 1 AND h2 = 1;

-- Range column constant
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT h1, h2 FROM th WHERE r1 = 1;
SELECT DISTINCT h1, h2 FROM th WHERE r1 = 1;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT r1 FROM th WHERE r1 = 1;
SELECT DISTINCT r1 FROM th WHERE r1 = 1;

DROP TABLE th;

-- Split at 1, ... to ensure that the value r1 = 1 is present in more than one tablet.
-- See #18101.
CREATE TABLE t(r1 INT, r2 INT, r3 INT, v INT, PRIMARY KEY(r1 ASC, r2 ASC, r3 ASC)) SPLIT AT VALUES ((1, 1, 500));
INSERT INTO t (SELECT 1, i%3, i, i/3 FROM GENERATE_SERIES(1, 1000) AS i);
-- Add one more distinct value to catch bugs that arise only with more than one distinct value.
INSERT INTO t (SELECT 2, i%3, i, NULL FROM GENERATE_SERIES(1, 1000) AS i);

-- Only new thing here is that secondary key columns can be NULL.
-- It should not matter whether or not NULLS come first in the index or last.
-- Cover a fair amount of test cases regardless.

CREATE INDEX irv ON t (v ASC, r1 ASC);

-- Start off easy.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT v FROM t;
SELECT DISTINCT v FROM t LIMIT 10;

-- Check if ORDER BY works as usual with DISTINCT.
-- Might as well fish for a backwards scan.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT v FROM t ORDER BY v DESC;
SELECT DISTINCT v FROM t ORDER BY v DESC LIMIT 10;

-- Cover a case where no index is applicable.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT v, r2 FROM t;
SELECT DISTINCT v, r2 FROM t LIMIT 10;

DROP INDEX irv;

-- Secondary index is useful for DISTINCT regardless of whether NULLS are first or last.
CREATE INDEX irv ON t(v ASC NULLS FIRST, r1 ASC);

EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT v FROM t;
SELECT DISTINCT v FROM t LIMIT 10;

DROP INDEX irv;

DROP TABLE t;

-- Now, turn to table with hash columns.
CREATE TABLE th(h1 INT, h2 INT, r1 INT, r2 int, v INT, PRIMARY KEY((h1, h2) HASH, r1 ASC, r2 ASC)) SPLIT INTO 16 TABLETS;
INSERT INTO th (SELECT 1, i%3, 2-i%3, i, i/3 FROM GENERATE_SERIES(1, 1000) AS i);
INSERT INTO th (SELECT 2, i%3, 2-i%3, i, i/3 FROM GENERATE_SERIES(1, 1000) AS i);

CREATE INDEX ihv ON th((v, h1) HASH);

-- Go easy again. Picks the index ihv for distinct index scan.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT v FROM th;
SELECT DISTINCT v FROM th LIMIT 10;

DROP INDEX ihv;

DROP TABLE th;

-- Split at 1, ... to ensure that the value r1 = 1 is present in more than one tablet.
-- See #18101.
CREATE TABLE t(r1 INT, r2 INT, r3 INT, v INT, PRIMARY KEY(r1 ASC, r2 ASC, r3 ASC)) SPLIT AT VALUES ((1, 1, 500));
INSERT INTO t (SELECT 1, i%3, i, i/3 FROM GENERATE_SERIES(1, 1000) AS i);
-- Add one more distinct value to catch bugs that arise only with more than one distinct value.
INSERT INTO t (SELECT 2, i%3, i, i/3 FROM GENERATE_SERIES(1, 1000) AS i);

-- Joins are not supported at the moment.
--
-- 1) Unique node added on top of base index scans directly for distinct prefixes
--    that are range column exclusive, see #18101.
--    Usually, Unique nodes are added only after all the joins are done but not so here.
-- 2) Unlike sort, distinct keys are simply a union of base relation keys in joins.
-- 3) Similarly, unlike sort, distinct can permute its targets.
-- 4) Unlike sort, a prefix of distinct keys are not distinct.

-- Basic self inner join.
-- MergeJoin.
SELECT DISTINCT r1 FROM t t1 JOIN t t2 USING (r1);
-- Now with a larger prefix.
SELECT DISTINCT r1, r2 FROM t t1 JOIN t t2 USING (r1, r2);
-- DISTINCT works with permuted targets.
SELECT DISTINCT r2, r1 FROM t t1 JOIN t t2 USING (r1, r2);
-- MergeJoin => requires a sort of both r1 and r2, and join condition is applied with join and not index scan.
-- Implies that prefix length is 2 and not 1 despite the fact that the output does not contain r2.
SELECT DISTINCT r1 FROM t t1 JOIN t t2 USING (r1, r2);

-- NestLoop join.
SELECT DISTINCT r1 FROM t t1 JOIN t t2 USING (r1) WHERE t2.r1 = 1 AND t2.r2 = 1 AND t2.r3 = 1;

DROP TABLE t;

CREATE TABLE sample(a int, b int, primary key(a asc, b asc));
INSERT INTO sample VALUES (1,1), (1,2);
DELETE FROM sample where b = 1;

EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT DISTINCT a FROM sample WHERE a > 0;
SELECT DISTINCT a FROM sample WHERE a > 0;

DROP TABLE sample;

-- See issue #22615
CREATE TABLE t(r1 INT, r2 INT, PRIMARY KEY(r1 ASC, r2 ASC));
INSERT INTO t (SELECT i, 1 FROM GENERATE_SERIES(1, 100) AS i);

SELECT DISTINCT r1 FROM t WHERE r1 IN (1, 10) ORDER BY r1 DESC;
SELECT DISTINCT r1 FROM t WHERE r1 IN (1, 10, 20) ORDER BY r1 DESC;

DROP TABLE t;
