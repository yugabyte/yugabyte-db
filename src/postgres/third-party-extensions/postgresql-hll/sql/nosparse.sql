-- ----------------------------------------------------------------
-- Regression tests for explicit threshold control.
-- ----------------------------------------------------------------

SELECT hll_set_output_version(1);

-- Should display sparseon = 1
SELECT hll_print(hll_empty(12,5,0,1));

-- Should display sparseon = 0
SELECT hll_print(hll_empty(12,5,0,0));

-- Should become sparse
SELECT hll_add(hll_empty(12,5,0,1), hll_hash_integer(1,0));

-- Should go straight to compressed
SELECT hll_add(hll_empty(12,5,0,0), hll_hash_integer(1,0));
