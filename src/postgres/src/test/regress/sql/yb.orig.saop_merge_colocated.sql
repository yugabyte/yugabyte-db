--
-- See yb_saop_merge_schedule for details about the test.
--

\c co

\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/explainrun_saop_merge.sql'
\i :filename

-- No order
-- SAOP merge should not be used.
\set query 'SELECT * FROM r5n WHERE r1 IN (0, 1, 2, 3) LIMIT 5'
:explain2

-- No limit
\set query 'SELECT * FROM r5n WHERE r1 IN (0, 1, 2, 3) ORDER BY r2, r3, r4'
:explain2

-- Forward scan
\set query 'SELECT * FROM r5n WHERE r1 IN (0, 1, 2, 3) ORDER BY r2, r3, r4, n LIMIT 5'
:explain2run2

-- Backward scan
\set query 'SELECT * FROM r5n WHERE r1 IN (0, 1, 2, 3) ORDER BY r2 DESC, r3 DESC, r4 DESC, n LIMIT 5'
:explain2run2

-- Partial forward scan
\set query 'SELECT * FROM r5n WHERE r1 IN (0, 1, 2, 3) ORDER BY r2, r3 DESC, r4, n LIMIT 5'
:explain2run2

-- Partial backward scan
\set query 'SELECT * FROM r5n WHERE r1 IN (0, 1, 2, 3) ORDER BY r2 DESC, r3, r4 DESC, n LIMIT 5'
:explain2run2

-- Targets
\set query 'SELECT r5, 1, r5 FROM r5n WHERE r1 IN (0, 1, 2, 3) ORDER BY r2 DESC, r3, r4 DESC, n LIMIT 5'
:explain2run2

-- DISTINCT
\set query 'SELECT DISTINCT r2, r3 FROM r5n WHERE r1 IN (0, 1, 2, 3) ORDER BY r2, r3 LIMIT 5'
:explain2run2

-- GROUP BY
\set query 'SELECT COUNT(*), r2, r3 FROM r5n WHERE r1 IN (0, 1, 2, 3) GROUP BY r2, r3 ORDER BY r2, r3 LIMIT 5'
:explain2run2

-- sort, IN
-- SAOP merge should not be used.
\set query 'SELECT r1, n, r2, r3 FROM r5n WHERE r2 IN (0, 1, 2) ORDER BY r1, n LIMIT 5'
:explain2

-- _, IN, sort...
-- SAOP merge should not be used.
\set query 'SELECT r3, r4, r5, n, r2, r1 FROM r5n WHERE r2 IN (0, 1, 2) ORDER BY r3, r4, r5, n LIMIT 5'
:explain2

-- IN, sort, _, sort...
\set query 'SELECT r2, r4, r5, n, r1, r3 FROM r5n WHERE r1 IN (0, 1, 2) ORDER BY r2, r4, r5, n LIMIT 5'
:explain2run2

-- IN, IN, sort...
\set query 'SELECT r3, r4, r5, n, r1, r2 FROM r5n WHERE r1 IN (0, 1, 2) AND r2 IN (3, 4, 5) ORDER BY r3, r4, r5, n LIMIT 5'
:explain2run2

-- IN/=, sort...
-- SAOP merge should not be used.
\set query 'SELECT r1, r2, r3, r4, r5, n FROM r5n WHERE r1 = 2 AND r1 IN (0, 2, 4, 6, 8) ORDER BY r2, r3, r4, r5, n LIMIT 5'
:explain2run2

-- IN, IN/sort, IN, sort...
\set query 'SELECT r2, r4, r5, n, r1, r3 FROM r5n WHERE r1 IN (1, 2, 3, 4) AND r2 IN (1, 9) AND r3 IN (5, 6, 7, 8) ORDER BY r2, r4, r5, n LIMIT 5'
:explain2run2

-- IN, =, IN, sort...
\set query 'SELECT r2, r4, r5, n, r1, r3 FROM r5n WHERE r1 IN (1, 2, 3, 4) AND r2 = 7 AND r3 IN (5, 6, 7, 8) ORDER BY r4, r5, n LIMIT 5'
:explain2run2

-- IN, =/sort, IN, sort...
\set query 'SELECT r2, r4, r5, n, r1, r3 FROM r5n WHERE r1 IN (1, 2, 3, 4) AND r2 = 7 AND r3 IN (5, 6, 7, 8) ORDER BY r2, r4, r5, n LIMIT 5'
:explain2run2

