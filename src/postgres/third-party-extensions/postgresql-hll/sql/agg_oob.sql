-- ----------------------------------------------------------------
-- Aggregate Operations with Out-Of-Band Values.
-- NULL, UNDEFINED, EMPTY, EXPLICIT, SPARSE
-- (Sparse and compressed are the same internally)
-- ----------------------------------------------------------------

SELECT hll_set_output_version(1);

DROP TABLE IF EXISTS test_wdflbzfx;

CREATE TABLE test_wdflbzfx (
    recno    SERIAL,
    v1	     hll
);

INSERT INTO test_wdflbzfx (recno, v1) VALUES
-- NULL --
(0, NULL),
(1, NULL),
(2, NULL),
(3, NULL),
-- UNDEFINED --
(4, E'\\x108b7f'),
(5, E'\\x108b7f'),
(6, E'\\x108b7f'),
(7, E'\\x108b7f'),
-- EMPTY --
(8, E'\\x118b7f'),
(9, E'\\x118b7f'),
(10, E'\\x118b7f'),
(11, E'\\x118b7f'),
-- EXPLICIT --
(12, E'\\x128b7f1111111111111111'),
(13, E'\\x128b7f2222222222222222'),
(14, E'\\x128b7f3333333333333333'),
(15, E'\\x128b7f4444444444444444'),
-- SPARSE --
(16, E'\\x138b7f0001'),
(17, E'\\x138b7f0022'),
(18, E'\\x138b7f0041'),
(19, E'\\x138b7f0061');

-- ----------------------------------------------------------------
-- Aggregate Union
-- ----------------------------------------------------------------

-- No rows selected
SELECT hll_union_agg(v1) FROM test_wdflbzfx WHERE recno > 100;

-- NULLs
SELECT hll_union_agg(v1) FROM test_wdflbzfx WHERE recno IN (0, 1, 2, 3);

-- UNDEFINED
SELECT hll_union_agg(v1) FROM test_wdflbzfx WHERE recno IN (4, 5, 6, 7);

-- UNDEFINED and NULL
SELECT hll_union_agg(v1) FROM test_wdflbzfx WHERE recno IN (4, 5, 6, 7)
                                               OR recno IN (0, 1, 2, 3);

-- EMPTY
SELECT hll_union_agg(v1) FROM test_wdflbzfx WHERE recno IN (8, 9, 10, 11);

-- EMPTY and NULL
SELECT hll_union_agg(v1) FROM test_wdflbzfx WHERE recno IN (8, 9, 10, 11)
                                               OR recno IN (0, 1, 2, 3);

-- EMPTY and UNDEFINED
SELECT hll_union_agg(v1) FROM test_wdflbzfx WHERE recno IN (8, 9, 10, 11)
                                               OR recno IN (4, 5, 6, 7);

-- EMPTY, UNDEFINED and NULL
SELECT hll_union_agg(v1) FROM test_wdflbzfx WHERE recno IN (8, 9, 10, 11)
                                               OR recno IN (4, 5, 6, 7)
                                               OR recno IN (0, 1, 2, 3);

-- EXPLICIT
SELECT hll_union_agg(v1) FROM test_wdflbzfx WHERE recno IN (12, 13, 14, 15);

-- EXPLICIT and NULL
SELECT hll_union_agg(v1) FROM test_wdflbzfx WHERE recno IN (12, 13, 14, 15)
                                               OR recno IN (0, 1, 2, 3);

-- EXPLICIT and UNDEFINED
SELECT hll_union_agg(v1) FROM test_wdflbzfx WHERE recno IN (12, 13, 14, 15)
                                               OR recno IN (4, 5, 6, 7);

-- EXPLICIT, UNDEFINED and NULL
SELECT hll_union_agg(v1) FROM test_wdflbzfx WHERE recno IN (12, 13, 14, 15)
                                               OR recno IN (4, 5, 6, 7)
                                               OR recno IN (0, 1, 2, 3);

-- EXPLICIT and EMPTY
SELECT hll_union_agg(v1) FROM test_wdflbzfx WHERE recno IN (12, 13, 14, 15)
                                               OR recno IN (8, 9, 10, 11);

-- EXPLICIT, EMPTY and NULL
SELECT hll_union_agg(v1) FROM test_wdflbzfx WHERE recno IN (12, 13, 14, 15)
                                               OR recno IN (8, 9, 10, 11)
                                               OR recno IN (0, 1, 2, 3);

-- EXPLICIT, EMPTY and UNDEFINED
SELECT hll_union_agg(v1) FROM test_wdflbzfx WHERE recno IN (12, 13, 14, 15)
                                               OR recno IN (8, 9, 10, 11)
                                               OR recno IN (4, 5, 6, 7);

