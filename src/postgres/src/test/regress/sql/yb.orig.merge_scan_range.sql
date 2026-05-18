--
-- See yb_merge_scan_schedule for details about the test.
--

\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/merge_scan_setup.sql'
\i :filename

-- No order
-- Merge scan should not be used.
\set query ':explain :Q SELECT * FROM r5n WHERE r1 IN (0, 1, 2, 3) LIMIT 5;'
\i :iter_Q2

-- No limit
\set query ':explain :Q SELECT * FROM r5n WHERE r1 IN (0, 1, 2, 3) ORDER BY r2, r3, r4;'
\i :iter_Q2

-- Forward scan
\set query ':P :Q SELECT * FROM r5n WHERE r1 IN (0, 1, 2, 3) ORDER BY r2, r3, r4, n LIMIT 5;'
\i :iter_P2

-- Backward scan
\set query ':P :Q SELECT * FROM r5n WHERE r1 IN (0, 1, 2, 3) ORDER BY r2 DESC, r3 DESC, r4 DESC, n LIMIT 5;'
\i :iter_P2

-- Partial forward scan
\set query ':P :Q SELECT * FROM r5n WHERE r1 IN (0, 1, 2, 3) ORDER BY r2, r3 DESC, r4, n LIMIT 5;'
\i :iter_P2

-- Partial backward scan
\set query ':P :Q SELECT * FROM r5n WHERE r1 IN (0, 1, 2, 3) ORDER BY r2 DESC, r3, r4 DESC, n LIMIT 5;'
\i :iter_P2

-- Targets
\set query ':P :Q SELECT r5, 1, r5 FROM r5n WHERE r1 IN (0, 1, 2, 3) ORDER BY r2 DESC, r3, r4 DESC, n LIMIT 5;'
\i :iter_P2

-- DISTINCT
\set query ':P :Q SELECT DISTINCT r2, r3 FROM r5n WHERE r1 IN (0, 1, 2, 3) ORDER BY r2, r3 LIMIT 5;'
\i :iter_P2

-- GROUP BY
\set query ':P :Q SELECT COUNT(*), r2, r3 FROM r5n WHERE r1 IN (0, 1, 2, 3) GROUP BY r2, r3 ORDER BY r2, r3 LIMIT 5;'
\i :iter_P2

-- sort, IN
-- Merge scan should not be used.
\set query ':explain :Q SELECT r1, n, r2, r3 FROM r5n WHERE r2 IN (0, 1, 2) ORDER BY r1, n LIMIT 5;'
\i :iter_Q2

-- _, IN, sort...
-- Merge scan should not be used.
\set query ':explain :Q SELECT r3, r4, r5, n, r2, r1 FROM r5n WHERE r2 IN (0, 1, 2) ORDER BY r3, r4, r5, n LIMIT 5;'
\i :iter_Q2

-- IN, sort, _, sort...
\set query ':P :Q SELECT r2, r4, r5, n, r1, r3 FROM r5n WHERE r1 IN (0, 1, 2) ORDER BY r2, r4, r5, n LIMIT 5;'
\i :iter_P2

-- IN, IN, sort...
\set query ':P :Q SELECT r3, r4, r5, n, r1, r2 FROM r5n WHERE r1 IN (0, 1, 2) AND r2 IN (3, 4, 5) ORDER BY r3, r4, r5, n LIMIT 5;'
\i :iter_P2

-- IN/=, sort...
-- Merge scan should not be used.
\set query ':P :Q SELECT r1, r2, r3, r4, r5, n FROM r5n WHERE r1 = 2 AND r1 IN (0, 2, 4, 6, 8) ORDER BY r2, r3, r4, r5, n LIMIT 5;'
\i :iter_P2

-- IN, IN/sort, IN, sort...
\set query ':P :Q SELECT r2, r4, r5, n, r1, r3 FROM r5n WHERE r1 IN (1, 2, 3, 4) AND r2 IN (1, 9) AND r3 IN (5, 6, 7, 8) ORDER BY r2, r4, r5, n LIMIT 5;'
\i :iter_P2

-- IN, =, IN, sort...
\set query ':P :Q SELECT r2, r4, r5, n, r1, r3 FROM r5n WHERE r1 IN (1, 2, 3, 4) AND r2 = 7 AND r3 IN (5, 6, 7, 8) ORDER BY r4, r5, n LIMIT 5;'
\i :iter_P2

-- IN, =/sort, IN, sort...
\set query ':P :Q SELECT r2, r4, r5, n, r1, r3 FROM r5n WHERE r1 IN (1, 2, 3, 4) AND r2 = 7 AND r3 IN (5, 6, 7, 8) ORDER BY r2, r4, r5, n LIMIT 5;'
\i :iter_P2

