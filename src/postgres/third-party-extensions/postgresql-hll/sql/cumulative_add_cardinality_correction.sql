-- ================================================================
-- Setup the table
--

set extra_float_digits=0;
SELECT hll_set_output_version(1);

-- This test relies on a non-standard fixed sparse-to-compressed
-- threshold value.
--
SELECT hll_set_max_sparse(0);

DROP TABLE IF EXISTS test_msgfjqhm;

CREATE TABLE test_msgfjqhm (
    recno                       SERIAL,
    cardinality                 double precision,
    raw_value                   bigint,
    union_compressed_multiset   hll
);

-- Copy the CSV data into the table
--
\copy test_msgfjqhm (cardinality, raw_value, union_compressed_multiset) from sql/data/cumulative_add_cardinality_correction.csv with csv header

SELECT COUNT(*) FROM test_msgfjqhm;

--  Test incremental adding.
SELECT curr.recno,
       curr.union_compressed_multiset,
       hll_add(prev.union_compressed_multiset, hll_hashval(curr.raw_value))
  FROM test_msgfjqhm prev, test_msgfjqhm curr
 WHERE curr.recno > 1
   AND curr.recno = prev.recno + 1
   AND curr.union_compressed_multiset != 
       hll_add(prev.union_compressed_multiset, hll_hashval(curr.raw_value))
 ORDER BY curr.recno;

--  Test cardinality of incremental adds.
SELECT curr.recno,
       curr.cardinality,
       hll_cardinality(hll_add(prev.union_compressed_multiset,
                       hll_hashval(curr.raw_value)))
  FROM test_msgfjqhm prev, test_msgfjqhm curr
 WHERE curr.recno > 1
   AND curr.recno = prev.recno + 1
   AND round(curr.cardinality::numeric, 10) != 
       round(hll_cardinality(hll_add(prev.union_compressed_multiset,
                             hll_hashval(curr.raw_value)))::numeric,
             10)
 ORDER BY curr.recno;

DROP TABLE test_msgfjqhm;

