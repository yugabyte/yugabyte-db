-- ----------------------------------------------------------------
-- Regression tests for union operator.
-- ----------------------------------------------------------------

SELECT hll_set_output_version(1);

SELECT hll_empty(11,5,256,1) || hll_empty(11,5,256,1);

SELECT hll_empty(11,5,256,1) || E'\\x128b498895a3f5af28cafe'::hll;

SELECT E'\\x128b498895a3f5af28cafe'::hll || E'\\x128b498895a3f5af28cafe'::hll;

SELECT E'\\x128b498895a3f5af28cafe'::hll || E'\\x128b49da0ce907e4355b60'::hll;

SELECT hll_empty(11,5,256,1) || hll_hash_integer(1,0);

SELECT hll_empty(11,5,256,1)
       || hll_hash_integer(1,0)
       || hll_hash_integer(2,0)
       || hll_hash_integer(3,0);

SELECT hll_hash_integer(1,0) || hll_empty(11,5,256,1);

SELECT hll_hash_integer(1,0) || hll_empty(11,5,256,1) || hll_hash_integer(2,0);