-- IN, =/sort, IN, sort... (out-of-order sort)
\set query ':P :Q SELECT r2, r4, r5, n, r1, r3 FROM r5n WHERE r1 IN (1, 2, 3, 4) AND r2 = 7 AND r3 IN (5, 6, 7, 8) ORDER BY r4, r5, r2, n LIMIT 5;'
\i :iter_P2

-- IN, =/sort, IN/sort, sort...
\set query ':P :Q SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 IN (1, 2, 3, 4) AND r2 = 7 AND r3 IN (5, 6, 7, 8) ORDER BY r2, r3, r4, r5, n LIMIT 5;'
\i :iter_P2

-- IN, IN/=/IN, sort...
\set query ':P :Q SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 IN (0, 1, 2, 3, 4, 5, 6) AND r2 IN (0, 1, 2) AND r2 = 2 AND r2 IN (1, 2, 3) ORDER BY r3, r4, r5, n LIMIT 5;'
\i :iter_P2

-- IN/=/IN, IN, sort...
\set query ':P :Q SELECT r1, r3, r4, r5, n, r2 FROM r5n WHERE r2 IN (0, 1, 2, 3, 4, 5, 6) AND r1 IN (0, 1, 2) AND r1 = 2 AND r1 IN (1, 2, 3) ORDER BY r3, r4, r5, n LIMIT 5;'
\i :iter_P2

-- =, IN/IN, sort...
\set query ':P :Q SELECT r1, r3, r4, r5, n, r2 FROM r5n WHERE r1 = 7 AND r2 IN (1, 2, 3, 4) AND r2 IN (0, 2, 4, 6, 8) ORDER BY r3, r4, r5, n LIMIT 5;'
\i :iter_P2

-- =, not-IN/IN, sort...
\set query ':P :Q SELECT r1, r3, r4, r5, n, r2 FROM r5n WHERE r1 = 7 AND r2 IN (1, 2, 3, 4) AND r2 NOT IN (0, 2, 4, 6, 8) ORDER BY r3, r4, r5, n LIMIT 5;'
\i :iter_P2

-- IN/>/<, sort...
\set query ':P :Q SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 > 1 AND r1 IN (0, 2, 4, 6, 8) AND r1 < 7 ORDER BY r2, r3, r4, r5, n LIMIT 5;'
\i :iter_P2

-- Not-IN, sort...
-- Merge scan should not be used.
\set query ':explain :Q SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 NOT IN (0, 2, 4, 6, 8) ORDER BY r2, r3, r4, r5, n LIMIT 5;'
\i :iter_Q2

-- =ANY, sort...
\set query ':P :Q SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 = ANY (ARRAY[[[0], [2]], [[4], [6]], [[8], [10]]]) ORDER BY r2, r3, r4, r5, n LIMIT 5;'
\i :iter_P2

-- >ANY, sort...
-- Merge scan should not be used.
\set query ':explain :Q SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 > ANY (ARRAY[0, 2, 4, 6, 8]) ORDER BY r2, r3, r4, r5, n LIMIT 5;'
\i :iter_Q2

-- >=ALL, sort...
-- Merge scan should not be used.
\set query ':explain :Q SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 >= ALL (ARRAY[0, 2, 4, 6, 8]) ORDER BY r2, r3, r4, r5, n LIMIT 5;'
\i :iter_Q2

-- IN, sort, =, sort
\set query ':P :Q SELECT r3, r2, r4, n, r1 FROM r5n WHERE r1 IN (0, 1, 2, 3, 4, 5) AND r3 = 2 ORDER BY r2, r4, n LIMIT 5;'
\i :iter_P2

-- =, sort, IN, sort
-- Merge scan should not be used.
\set query ':explain :Q SELECT r1, r2, r4, n, r3 FROM r5n WHERE r1 = 2 AND r3 IN (0, 1, 2, 3, 4, 5) ORDER BY r2, r4, n LIMIT 5;'
\i :iter_Q2

-- IN, sort, IN, sort...
\set query ':P :Q SELECT r2, r4, r5, n, r1, r3 FROM r5n WHERE r1 IN (0, 1, 2) AND r3 IN (3, 4, 5) ORDER BY r2, r4, r5, n LIMIT 5;'
\i :iter_P2

-- IN, sort, IN/sort, sort...
\set query ':P :Q SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 IN (0, 1, 2) AND r3 IN (3, 4, 5) ORDER BY r2, r3, r4, r5, n LIMIT 5;'
\i :iter_P2