-- IN, =/sort, IN, sort... (out-of-order sort)
\set query 'SELECT r2, r4, r5, n, r1, r3 FROM r5n WHERE r1 IN (1, 2, 3, 4) AND r2 = 7 AND r3 IN (5, 6, 7, 8) ORDER BY r4, r5, r2, n LIMIT 5'
:explain2run2

-- IN, =/sort, IN/sort, sort...
\set query 'SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 IN (1, 2, 3, 4) AND r2 = 7 AND r3 IN (5, 6, 7, 8) ORDER BY r2, r3, r4, r5, n LIMIT 5'
:explain2run2

-- IN, IN/=/IN, sort...
\set query 'SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 IN (0, 1, 2, 3, 4, 5, 6) AND r2 IN (0, 1, 2) AND r2 = 2 AND r2 IN (1, 2, 3) ORDER BY r3, r4, r5, n LIMIT 5'
:explain2run2

-- IN/=/IN, IN, sort...
\set query 'SELECT r1, r3, r4, r5, n, r2 FROM r5n WHERE r2 IN (0, 1, 2, 3, 4, 5, 6) AND r1 IN (0, 1, 2) AND r1 = 2 AND r1 IN (1, 2, 3) ORDER BY r3, r4, r5, n LIMIT 5'
:explain2run2

-- =, IN/IN, sort...
\set query 'SELECT r1, r3, r4, r5, n, r2 FROM r5n WHERE r1 = 7 AND r2 IN (1, 2, 3, 4) AND r2 IN (0, 2, 4, 6, 8) ORDER BY r3, r4, r5, n LIMIT 5'
:explain2run2

-- =, not-IN/IN, sort...
\set query 'SELECT r1, r3, r4, r5, n, r2 FROM r5n WHERE r1 = 7 AND r2 IN (1, 2, 3, 4) AND r2 NOT IN (0, 2, 4, 6, 8) ORDER BY r3, r4, r5, n LIMIT 5'
:explain2run2

-- IN/>/<, sort...
\set query 'SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 > 1 AND r1 IN (0, 2, 4, 6, 8) AND r1 < 7 ORDER BY r2, r3, r4, r5, n LIMIT 5'
:explain2run2

-- Not-IN, sort...
-- SAOP merge should not be used.
\set query 'SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 NOT IN (0, 2, 4, 6, 8) ORDER BY r2, r3, r4, r5, n LIMIT 5'
:explain2

-- =ANY, sort...
\set query 'SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 = ANY (ARRAY[[[0], [2]], [[4], [6]], [[8], [10]]]) ORDER BY r2, r3, r4, r5, n LIMIT 5'
:explain2run2

-- >ANY, sort...
-- SAOP merge should not be used.
\set query 'SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 > ANY (ARRAY[0, 2, 4, 6, 8]) ORDER BY r2, r3, r4, r5, n LIMIT 5'
:explain2

-- >=ALL, sort...
-- SAOP merge should not be used.
\set query 'SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 >= ALL (ARRAY[0, 2, 4, 6, 8]) ORDER BY r2, r3, r4, r5, n LIMIT 5'
:explain2

-- IN, sort, =, sort
\set query 'SELECT r3, r2, r4, n, r1 FROM r5n WHERE r1 IN (0, 1, 2, 3, 4, 5) AND r3 = 2 ORDER BY r2, r4, n LIMIT 5'
:explain2run2

-- =, sort, IN, sort
-- SAOP merge should not be used.
\set query 'SELECT r1, r2, r4, n, r3 FROM r5n WHERE r1 = 2 AND r3 IN (0, 1, 2, 3, 4, 5) ORDER BY r2, r4, n LIMIT 5'
:explain2

-- IN, sort, IN, sort...
\set query 'SELECT r2, r4, r5, n, r1, r3 FROM r5n WHERE r1 IN (0, 1, 2) AND r3 IN (3, 4, 5) ORDER BY r2, r4, r5, n LIMIT 5'
:explain2run2

-- IN, sort, IN/sort, sort...
\set query 'SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 IN (0, 1, 2) AND r3 IN (3, 4, 5) ORDER BY r2, r3, r4, r5, n LIMIT 5'
:explain2run2