-- EXPLICIT, EMPTY, UNDEFINED and NULL
SELECT hll_union_agg(v1) FROM test_wdflbzfx WHERE recno IN (12, 13, 14, 15)
                                               OR recno IN (8, 9, 10, 11)
                                               OR recno IN (4, 5, 6, 7)
                                               OR recno IN (0, 1, 2, 3);

-- Don't feel like a full sparse/explicit permuatation is adding
-- anything here ... just replace explicit w/ sparse.

-- SPARSE
SELECT hll_union_agg(v1) FROM test_wdflbzfx WHERE recno IN (16, 17, 18, 19);

-- SPARSE and NULL
SELECT hll_union_agg(v1) FROM test_wdflbzfx WHERE recno IN (16, 17, 18, 19)
                                               OR recno IN (0, 1, 2, 3);

-- SPARSE and UNDEFINED
SELECT hll_union_agg(v1) FROM test_wdflbzfx WHERE recno IN (16, 17, 18, 19)
                                               OR recno IN (4, 5, 6, 7);

-- SPARSE, UNDEFINED and NULL
SELECT hll_union_agg(v1) FROM test_wdflbzfx WHERE recno IN (16, 17, 18, 19)
                                               OR recno IN (4, 5, 6, 7)
                                               OR recno IN (0, 1, 2, 3);

-- SPARSE and EMPTY
SELECT hll_union_agg(v1) FROM test_wdflbzfx WHERE recno IN (16, 17, 18, 19)
                                               OR recno IN (8, 9, 10, 11);

-- SPARSE, EMPTY and NULL
SELECT hll_union_agg(v1) FROM test_wdflbzfx WHERE recno IN (16, 17, 18, 19)
                                               OR recno IN (8, 9, 10, 11)
                                               OR recno IN (0, 1, 2, 3);

-- SPARSE, EMPTY and UNDEFINED
SELECT hll_union_agg(v1) FROM test_wdflbzfx WHERE recno IN (16, 17, 18, 19)
                                               OR recno IN (8, 9, 10, 11)
                                               OR recno IN (4, 5, 6, 7);

-- SPARSE, EMPTY, UNDEFINED and NULL
SELECT hll_union_agg(v1) FROM test_wdflbzfx WHERE recno IN (16, 17, 18, 19)
                                               OR recno IN (8, 9, 10, 11)
                                               OR recno IN (4, 5, 6, 7)
                                               OR recno IN (0, 1, 2, 3);

-- ----------------------------------------------------------------
-- Aggregate Cardinality
-- ----------------------------------------------------------------


-- No rows selected
SELECT ceiling(hll_cardinality(hll_union_agg(v1)))
                      FROM test_wdflbzfx WHERE recno > 100;

-- NULLs
SELECT ceiling(hll_cardinality(hll_union_agg(v1)))
                      FROM test_wdflbzfx WHERE recno IN (0, 1, 2, 3);

-- UNDEFINED
SELECT ceiling(hll_cardinality(hll_union_agg(v1)))
                      FROM test_wdflbzfx WHERE recno IN (4, 5, 6, 7);

-- UNDEFINED and NULL
SELECT ceiling(hll_cardinality(hll_union_agg(v1)))
                      FROM test_wdflbzfx WHERE recno IN (4, 5, 6, 7)
                                            OR recno IN (0, 1, 2, 3);

-- EMPTY
SELECT ceiling(hll_cardinality(hll_union_agg(v1)))
                      FROM test_wdflbzfx WHERE recno IN (8, 9, 10, 11);

-- EMPTY and NULL
SELECT ceiling(hll_cardinality(hll_union_agg(v1)))
                      FROM test_wdflbzfx WHERE recno IN (8, 9, 10, 11)
                                            OR recno IN (0, 1, 2, 3);

-- EMPTY and UNDEFINED
SELECT ceiling(hll_cardinality(hll_union_agg(v1)))
                      FROM test_wdflbzfx WHERE recno IN (8, 9, 10, 11)
                                            OR recno IN (4, 5, 6, 7);

-- EMPTY, UNDEFINED and NULL
SELECT ceiling(hll_cardinality(hll_union_agg(v1)))
                      FROM test_wdflbzfx WHERE recno IN (8, 9, 10, 11)
                                            OR recno IN (4, 5, 6, 7)
                                            OR recno IN (0, 1, 2, 3);

-- EXPLICIT
SELECT ceiling(hll_cardinality(hll_union_agg(v1)))
                      FROM test_wdflbzfx WHERE recno IN (12, 13, 14, 15);

