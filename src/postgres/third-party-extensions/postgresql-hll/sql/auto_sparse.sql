-- ----------------------------------------------------------------
-- Tests automatic sparse to compressed threshold assignment.
-- ----------------------------------------------------------------

SELECT hll_set_output_version(1);

-- Empty

SELECT hll_empty(5,5,-1,1);

-- Explicit

SELECT hll_add(hll_empty(5,5,-1,1),
           hll_hash_integer(1,0));

SELECT hll_add(
       hll_add(hll_empty(5,5,-1,1),
           hll_hash_integer(2,0)),
           hll_hash_integer(1,0));

-- Sparse

SELECT hll_add(
       hll_add(
       hll_add(hll_empty(5,5,-1,1),
           hll_hash_integer(3,0)),
           hll_hash_integer(2,0)),
           hll_hash_integer(1,0));


-- Sparse, has 15 filled
SELECT hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(hll_empty(5,5,-1,1),
           hll_hash_integer(17,0)),
           hll_hash_integer(15,0)),
           hll_hash_integer(14,0)),
           hll_hash_integer(13,0)),
           hll_hash_integer(12,0)),
           hll_hash_integer(11,0)),
           hll_hash_integer(10,0)),
           hll_hash_integer(9,0)),
           hll_hash_integer(8,0)),
           hll_hash_integer(7,0)),
           hll_hash_integer(6,0)),
           hll_hash_integer(5,0)),
           hll_hash_integer(4,0)),
           hll_hash_integer(3,0)),
           hll_hash_integer(2,0)),
           hll_hash_integer(1,0));

-- Compressed, has 16 filled
SELECT hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(hll_empty(5,5,-1,1),
           hll_hash_integer(20,0)),
           hll_hash_integer(17,0)),
           hll_hash_integer(15,0)),
           hll_hash_integer(14,0)),
           hll_hash_integer(13,0)),
           hll_hash_integer(12,0)),
           hll_hash_integer(11,0)),
           hll_hash_integer(10,0)),
           hll_hash_integer(9,0)),
           hll_hash_integer(8,0)),
           hll_hash_integer(7,0)),
           hll_hash_integer(6,0)),
           hll_hash_integer(5,0)),
           hll_hash_integer(4,0)),
           hll_hash_integer(3,0)),
           hll_hash_integer(2,0)),
           hll_hash_integer(1,0));

-- Same with fixed sparse of 128

SELECT hll_set_max_sparse(128);

SELECT hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(
       hll_add(hll_empty(5,5,-1,1),
           hll_hash_integer(20,0)),
           hll_hash_integer(17,0)),
           hll_hash_integer(15,0)),
           hll_hash_integer(14,0)),
           hll_hash_integer(13,0)),
           hll_hash_integer(12,0)),
           hll_hash_integer(11,0)),
           hll_hash_integer(10,0)),
           hll_hash_integer(9,0)),
           hll_hash_integer(8,0)),
           hll_hash_integer(7,0)),
           hll_hash_integer(6,0)),
           hll_hash_integer(5,0)),
           hll_hash_integer(4,0)),
           hll_hash_integer(3,0)),
           hll_hash_integer(2,0)),
           hll_hash_integer(1,0));

-- Notice that sparse result is not shorter than the compressed
-- result.