-- IN equivalence to first key non-sort column
-- TODO(#29030): this should use SAOP merge.
\set query 'SELECT r3, r4, r5, n, r1, r2 FROM r5n WHERE r2 IN (7, 8, 9) AND r2 = r1 ORDER BY r3, r4, r5, n LIMIT 5'
:explain2run2

-- IN equivalence to first key sort column
\set query 'SELECT r1, r3, r4, r5, n, r2 FROM r5n WHERE r1 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) AND r2 IN (6, 0, 5) AND r1 = r3 ORDER BY r3, r4, r5, n LIMIT 5'
:explain2run2

-- IN equivalence to first key sort column (v2)
\set query 'SELECT r1, r3, r4, r5, n, r2 FROM r5n WHERE r1 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) AND r2 IN (6, 0, 5) AND r1 = r3 ORDER BY r1, r4, r5, n LIMIT 5'
:explain2run2

-- IN equivalence to last key sort column
\set query 'SELECT r3, r4, r1, r5, n, r2 FROM r5n WHERE r1 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) AND r2 IN (6, 0, 5) AND r1 = r5 ORDER BY r3, r4, r5, n LIMIT 5'
:explain2run2

-- IN equivalence to last key non-sort column
\set query 'SELECT r3, r4, n, r1, r5, r2 FROM r5n WHERE r1 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) AND r2 IN (6, 0, 5) AND r1 = r5 ORDER BY r3, r4, n LIMIT 5'
:explain2run2

-- IN equivalence to last non-key sort column
\set query 'SELECT r3, r4, r5, r1, n, r2 FROM r5n WHERE r1 IN (1, 2, 3, 4, 5, 6, 7, 8, 9) AND r2 IN (6, 0, 5) AND r1 = n ORDER BY r3, r4, r5, n LIMIT 5'
:explain2run2

-- =-var equivalence prefix
-- SAOP merge should not be used.
\set query 'SELECT r4, r5, n, r3, r1, r2 FROM r5n WHERE r3 IN (7, 8, 9) AND r1 = r2 ORDER BY r4, r5, n LIMIT 5'
:explain2

-- =-var equivalence suffix
-- SAOP merge should not be used.
\set query 'SELECT r4, r5, n, r1, r2, r3 FROM r5n WHERE r1 IN (7, 8, 9) AND r2 = r3 ORDER BY r4, r5, n LIMIT 5'
:explain2

-- OR clause
\set query 'SELECT r3, r4, r5, n, r1, r2 FROM r5n WHERE r2 IN (0, 1, 2, 3, 4, 5, 6) AND r1 IN (0, 1, 2) AND (r1 = 2 OR r1 IN (1, 2, 3)) ORDER BY r3, r4, r5, n LIMIT 5'
:explain2run2

-- Another OR clause
-- SAOP merge should not be used.
\set query 'SELECT r3, r4, r5, n, r2, r1 FROM r5n WHERE r2 IN (0, 1, 2, 3, 4, 5, 6) AND (r1 IN (0, 1, 2) OR r1 = 2 OR r1 IN (1, 2, 3)) ORDER BY r3, r4, r5, n LIMIT 5'
:explain2

-- Cross-type SAOP: compatible type
\set query 'SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 = ANY (ARRAY[0, 2, 4, 6, 8]::int8[]) ORDER BY r2, r3, r4, r5, n LIMIT 5'
:explain2run2

-- Cross-type SAOP: incompatible type
-- SAOP merge should not be used.
\set query 'SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 = ANY (ARRAY[0, 2, 4, 6, 8]::float[]) ORDER BY r2, r3, r4, r5, n LIMIT 5'
:explain2

-- Typecasted LHS
-- SAOP merge should not be used.
\set query 'SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1::text = ANY (ARRAY[0, 2, 4, 6, 8]::text[]) ORDER BY r2, r3, r4, r5, n LIMIT 5'
:explain2

-- ArrayExpr containing FuncExpr
-- SAOP merge should not be used.
\set query 'SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 IN (0, 1, random()::int) ORDER BY r2, r3, r4, r5, n LIMIT 5'
:explain2

-- ArrayExpr containing OpExpr
-- SAOP merge should not be used.
\set query 'SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 IN (1, 100 + random()::int) ORDER BY r2, r3, r4, r5, n LIMIT 5'
:explain2

-- ArrayExpr containing Param
-- SAOP merge should not be used.
\set query 'SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 IN (1, (SELECT count(*) FROM r5n)) ORDER BY r2, r3, r4, r5, n LIMIT 5'
:explain2

-- Row IN without constant table
-- TODO(#29032): this should use SAOP merge.
\set query 'SELECT r3, r4, r5, n, r1, r2 FROM r5n WHERE (r1, r2) IN ((1, 2), (3, 4)) ORDER BY r3, r4, r5, n LIMIT 5'
:explain2run2

-- Row IN with constant table
-- TODO(#29032): this should use SAOP merge.
\set query 'SELECT r3, r4, r5, n, r1, r2 FROM r5n WHERE row(r1, r2) IN (values (1, 2), (3, 4)) ORDER BY r3, r4, r5, n LIMIT 5'
:explain2run2

-- Subquery with ORDER inside
\set query 'SELECT r2, r3, r4, n FROM (SELECT r2, r3, r4, n FROM r5n WHERE r1 IN (0, 1, 2, 3) ORDER BY r2, r3, r4, n LIMIT 5) sub'
:explain2run2

-- Subquery with ORDER outside
\set query 'SELECT r2, r3, r4, n FROM (SELECT r2, r3, r4, n FROM r5n WHERE r1 IN (0, 1, 2, 3)) sub ORDER BY r2, r3, r4, n LIMIT 5'
:explain2run2

-- NULL in IN
-- TODO(#29073) after culling array, number of streams should be 3.
\set query 'SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 IN (1, 2, null, 3) ORDER BY r2, r3, r4, r5, n LIMIT 5'
:explain2run2

-- Only NULLs in IN
-- Third hint is to use SAOP merge as the second hint ends up using sort.
-- TODO(#29073): after culling array, maybe the third hint should not use SAOP
-- merge.
\set query 'SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 IN (null, null, null) ORDER BY r2, r3, r4, r5, n LIMIT 5'
\set hint3 '/*+Set(enable_sort off) Set(yb_max_saop_merge_streams 64)*/'
:explain3run3

-- Empty array
-- Third hint is to use SAOP merge as the second hint ends up using sort.
\set query 'SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 = ANY(''{}'') ORDER BY r2, r3, r4, r5, n LIMIT 5'
:explain3run3

-- Non-const in RHS (like var ref)
-- SAOP merge should not be used.
\set query 'SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 IN (1, r2, 2) ORDER BY r2, r3, r4, r5, n LIMIT 5'
:explain2

-- Large number of streams
\set query 'SELECT r4, r5, n, r1, r2, r3 FROM r5n WHERE r1 IN (0, 1, 2, 3) AND r2 IN (0, 1, 2, 3) AND r3 IN (0, 1, 2, 3) ORDER BY r4, r5, n LIMIT 5'
:explain2run2

-- Single IN hitting limit
-- SAOP merge should not be used.
\set on '/*+Set(yb_max_saop_merge_streams 5)*/'
\set query 'SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 IN (0, 2, 4, 6, 8, 10) ORDER BY r2, r3, r4, r5, n LIMIT 5'
:explain2

-- Double IN hitting limit
-- SAOP merge should not be used.
\set query 'SELECT r3, r4, r5, n, r1, r2 FROM r5n WHERE r1 IN (0, 2, 4) AND r2 IN (6, 8) ORDER BY r3, r4, r5, n LIMIT 5'
:explain2

-- Triple IN hitting limit before realizing limit could be satisfied with 0x
-- multiplier.
-- Third hint is to encourage use of SAOP merge like other similar empty array
-- test cases.
-- SAOP merge should not be used.
\set query 'SELECT r3, r4, r5, n, r1, r2 FROM r5n WHERE r1 IN (0, 2, 4) AND r2 IN (6, 8) AND r3 = ANY(''{}'') ORDER BY r4, r5, n LIMIT 5'
\set hint3 '/*+Set(enable_sort off) Set(yb_max_saop_merge_streams 5)*/'
:explain3

-- Triple IN avoiding limit because of 0x multiplier.
-- Third hint is to use SAOP merge as the second hint ends up using sort.
\set query 'SELECT r3, r4, r5, n, r1, r2 FROM r5n WHERE r1 IN (0, 2, 4) AND r2 = ANY(''{}'') AND r3 IN (6, 8) ORDER BY r4, r5, n LIMIT 5'
:explain3run3

-- Choose lowest cardinality IN
-- TODO(#20899): this test needs to be reconsidered when fixing this issue.
\set query 'SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 IN (1, 2, 3, 4, 5, 6) AND r1 IN (3, 4, 5, 6, 7) AND r1 IN (5, 6, 7, 8, 9, 10) ORDER BY r2, r3, r4, r5, n LIMIT 5'
:explain2run2

-- Duplicates in IN
-- TODO(#29073): after culling array, this should use SAOP merge.
\set query 'SELECT r2, r3, r4, r5, n, r1 FROM r5n WHERE r1 IN (1, 1, 1, 2, 3, 3, 4, 5, 5, 5, 1) ORDER BY r2, r3, r4, r5, n LIMIT 5'
:explain2run2

-- (Reset the limit change)
\set on '/*+Set(yb_max_saop_merge_streams 64)*/'

-- #30096: SAOP merge shouldn't be used in a parallel scan.
\set query 'SELECT * FROM r5n WHERE r1 IN (0, 2, 4) AND r2 IN (6, 8) ORDER BY r3, r4, r5'
\set hint3 '/*+Parallel(r5n 2) Set(yb_enable_parallel_scan_range_sharded true) Set(yb_parallel_range_rows 1) Set(yb_max_saop_merge_streams 0)*/'
\set hint4 '/*+Parallel(r5n 2) Set(yb_enable_parallel_scan_range_sharded true) Set(yb_parallel_range_rows 1) Set(yb_max_saop_merge_streams 64)*/'
:explain4

-- Same thing with backwards scan.
\set query 'SELECT * FROM r5n WHERE r1 IN (0, 2, 4) AND r2 IN (6, 8) ORDER BY r3 DESC, r4 DESC, r5 DESC'
:explain4

--
-- Secondary index
--
CREATE INDEX NONCONCURRENTLY ON r5n (r2 ASC, r3, r4, r5);

-- No order
-- SAOP merge should not be used.
\set query 'SELECT * FROM r5n WHERE r2 IN (0, 1, 2, 3) LIMIT 5'
:explain2

-- No limit
-- TODO(#29078): this likely should use SAOP merge.
\set query 'SELECT * FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY r3, r4, r5'
:explain2

-- Following queries send various numbers of requests/scan various number of rows
-- due to non-deterministic order of equal rows in merge sort, hide their DIST
\set explain 'EXPLAIN (ANALYZE, VERBOSE, COSTS OFF, SUMMARY OFF, TIMING OFF)'

-- Forward scan
\set query 'SELECT * FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY r3, r4, n LIMIT 5'
:explain2run2

-- Backward scan
\set query 'SELECT * FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY r3 DESC, r4 DESC, n LIMIT 5'
:explain2run2

-- Targets
\set query 'SELECT r5, 1, r5 FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY r3 DESC, r4 DESC, n LIMIT 5'
:explain2run2

-- (Reset the explain change)
\set explain 'EXPLAIN (ANALYZE, DIST, VERBOSE, COSTS OFF, SUMMARY OFF, TIMING OFF)'

-- Secondary index scan VS SAOP merge PK scan
\set query 'SELECT r2, r3, r4, n, r1 FROM r5n WHERE r1 IN (1, 2, 3, 4, 5) AND r2 = 4 ORDER BY r3, r4, n LIMIT 5'
-- When PgGate does not have to make a separate round trip to the secondary index, the secondary index scan
-- becomes viable alternative to the SAOP merge PK scan. It wins in this case, so we use the third hint to
-- force the use of the primary key.
\set hint3 '/*+IndexScan(r5n r5n_pkey) Set(yb_max_saop_merge_streams 64)*/'
:explain3run3

-- SAOP merge secondary index scan VS SAOP merge PK scan
-- Third hint is to use the secondary index as the second hint ends up using
-- the PK index.
\set query 'SELECT r3, r4, r5, n, r1, r2 FROM r5n WHERE r1 IN (1, 2, 3, 4) AND r2 IN (1, 2, 3, 4) ORDER BY r3, r4, r5, n LIMIT 5'
\set hint3 '/*+IndexScan(r5n r5n_r2_r3_r4_r5_idx) Set(yb_max_saop_merge_streams 64)*/'
:explain3run3

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
CREATE INDEX NONCONCURRENTLY ON r5n (r2 test_int8_ops ASC, r3, r4 test_numeric_ops, r5);

-- No order
-- SAOP merge should not be used.
\set query 'SELECT * FROM r5n WHERE r2 =#= ANY(ARRAY[0, 1, 2, 3]) LIMIT 5'
:explain2

-- Forward scan
-- TODO(#29383): the output is incorrect where it relies on DocDB order.
-- \set query 'SELECT * FROM r5n WHERE r2 =#= ANY(ARRAY[0, 1, 2, 3]) ORDER BY r3, r4 USING #<#, n LIMIT 5'
-- :explain2run2

-- Backward scan
-- TODO(#29383): the output is incorrect where it relies on DocDB order.
-- \set query 'SELECT * FROM r5n WHERE r2 =#= ANY(ARRAY[0, 1, 2, 3]) ORDER BY r3 DESC, r4 USING #>#, n LIMIT 5'
-- :explain2run2

-- (Drop this index)
DROP INDEX r5n_r2_r3_r4_r5_idx;

--
-- INCLUDE index
--
CREATE INDEX NONCONCURRENTLY ON r5n (r2 ASC, r3, r4, r5) INCLUDE (r1, n);

-- No order
-- SAOP merge should not be used.
\set query 'SELECT * FROM r5n WHERE r2 IN (0, 1, 2, 3) LIMIT 5'
:explain2

-- Following queries send various numbers of requests/scan various number of rows
-- due to non-deterministic order of equal rows in merge sort, hide their DIST
\set explain 'EXPLAIN (ANALYZE, VERBOSE, COSTS OFF, SUMMARY OFF, TIMING OFF)'

-- Forward scan
\set query 'SELECT * FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY r3, r4, n LIMIT 5'
:explain2run2

-- Backward scan
\set query 'SELECT * FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY r3 DESC, r4 DESC, n LIMIT 5'
:explain2run2

-- Targets
\set query 'SELECT n, 1, n FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY r3 DESC, r4 DESC, n LIMIT 5'
:explain2run2

-- (Reset the explain change)
\set explain 'EXPLAIN (ANALYZE, DIST, VERBOSE, COSTS OFF, SUMMARY OFF, TIMING OFF)'

-- Secondary index only scan VS SAOP merge PK scan
\set query 'SELECT r2, r3, r4, n, r1 FROM r5n WHERE r1 IN (1, 2, 3, 4, 5) ORDER BY r2, r3, r4, n LIMIT 5'
:explain2run2

-- (Drop this index)
DROP INDEX r5n_r2_r3_r4_r5_r1_n_idx;

--
-- Expression prefix secondary index
--
CREATE INDEX NONCONCURRENTLY ON r5n ((greatest(r2, r3, r4) - least(r2, r3, r4)) ASC, r2, r3, r4);

-- No order
-- SAOP merge should not be used.
\set query 'SELECT * FROM r5n WHERE (greatest(r2, r3, r4) - least(r2, r3, r4)) IN (0, 2) LIMIT 5'
:explain2

-- Forward scan
\set query 'SELECT * FROM r5n WHERE (greatest(r2, r3, r4) - least(r2, r3, r4)) IN (0, 2) ORDER BY r2, r3, r4, n LIMIT 5'
:explain2run2

-- Backward scan
\set query 'SELECT * FROM r5n WHERE (greatest(r2, r3, r4) - least(r2, r3, r4)) IN (0, 2) ORDER BY r2 DESC, r3 DESC, r4 DESC, n LIMIT 5'
:explain2run2

-- Targets
\set query 'SELECT (greatest(r2, r3, r4) - least(r2, r3, r4)), 1, (greatest(r2, r3, r4) - least(r2, r3, r4)) FROM r5n WHERE (greatest(r2, r3, r4) - least(r2, r3, r4)) IN (0, 2) ORDER BY r2 DESC, r3 DESC, r4 DESC, n LIMIT 5'
:explain2run2

-- Secondary index scan VS SAOP merge PK scan
-- Third hint is to use the PK index as the second hint ends up using the
-- expression index.
\set query 'SELECT (greatest(r2, r3, r4) - least(r2, r3, r4)), r2, r3, r4, n, r1 FROM r5n WHERE r1 IN (1, 2, 3, 4, 5) AND (greatest(r2, r3, r4) - least(r2, r3, r4)) = 4 ORDER BY r2, r3, r4, n LIMIT 5'
\set hint3 '/*+IndexScan(r5n r5n_pkey) Set(yb_max_saop_merge_streams 64)*/'
:explain3run3

-- SAOP merge secondary index scan VS SAOP merge PK scan
-- Third hint is to use the PK index as the second hint ends up using the
-- expression index.
\set query 'SELECT r2, r3, r4, n, r1, (greatest(r2, r3, r4) - least(r2, r3, r4)) FROM r5n WHERE r1 IN (1, 2, 3, 4) AND (greatest(r2, r3, r4) - least(r2, r3, r4)) IN (1, 2, 3, 4) ORDER BY r2, r3, r4, n LIMIT 5'
\set hint3 '/*+IndexScan(r5n r5n_pkey) Set(yb_max_saop_merge_streams 64)*/'
:explain3run3

-- (Drop this index)
DROP INDEX r5n_expr_r2_r3_r4_idx;

--
-- Expression suffix secondary index
-- Expressions in a secondary colocated index are useless for SAOP merge
--
CREATE INDEX NONCONCURRENTLY ON r5n (r2 ASC, r3 DESC, (-r4));

-- Forward scan
\set query 'SELECT * FROM r5n WHERE r2 IN (0, 2) ORDER BY r3 DESC, -r4, n LIMIT 5'
:explain2run2

-- Backward scan
\set query 'SELECT * FROM r5n WHERE r2 IN (0, 2) ORDER BY r3 ASC, -r4 DESC, n LIMIT 5'
:explain2run2

-- Targets
\set query 'SELECT -r4, 1, -r4 FROM r5n WHERE r2 IN (0, 2) ORDER BY r3 ASC, -r4 DESC, n LIMIT 5'
:explain2run2

-- (Drop this index)
DROP INDEX r5n_r2_r3_expr_idx;

--
-- Duplicate columns secondary index
--
CREATE INDEX NONCONCURRENTLY ON r5n (r2 ASC, (r3 + r4), r2 DESC, (r3 + r4), r2);

-- No order
-- SAOP merge should not be used.
\set query 'SELECT * FROM r5n WHERE r2 IN (0, 1, 2, 3) LIMIT 5'
:explain2

-- Following queries send various numbers of requests/scan various number of rows
-- due to non-deterministic order of equal rows in merge sort, hide their DIST
\set explain 'EXPLAIN (ANALYZE, VERBOSE, COSTS OFF, SUMMARY OFF, TIMING OFF)'

-- Forward scan
-- Order by an expression in the embedded index
-- SAOP merge should not be used.
\set query 'SELECT * FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY (r3 + r4), n LIMIT 5'
:explain2run2

-- Forward scan (v2)
-- SAOP merge is not used.
\set query 'SELECT * FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY (r3 + r4), r2 DESC, n LIMIT 5'
:explain2

-- Backward scan
-- Order by an expression in the embedded index
-- SAOP merge should not be used.
\set query 'SELECT * FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY (r3 + r4) DESC, n LIMIT 5'
:explain2run2

-- Backward scan (v2)
-- SAOP merge is not used.
\set query 'SELECT * FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY (r3 + r4) DESC, r2, n LIMIT 5'
:explain2

-- Targets
-- Order by an expression in the embedded index
-- SAOP merge should not be used.
\set query 'SELECT r5, 1, r5 FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY (r3 + r4) DESC, n LIMIT 5'
:explain2run2

-- Targets (v2)
-- SAOP merge is not used.
\set query 'SELECT r5, 1, r5 FROM r5n WHERE r2 IN (0, 1, 2, 3) ORDER BY (r3 + r4) DESC, r2, n LIMIT 5'
:explain2

-- (Reset the explain change)
\set explain 'EXPLAIN (ANALYZE, DIST, VERBOSE, COSTS OFF, SUMMARY OFF, TIMING OFF)'

-- (Drop this index)
DROP INDEX r5n_r2_expr_r21_expr1_r22_idx;

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
FROM unnest(ARRAY['/*+Set(yb_max_saop_merge_streams 0)*/',
                  '/*+Set(yb_max_saop_merge_streams 64)*/']) AS hint,
     unnest(ARRAY['Merge Sort Key',
                  'Merge Stream Key',
                  'Merge Streams',
                  'Merge Cond']) AS property;
