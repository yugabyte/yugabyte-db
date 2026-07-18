-- ----------------------------------------------------------------
-- Scalar Operations with Out-Of-Band Values.
-- NULL, UNDEFINED, EMPTY, EXPLICIT, SPARSE
-- (Sparse and compressed are the same internally)
-- ----------------------------------------------------------------

set extra_float_digits=0;
SELECT hll_set_output_version(1);

-- Scalar Cardinality ----

SELECT hll_cardinality(NULL);

SELECT hll_cardinality(E'\\x108b7f'::hll);

SELECT hll_cardinality(E'\\x118b7f'::hll);

SELECT hll_cardinality(E'\\x128b7f1111111111111111'::hll);

SELECT hll_cardinality(E'\\x138b7f0001'::hll);


-- Scalar Union ----

-- NULL

SELECT hll_union(NULL, NULL);

SELECT hll_union(NULL, E'\\x108b7f'::hll);

SELECT hll_union(NULL, E'\\x118b7f'::hll);

SELECT hll_union(NULL, E'\\x128b7f1111111111111111'::hll);

SELECT hll_union(NULL, E'\\x138b7f0001'::hll);

-- UNDEFINED

SELECT hll_union(E'\\x108b7f'::hll, NULL);

SELECT hll_union(E'\\x108b7f'::hll, E'\\x108b7f'::hll);

SELECT hll_union(E'\\x108b7f'::hll, E'\\x118b7f'::hll);

SELECT hll_union(E'\\x108b7f'::hll, E'\\x128b7f1111111111111111'::hll);

SELECT hll_union(E'\\x108b7f'::hll, E'\\x138b7f0001'::hll);

-- EMPTY

SELECT hll_union(E'\\x118b7f'::hll, NULL);

SELECT hll_union(E'\\x118b7f'::hll, E'\\x108b7f'::hll);

SELECT hll_union(E'\\x118b7f'::hll, E'\\x118b7f'::hll);

SELECT hll_union(E'\\x118b7f'::hll, E'\\x128b7f1111111111111111'::hll);

SELECT hll_union(E'\\x118b7f'::hll, E'\\x138b7f0001'::hll);

-- EXPLICIT

SELECT hll_union(E'\\x128b7f1111111111111111'::hll, NULL);

SELECT hll_union(E'\\x128b7f1111111111111111'::hll, E'\\x108b7f'::hll);

SELECT hll_union(E'\\x128b7f1111111111111111'::hll, E'\\x118b7f'::hll);

SELECT hll_union(E'\\x128b7f1111111111111111'::hll, E'\\x128b7f1111111111111111'::hll);

SELECT hll_union(E'\\x128b7f1111111111111111'::hll, E'\\x138b7f0001'::hll);

-- SPARSE

SELECT hll_union(E'\\x138b7f0001'::hll, NULL);

SELECT hll_union(E'\\x138b7f0001'::hll, E'\\x108b7f'::hll);

SELECT hll_union(E'\\x138b7f0001'::hll, E'\\x118b7f'::hll);

SELECT hll_union(E'\\x138b7f0001'::hll, E'\\x128b7f1111111111111111'::hll);

SELECT hll_union(E'\\x138b7f0001'::hll, E'\\x138b7f0001'::hll);

