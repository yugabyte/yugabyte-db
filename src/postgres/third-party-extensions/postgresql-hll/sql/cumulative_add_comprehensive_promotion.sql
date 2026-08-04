-- ================================================================
-- Setup the table
--

set extra_float_digits=0;
SELECT hll_set_output_version(1);

-- This test relies on a non-standard fixed sparse-to-compressed
-- threshold value.
--
SELECT hll_set_max_sparse(512);

DROP TABLE IF EXISTS test_ptwysrqk;

CREATE TABLE test_ptwysrqk (
    recno                       SERIAL,
    cardinality                 double precision,
    raw_value                   bigint,
    union_compressed_multiset   hll
);

-- Copy the CSV data into the table
--
\copy test_ptwysrqk (cardinality, raw_value, union_compressed_multiset) from sql/data/cumulative_add_comprehensive_promotion.csv with csv header

SELECT COUNT(*) FROM test_ptwysrqk;

--  Test incremental adding.
SELECT curr.recno,
       curr.union_compressed_multiset,
       hll_add(prev.union_compressed_multiset, curr.raw_value::hll_hashval)
  FROM test_ptwysrqk prev, test_ptwysrqk curr
 WHERE curr.recno > 1
   AND curr.recno = prev.recno + 1
   AND curr.union_compressed_multiset != 
       hll_add(prev.union_compressed_multiset, curr.raw_value::hll_hashval)
 ORDER BY curr.recno;

--  Test cardinality of incremental adds.
SELECT curr.recno,
       curr.cardinality,
       hll_cardinality(hll_add(prev.union_compressed_multiset,
                       curr.raw_value::hll_hashval))
  FROM test_ptwysrqk prev, test_ptwysrqk curr
 WHERE curr.recno > 1
   AND curr.recno = prev.recno + 1
   AND round(curr.cardinality::numeric, 10) != 
       round(hll_cardinality(hll_add(prev.union_compressed_multiset,
                             curr.raw_value::hll_hashval))::numeric,
             10)
 ORDER BY curr.recno;

DROP TABLE test_ptwysrqk;