-- IN equivalence to first key non-sort column
-- TODO(#29030): this should use merge scan.
\set query ':P :Q SELECT r3, r4, r5, n, r1, r2 FROM r5n WHERE r2 IN (7, 8, 9) AND r2 = r1 ORDER BY r3, r4, r5, n LIMIT 5;'
\i :iter_P2

-- IN equivalence to first key sort column
\set query ':P :Q SELECT r1, r3, r4, r5, n, r2 FROM r5n WHERE r1 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) AND r2 IN (6, 0, 5) AND r1 = r3 ORDER BY r3, r4, r5, n LIMIT 5;'
\i :iter_P2

-- IN equivalence to first key sort column (v2)
\set query ':P :Q SELECT r1, r3, r4, r5, n, r2 FROM r5n WHERE r1 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) AND r2 IN (6, 0, 5) AND r1 = r3 ORDER BY r1, r4, r5, n LIMIT 5;'
\i :iter_P2

-- IN equivalence to last key sort column
\set query ':P :Q SELECT r3, r4, r1, r5, n, r2 FROM r5n WHERE r1 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) AND r2 IN (6, 0, 5) AND r1 = r5 ORDER BY r3, r4, r5, n LIMIT 5;'
\i :iter_P2

-- IN equivalence to last key non-sort column
\set query ':P :Q SELECT r3, r4, n, r1, r5, r2 FROM r5n WHERE r1 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) AND r2 IN (6, 0, 5) AND r1 = r5 ORDER BY r3, r4, n LIMIT 5;'
\i :iter_P2

-- IN equivalence to last non-key sort column
\set query ':P :Q SELECT r3, r4, r5, r1, n, r2 FROM r5n WHERE r1 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) AND r2 IN (6, 0, 5) AND r1 = n ORDER BY r3, r4, r5, n LIMIT 5;'
\i :iter_P2

-- =-var equivalence prefix
-- Merge scan should not be used.
\set query ':explain :Q SELECT r4, r5, n, r3, r1, r2 FROM r5n WHERE r3 IN (7, 8, 9) AND r1 = r2 ORDER BY r4, r5, n LIMIT 5;'
\i :iter_Q2

-- =-var equivalence suffix
-- Merge scan should not be used.
\set query ':explain :Q SELECT r4, r5, n, r1, r2, r3 FROM r5n WHERE r1 IN (7, 8, 9) AND r2 = r3 ORDER BY r4, r5, n LIMIT 5;'
\i :iter_Q2

-- OR clause
\set query ':P :Q SELECT r3, r4, r5, n, r1, r2 FROM r5n WHERE r2 IN (0, 1, 2, 3, 4, 5, 6) AND r1 IN (0, 1, 2) AND (r1 = 2 OR r1 IN (1, 2, 3)) ORDER BY r3, r4, r5, n LIMIT 5;'
\i :iter_P2

-- Another OR clause
-- Merge scan should not be used.
\set query ':explain :Q SELECT r3, r4, r5, n, r2, r1 FROM r5n WHERE r2 IN (0, 1, 2, 3, 4, 5, 6) AND (r1 IN (0, 1, 2) OR r1 = 2 OR r1 IN (1, 2, 3)) ORDER BY r3, r4, r5, n LIMIT 5;'
\i :iter_Q2

-- Cross-type SAOP: compatible type
\set query ':P :Q SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 = ANY (ARRAY[0, 2, 4, 6, 8]::int8[]) ORDER BY r2, r3, r4, r5, n LIMIT 5;'
\i :iter_P2

-- Cross-type SAOP: incompatible type
-- Merge scan should not be used.
\set query ':explain :Q SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 = ANY (ARRAY[0, 2, 4, 6, 8]::float[]) ORDER BY r2, r3, r4, r5, n LIMIT 5;'
\i :iter_Q2

-- Typecasted LHS
-- Merge scan should not be used.
\set query ':explain :Q SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1::text = ANY (ARRAY[0, 2, 4, 6, 8]::text[]) ORDER BY r2, r3, r4, r5, n LIMIT 5;'
\i :iter_Q2

-- ArrayExpr containing FuncExpr
-- Merge scan should not be used.
\set query ':explain :Q SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 IN (0, 1, random()::int) ORDER BY r2, r3, r4, r5, n LIMIT 5;'
\i :iter_Q2

-- ArrayExpr containing OpExpr
-- Merge scan should not be used.
\set query ':explain :Q SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 IN (1, 100 + random()::int) ORDER BY r2, r3, r4, r5, n LIMIT 5;'
\i :iter_Q2

-- ArrayExpr containing Param
-- Merge scan should not be used.
\set query ':explain :Q SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 IN (1, (SELECT count(*) FROM r5n)) ORDER BY r2, r3, r4, r5, n LIMIT 5;'
\i :iter_Q2

-- Row IN without constant table
-- TODO(#29032): this should use merge scan.
\set query ':P :Q SELECT r3, r4, r5, n, r1, r2 FROM r5n WHERE (r1, r2) IN ((1, 2), (3, 4)) ORDER BY r3, r4, r5, n LIMIT 5;'
\i :iter_P2

-- Row IN with constant table
-- TODO(#29032): this should use merge scan.
\set query ':P :Q SELECT r3, r4, r5, n, r1, r2 FROM r5n WHERE row(r1, r2) IN (values (1, 2), (3, 4)) ORDER BY r3, r4, r5, n LIMIT 5;'
\i :iter_P2

-- Subquery with ORDER inside
\set query ':P :Q SELECT r2, r3, r4, n FROM (SELECT r2, r3, r4, n FROM r5n WHERE r1 IN (0, 1, 2, 3) ORDER BY r2, r3, r4, n LIMIT 5) sub;'
\i :iter_P2

-- Subquery with ORDER outside
\set query ':P :Q SELECT r2, r3, r4, n FROM (SELECT r2, r3, r4, n FROM r5n WHERE r1 IN (0, 1, 2, 3)) sub ORDER BY r2, r3, r4, n LIMIT 5;'
\i :iter_P2

-- NULL in IN
-- TODO(#29073) after culling array, number of streams should be 3.
\set query ':P :Q SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 IN (1, 2, null, 3) ORDER BY r2, r3, r4, r5, n LIMIT 5;'
\i :iter_P2

-- Only NULLs in IN
-- Third hint is to use merge scan as the second hint ends up using sort.
-- TODO(#29073): after culling array, maybe the third hint should not use SAOP
-- merge.
\set query ':P :Q SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 IN (null, null, null) ORDER BY r2, r3, r4, r5, n LIMIT 5;'
\set Q3 '/*+Set(enable_sort off) Set(yb_max_merge_scan_streams 64)*/'
\set Pnext :iter_Q3
\i :iter_P2

-- Empty array
-- Third hint is to use merge scan as the second hint ends up using sort.
\set query ':P :Q SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 = ANY(''{}'') ORDER BY r2, r3, r4, r5, n LIMIT 5;'
\i :iter_P2

-- Non-const in RHS (like var ref)
-- Merge scan should not be used.
\set query ':explain :Q SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 IN (1, r2, 2) ORDER BY r2, r3, r4, r5, n LIMIT 5;'
\i :iter_Q2

-- Large number of streams
\set query ':P :Q SELECT r4, r5, n, r1, r2, r3 FROM r5n WHERE r1 IN (0, 1, 2, 3) AND r2 IN (0, 1, 2, 3) AND r3 IN (0, 1, 2, 3) ORDER BY r4, r5, n LIMIT 5;'
\set Pnext :iter_Q2
\i :iter_P2

-- Single IN hitting limit
-- Merge scan should not be used.
\set on '/*+Set(yb_max_merge_scan_streams 5)*/'
\set query ':explain :Q SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 IN (0, 2, 4, 6, 8, 10) ORDER BY r2, r3, r4, r5, n LIMIT 5;'
\i :iter_Q2

-- Double IN hitting limit
-- Merge scan should not be used.
\set query ':explain :Q SELECT r3, r4, r5, n, r1, r2 FROM r5n WHERE r1 IN (0, 2, 4) AND r2 IN (6, 8) ORDER BY r3, r4, r5, n LIMIT 5;'
\i :iter_Q2

-- Triple IN hitting limit before realizing limit could be satisfied with 0x
-- multiplier.
-- Third hint is to encourage use of merge scan like other similar empty array
-- test cases.
-- Merge scan should not be used.
\set query ':explain :Q SELECT r3, r4, r5, n, r1, r2 FROM r5n WHERE r1 IN (0, 2, 4) AND r2 IN (6, 8) AND r3 = ANY(''{}'') ORDER BY r4, r5, n LIMIT 5;'
\set Q3 '/*+Set(enable_sort off) Set(yb_max_merge_scan_streams 5)*/'
\i :iter_Q3

-- Triple IN avoiding limit because of 0x multiplier.
-- Third hint is to use merge scan as the second hint ends up using sort.
\set query ':P :Q SELECT r3, r4, r5, n, r1, r2 FROM r5n WHERE r1 IN (0, 2, 4) AND r2 = ANY(''{}'') AND r3 IN (6, 8) ORDER BY r4, r5, n LIMIT 5;'
\set Pnext :iter_Q3
\i :iter_P2

-- Choose lowest cardinality IN
-- TODO(#20899): this test needs to be reconsidered when fixing this issue.
\set query ':P :Q SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 IN (1, 2, 3, 4, 5, 6) AND r1 IN (3, 4, 5, 6, 7) AND r1 IN (5, 6, 7, 8, 9, 10) ORDER BY r2, r3, r4, r5, n LIMIT 5;'
\set Pnext :iter_Q2
\i :iter_P2

-- Duplicates in IN
-- TODO(#29073): after culling array, this should use merge scan.
\set query ':P :Q SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 IN (1, 1, 1, 2, 3, 3, 4, 5, 5, 5, 1) ORDER BY r2, r3, r4, r5, n LIMIT 5;'
\i :iter_P2

-- (Reset the limit change)
\set on '/*+Set(yb_max_merge_scan_streams 64)*/'

-- #30096: Merge scan shouldn't be used in a parallel scan.
\set query ':explain :Q SELECT * FROM r5n WHERE r1 IN (0, 2, 4) AND r2 IN (6, 8) ORDER BY r3, r4, r5;'
\set Q3 '/*+Parallel(r5n 2) Set(yb_enable_parallel_scan_range_sharded true) Set(yb_parallel_range_rows 1) Set(yb_max_merge_scan_streams 0)*/'
\set Q4 '/*+Parallel(r5n 2) Set(yb_enable_parallel_scan_range_sharded true) Set(yb_parallel_range_rows 1) Set(yb_max_merge_scan_streams 64)*/'
\i :iter_Q4

-- Same thing with backwards scan.
\set query ':explain :Q SELECT * FROM r5n WHERE r1 IN (0, 2, 4) AND r2 IN (6, 8) ORDER BY r3 DESC, r4 DESC, r5 DESC;'
\i :iter_Q4

--
-- Secondary index
--
CREATE INDEX NONCONCURRENTLY ON r5n (r2 ASC, r3, r4, r5)
SPLIT AT VALUES (
    (1),
    (2),
    (2, 2),
    (2, 2, 2),
    (2, 2, 2, 2),
    (3));

-- No order
-- Merge scan should not be used.
\set query ':explain :Q SELECT * FROM r5n WHERE r2 IN (0, 1, 2, 3) LIMIT 5;'
\i :iter_Q2

-- No limit
-- TODO(#29078): this likely should use merge scan.
\set query ':explain :Q SELECT * FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY r3, r4, r5;'
\i :iter_Q2

-- Following queries send various numbers of requests/scan various number of rows
-- due to non-deterministic order of equal rows in merge sort, hide their DIST
\set explain 'EXPLAIN (ANALYZE, VERBOSE, COSTS OFF, SUMMARY OFF, TIMING OFF)'

-- Forward scan
\set query ':P :Q SELECT * FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY r3, r4, n LIMIT 5;'
\i :iter_P2

-- Backward scan
\set query ':P :Q SELECT * FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY r3 DESC, r4 DESC, n LIMIT 5;'
\i :iter_P2

-- Targets
\set query ':P :Q SELECT r5, 1, r5 FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY r3 DESC, r4 DESC, n LIMIT 5;'
\i :iter_P2

-- (Reset the explain change)
\set explain 'EXPLAIN (ANALYZE, DIST, VERBOSE, COSTS OFF, SUMMARY OFF, TIMING OFF)'

-- Secondary index scan VS merge PK scan
-- Expected secondary index scan; merge PK scan likely wins due to missing
-- merge-scan-specific cost model overhead/savings, expected to be fixed by
-- https://github.com/yugabyte/yugabyte-db/issues/29078.
\set query ':P :Q SELECT r2, r3, r4, n, r1 FROM r5n WHERE r1 IN (1, 2, 3, 4, 5) AND r2 = 4 ORDER BY r3, r4, n LIMIT 5;'
\i :iter_P2

-- Merge secondary index scan VS merge PK scan
-- Third hint is to use the secondary index as the second hint ends up using
-- the PK index.
\set query ':P :Q SELECT r3, r4, r5, n, r1, r2 FROM r5n WHERE r1 IN (1, 2, 3, 4) AND r2 IN (1, 2, 3, 4) ORDER BY r3, r4, r5, n LIMIT 5;'
\set Q3 '/*+IndexScan(r5n r5n_r2_r3_r4_r5_idx) Set(yb_max_merge_scan_streams 64)*/'
\set Pnext :iter_Q3
\i :iter_P2

-- (Drop this index)
DROP INDEX r5n_r2_r3_r4_r5_idx;

--
-- Custom opclass secondary index
--
CREATE OPERATOR =#= (
    leftarg = int8,
    rightarg = int8,
    procedure = int8eq,
    commutator = =#=,
    negator = <>
);
CREATE FUNCTION my_int8_sort(int8,int8) RETURNS int LANGUAGE sql AS $$
    SELECT CASE WHEN $1 = $2 THEN 0 WHEN $1 > $2 THEN 1 ELSE -1 END;
$$;
CREATE OPERATOR CLASS test_int8_ops FOR TYPE int8 USING lsm AS
    OPERATOR 1 < (int8,int8), OPERATOR 2 <= (int8,int8),
    OPERATOR 3 =#= (int8,int8), OPERATOR 4 >= (int8,int8),
    OPERATOR 5 > (int8,int8), FUNCTION 1 my_int8_sort(int8,int8);
-- For numeric, treat 0 as highest value.
CREATE FUNCTION my_numeric_lt(numeric,numeric) RETURNS bool LANGUAGE sql AS $$
    SELECT CASE
        WHEN $1 = 0 THEN false
        WHEN $2 = 0 THEN true
        ELSE $1 < $2
    END;
$$;
CREATE FUNCTION my_numeric_le(numeric,numeric) RETURNS bool LANGUAGE sql AS $$
    SELECT CASE
        WHEN $1 = $2 THEN true
        WHEN $1 = 0 THEN false
        WHEN $2 = 0 THEN true
        ELSE $1 <= $2
    END;
$$;
CREATE FUNCTION my_numeric_ge(numeric,numeric) RETURNS bool LANGUAGE sql AS $$
    SELECT CASE
        WHEN $1 = $2 THEN true
        WHEN $2 = 0 THEN false
        WHEN $1 = 0 THEN true
        ELSE $1 >= $2
    END;
$$;
CREATE FUNCTION my_numeric_gt(numeric,numeric) RETURNS bool LANGUAGE sql AS $$
    SELECT CASE
        WHEN $2 = 0 THEN false
        WHEN $1 = 0 THEN true
        ELSE $1 > $2
    END;
$$;
CREATE OPERATOR #<# (
    leftarg = numeric,
    rightarg = numeric,
    procedure = my_numeric_lt,
    commutator = #>#,
    negator = #>=#
);
CREATE OPERATOR #<=# (
    leftarg = numeric,
    rightarg = numeric,
    procedure = my_numeric_le,
    commutator = #>=#,
    negator = #>#
);
CREATE OPERATOR #=# (
    leftarg = numeric,
    rightarg = numeric,
    procedure = numeric_eq,
    commutator = #=#,
    negator = <>
);
CREATE OPERATOR #>=# (
    leftarg = numeric,
    rightarg = numeric,
    procedure = my_numeric_ge,
    commutator = #<=#,
    negator = #<#
);
CREATE OPERATOR #># (
    leftarg = numeric,
    rightarg = numeric,
    procedure = my_numeric_gt,
    commutator = #<#,
    negator = #<=#
);
CREATE FUNCTION my_numeric_sort(numeric,numeric) RETURNS int LANGUAGE sql AS $$
    SELECT CASE
        WHEN $1 = $2 THEN 0
        WHEN $1 = 0 THEN 1
        WHEN $2 = 0 THEN -1
        WHEN $1 > $2 THEN 1
        ELSE -1
    END;
$$;
CREATE OPERATOR CLASS test_numeric_ops FOR TYPE numeric USING lsm AS
    OPERATOR 1 #<# (numeric,numeric), OPERATOR 2 #<=# (numeric,numeric),
    OPERATOR 3 #=# (numeric,numeric), OPERATOR 4 #>=# (numeric,numeric),
    OPERATOR 5 #># (numeric,numeric), FUNCTION 1 my_numeric_sort(numeric,numeric);
CREATE INDEX NONCONCURRENTLY ON r5n (r2 test_int8_ops ASC, r3, r4 test_numeric_ops, r5)
SPLIT AT VALUES (
    (1),
    (2),
    (2, 2),
    (2, 2, 2),
    (2, 2, 2, 2),
    (3));

-- No order
-- Merge scan should not be used.
\set query ':explain :Q SELECT * FROM r5n WHERE r2 =#= ANY(ARRAY[0, 1, 2, 3]) LIMIT 5;'
\i :iter_Q2

-- Forward scan
-- TODO(#29383): the output is incorrect where it relies on DocDB order.
-- \set query ':P :Q SELECT * FROM r5n WHERE r2 =#= ANY(ARRAY[0, 1, 2, 3]) ORDER BY r3, r4 USING #<#, n LIMIT 5;'
-- \set Pnext :iter_Q2
-- \i :iter_P2

-- Backward scan
-- TODO(#29383): the output is incorrect where it relies on DocDB order.
-- \set query ':P :Q SELECT * FROM r5n WHERE r2 =#= ANY(ARRAY[0, 1, 2, 3]) ORDER BY r3 DESC, r4 USING #>#, n LIMIT 5;'
-- \i :iter_P2

-- (Drop this index)
DROP INDEX r5n_r2_r3_r4_r5_idx;

--
-- INCLUDE index
--
CREATE INDEX NONCONCURRENTLY ON r5n (r2 ASC, r3, r4, r5) INCLUDE (r1, n)
SPLIT AT VALUES (
    (1),
    (2),
    (2, 2),
    (2, 2, 2),
    (2, 2, 2, 2),
    (3));

-- No order
-- Merge scan should not be used.
\set query ':explain :Q SELECT * FROM r5n WHERE r2 IN (0, 1, 2, 3) LIMIT 5;'
\i :iter_Q2

-- Following queries send various numbers of requests/scan various number of rows
-- due to non-deterministic order of equal rows in merge sort, hide their DIST
\set explain 'EXPLAIN (ANALYZE, VERBOSE, COSTS OFF, SUMMARY OFF, TIMING OFF)'

-- Forward scan
\set query ':P :Q SELECT * FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY r3, r4, n LIMIT 5;'
\set Pnext :iter_Q2
\i :iter_P2

-- Backward scan
\set query ':P :Q SELECT * FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY r3 DESC, r4 DESC, n LIMIT 5;'
\i :iter_P2

-- Targets
\set query ':P :Q SELECT n, 1, n FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY r3 DESC, r4 DESC, n LIMIT 5;'
\i :iter_P2

-- (Reset the explain change)
\set explain 'EXPLAIN (ANALYZE, DIST, VERBOSE, COSTS OFF, SUMMARY OFF, TIMING OFF)'

-- Secondary index only scan VS merge PK scan
-- Expected secondary index-only scan; merge PK scan is expected to be fixed by #29078.
\set query ':P :Q SELECT r2, r3, r4, n, r1 FROM r5n WHERE r1 IN (1, 2, 3, 4, 5) ORDER BY r2, r3, r4, n LIMIT 5;'
\i :iter_P2

-- (Drop this index)
DROP INDEX r5n_r2_r3_r4_r5_r1_n_idx;

--
-- Expression prefix secondary index
--
CREATE INDEX NONCONCURRENTLY ON r5n ((greatest(r2, r3, r4) - least(r2, r3, r4)) ASC, r2, r3, r4)
SPLIT AT VALUES (
    (1),
    (2),
    (2, 2),
    (2, 2, 2),
    (2, 2, 2, 2),
    (3));

-- No order
-- Merge scan should not be used.
\set query ':explain :Q SELECT * FROM r5n WHERE (greatest(r2, r3, r4) - least(r2, r3, r4)) IN (0, 2) LIMIT 5;'
\i :iter_Q2

-- Forward scan
\set query ':P :Q SELECT * FROM r5n WHERE (greatest(r2, r3, r4) - least(r2, r3, r4)) IN (0, 2) ORDER BY r2, r3, r4, n LIMIT 5;'
\i :iter_P2

-- Backward scan
\set query ':P :Q SELECT * FROM r5n WHERE (greatest(r2, r3, r4) - least(r2, r3, r4)) IN (0, 2) ORDER BY r2 DESC, r3 DESC, r4 DESC, n LIMIT 5;'
\i :iter_P2

-- Targets
\set query ':P :Q SELECT (greatest(r2, r3, r4) - least(r2, r3, r4)), 1, (greatest(r2, r3, r4) - least(r2, r3, r4)) FROM r5n WHERE (greatest(r2, r3, r4) - least(r2, r3, r4)) IN (0, 2) ORDER BY r2 DESC, r3 DESC, r4 DESC, n LIMIT 5;'
\i :iter_P2

-- Secondary index scan VS merge PK scan
-- Third hint is to use the PK index as the second hint ends up using the
-- expression index.
\set query ':P :Q SELECT (greatest(r2, r3, r4) - least(r2, r3, r4)), r2, r3, r4, n, r1 FROM r5n WHERE r1 IN (1, 2, 3, 4, 5) AND (greatest(r2, r3, r4) - least(r2, r3, r4)) = 4 ORDER BY r2, r3, r4, n LIMIT 5;'
\set Q3 '/*+IndexScan(r5n r5n_pkey) Set(yb_max_merge_scan_streams 64)*/'
\set Pnext :iter_Q3
\i :iter_P2

-- Merge secondary index scan VS merge PK scan
-- Third hint is to use the PK index as the second hint ends up using the
-- expression index.
\set query ':P :Q SELECT r2, r3, r4, n, r1, (greatest(r2, r3, r4) - least(r2, r3, r4)) FROM r5n WHERE r1 IN (1, 2, 3, 4) AND (greatest(r2, r3, r4) - least(r2, r3, r4)) IN (1, 2, 3, 4) ORDER BY r2, r3, r4, n LIMIT 5;'
\set Q3 '/*+IndexScan(r5n r5n_pkey) Set(yb_max_merge_scan_streams 64)*/'
\i :iter_P2

-- (Drop this index)
DROP INDEX r5n_expr_r2_r3_r4_idx;

--
-- Expression suffix secondary index
--
CREATE INDEX NONCONCURRENTLY ON r5n (r2 ASC, (-r3), (-r4))
SPLIT AT VALUES (
    (1),
    (2),
    (2, -2),
    (2, -2, -2),
    (3));

-- Forward scan
\set query ':P :Q SELECT * FROM r5n WHERE r2 IN (0, 2) ORDER BY -r3, -r4, n LIMIT 5;'
\set Pnext :iter_Q2
\i :iter_P2

-- Backward scan
\set query ':P :Q SELECT * FROM r5n WHERE r2 IN (0, 2) ORDER BY -r3 DESC, -r4 DESC, n LIMIT 5;'
\i :iter_P2

-- Targets
\set query ':P :Q SELECT -r4, 1, -r4 FROM r5n WHERE r2 IN (0, 2) ORDER BY -r3 DESC, -r4 DESC, n LIMIT 5;'
\i :iter_P2

-- (Drop this index)
DROP INDEX r5n_r2_expr_expr1_idx;

--
-- Duplicate columns secondary index
--
CREATE INDEX NONCONCURRENTLY ON r5n (r2 ASC, (r3 + r4), r2 DESC, (r3 + r4), r2)
SPLIT AT VALUES (
    (1),
    (2),
    (2, 2),
    (2, 2, 2),
    (2, 2, 2, 2),
    (2, 2, 2, 2, 2),
    (3));

-- No order
-- Merge scan should not be used.
\set query ':explain :Q SELECT * FROM r5n WHERE r2 IN (0, 1, 2, 3) LIMIT 5;'
\i :iter_Q2

-- Following queries send various numbers of requests/scan various number of rows
-- due to non-deterministic order of equal rows in merge sort, hide their DIST
\set explain 'EXPLAIN (ANALYZE, VERBOSE, COSTS OFF, SUMMARY OFF, TIMING OFF)'

-- Forward scan
\set query ':P :Q SELECT * FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY (r3 + r4), n LIMIT 5;'
\i :iter_P2

-- Forward scan (v2)
-- Merge scan is not used.
\set query ':explain :Q SELECT * FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY (r3 + r4), r2 DESC, n LIMIT 5;'
\i :iter_Q2

-- Backward scan
\set query ':P :Q SELECT * FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY (r3 + r4) DESC, n LIMIT 5;'
\i :iter_P2

-- Backward scan (v2)
-- Merge scan is not used.
\set query ':explain :Q SELECT * FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY (r3 + r4) DESC, r2, n LIMIT 5;'
\i :iter_Q2

-- Targets
\set query ':P :Q SELECT r5, 1, r5 FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY (r3 + r4) DESC, n LIMIT 5;'
\i :iter_P2

-- Targets (v2)
-- Merge scan is not used.
\set query ':explain :Q SELECT r5, 1, r5 FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY (r3 + r4) DESC, r2, n LIMIT 5;'
\i :iter_Q2

-- (Reset the explain change)
\set explain 'EXPLAIN (ANALYZE, DIST, VERBOSE, COSTS OFF, SUMMARY OFF, TIMING OFF)'

-- (Drop this index)
DROP INDEX r5n_r2_expr_r21_expr1_r22_idx;

-- test yb_enable_advanced_index_cond_fold flag off
SET yb_enable_advanced_index_cond_fold = off;
\set query ':P :Q SELECT r1, r3, r4, r5, n, r2 FROM r5n WHERE r1 = 7 AND r2 IN (1, 2, 3, 4) AND r2 IN (0, 2, 4, 6, 8) ORDER BY r3, r4, r5, n LIMIT 5;'
\i :iter_P2
\set query ':P :Q SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 > 1 AND r1 IN (0, 2, 4, 6, 8) AND r1 < 7 ORDER BY r2, r3, r4, r5, n LIMIT 5;'
\i :iter_P2
\set query ':P :Q SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 IN (1, 2, 3, 4, 5, 6) AND r1 IN (3, 4, 5, 6, 7) AND r1 IN (5, 6, 7, 8, 9, 10) ORDER BY r2, r3, r4, r5, n LIMIT 5;'
\i :iter_P2
RESET yb_enable_advanced_index_cond_fold;

--
-- EXPLAIN FORMAT JSON
--
CREATE FUNCTION get_explain_property(query text, property text) RETURNS jsonb LANGUAGE plpgsql AS $$
DECLARE
    plan jsonb;
BEGIN
    FOR plan IN EXECUTE 'EXPLAIN (ANALYZE, DIST, FORMAT JSON, VERBOSE)' || query LOOP
        RETURN plan->0->'Plan'->property;
    END LOOP;
END $$;

SELECT get_explain_property(hint || 'SELECT * FROM r5n WHERE r1 IN (1, 2, 3) ORDER BY r2', property)
FROM unnest(ARRAY['/*+Set(yb_max_merge_scan_streams 0)*/',
                  '/*+Set(yb_max_merge_scan_streams 64)*/']) AS hint,
     unnest(ARRAY['Merge Sort Key',
                  'Merge Stream Key',
                  'Merge Streams',
                  'Merge Cond']) AS property;