-- EXPLICIT and NULL
SELECT ceiling(hll_cardinality(hll_union_agg(v1)))
                      FROM test_wdflbzfx WHERE recno IN (12, 13, 14, 15)
                                            OR recno IN (0, 1, 2, 3);

-- EXPLICIT and UNDEFINED
SELECT ceiling(hll_cardinality(hll_union_agg(v1)))
                      FROM test_wdflbzfx WHERE recno IN (12, 13, 14, 15)
                                            OR recno IN (4, 5, 6, 7);

-- EXPLICIT, UNDEFINED and NULL
SELECT ceiling(hll_cardinality(hll_union_agg(v1)))
                      FROM test_wdflbzfx WHERE recno IN (12, 13, 14, 15)
                                            OR recno IN (4, 5, 6, 7)
                                            OR recno IN (0, 1, 2, 3);

-- EXPLICIT and EMPTY
SELECT ceiling(hll_cardinality(hll_union_agg(v1)))
                      FROM test_wdflbzfx WHERE recno IN (12, 13, 14, 15)
                                            OR recno IN (8, 9, 10, 11);

-- EXPLICIT, EMPTY and NULL
SELECT ceiling(hll_cardinality(hll_union_agg(v1)))
                      FROM test_wdflbzfx WHERE recno IN (12, 13, 14, 15)
                                            OR recno IN (8, 9, 10, 11)
                                            OR recno IN (0, 1, 2, 3);

-- EXPLICIT, EMPTY and UNDEFINED
SELECT ceiling(hll_cardinality(hll_union_agg(v1)))
                      FROM test_wdflbzfx WHERE recno IN (12, 13, 14, 15)
                                            OR recno IN (8, 9, 10, 11)
                                            OR recno IN (4, 5, 6, 7);

-- EXPLICIT, EMPTY, UNDEFINED and NULL
SELECT ceiling(hll_cardinality(hll_union_agg(v1)))
                      FROM test_wdflbzfx WHERE recno IN (12, 13, 14, 15)
                                            OR recno IN (8, 9, 10, 11)
                                            OR recno IN (4, 5, 6, 7)
                                            OR recno IN (0, 1, 2, 3);

-- Don't feel like a full sparse/explicit permuatation is adding
-- anything here ... just replace explicit w/ sparse.

-- SPARSE
SELECT ceiling(hll_cardinality(hll_union_agg(v1)))
                      FROM test_wdflbzfx WHERE recno IN (16, 17, 18, 19);

-- SPARSE and NULL
SELECT ceiling(hll_cardinality(hll_union_agg(v1)))
                      FROM test_wdflbzfx WHERE recno IN (16, 17, 18, 19)
                                            OR recno IN (0, 1, 2, 3);

-- SPARSE and UNDEFINED
SELECT ceiling(hll_cardinality(hll_union_agg(v1)))
                      FROM test_wdflbzfx WHERE recno IN (16, 17, 18, 19)
                                            OR recno IN (4, 5, 6, 7);

-- SPARSE, UNDEFINED and NULL
SELECT ceiling(hll_cardinality(hll_union_agg(v1)))
                      FROM test_wdflbzfx WHERE recno IN (16, 17, 18, 19)
                                            OR recno IN (4, 5, 6, 7)
                                            OR recno IN (0, 1, 2, 3);

-- SPARSE and EMPTY
SELECT ceiling(hll_cardinality(hll_union_agg(v1)))
                      FROM test_wdflbzfx WHERE recno IN (16, 17, 18, 19)
                                            OR recno IN (8, 9, 10, 11);

-- SPARSE, EMPTY and NULL
SELECT ceiling(hll_cardinality(hll_union_agg(v1)))
                      FROM test_wdflbzfx WHERE recno IN (16, 17, 18, 19)
                                            OR recno IN (8, 9, 10, 11)
                                            OR recno IN (0, 1, 2, 3);

-- SPARSE, EMPTY and UNDEFINED
SELECT ceiling(hll_cardinality(hll_union_agg(v1)))
                      FROM test_wdflbzfx WHERE recno IN (16, 17, 18, 19)
                                            OR recno IN (8, 9, 10, 11)
                                            OR recno IN (4, 5, 6, 7);

-- SPARSE, EMPTY, UNDEFINED and NULL
SELECT ceiling(hll_cardinality(hll_union_agg(v1)))
                      FROM test_wdflbzfx WHERE recno IN (16, 17, 18, 19)
                                            OR recno IN (8, 9, 10, 11)
                                            OR recno IN (4, 5, 6, 7)
                                            OR recno IN (0, 1, 2, 3);


DROP TABLE IF EXISTS test_wdflbzfx;
