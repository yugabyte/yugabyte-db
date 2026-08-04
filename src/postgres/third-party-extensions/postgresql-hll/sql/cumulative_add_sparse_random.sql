-- ================================================================
-- Setup the table
--

SELECT hll_set_output_version(1);

DROP TABLE IF EXISTS test_beelewuo;

CREATE TABLE test_beelewuo (
    recno                       SERIAL,
    cardinality                 double precision,
    raw_value                   bigint,
    union_compressed_multiset   hll
);

-- Copy the CSV data into the table
--
\copy test_beelewuo (cardinality, raw_value, union_compressed_multiset) from sql/data/cumulative_add_sparse_random.csv with csv header

SELECT COUNT(*) FROM test_beelewuo;

--  Test incremental adding.
SELECT curr.recno,
       curr.union_compressed_multiset,
       hll_add(prev.union_compressed_multiset, hll_hashval(curr.raw_value))
  FROM test_beelewuo prev, test_beelewuo curr
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
  FROM test_beelewuo prev, test_beelewuo curr
 WHERE curr.recno > 1
   AND curr.recno = prev.recno + 1
   AND curr.cardinality != 
       hll_cardinality(hll_add(prev.union_compressed_multiset,
                       hll_hashval(curr.raw_value)))
 ORDER BY curr.recno;

DROP TABLE test_beelewuo;

