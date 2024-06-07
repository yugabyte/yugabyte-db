-- ================================================================
-- Setup the table
--

SELECT hll_set_output_version(1);

-- This test relies on a non-standard fixed sparse-to-compressed
-- threshold value.
--
SELECT hll_set_max_sparse(512);

DROP TABLE IF EXISTS test_eopzdzwz;

CREATE TABLE test_eopzdzwz (
    recno                       SERIAL,
    cardinality                 double precision,
    raw_value                   bigint,
    union_compressed_multiset   hll
);

-- Copy the CSV data into the table
--
\copy test_eopzdzwz (cardinality, raw_value, union_compressed_multiset) from sql/data/cumulative_add_sparse_edge.csv with csv header

SELECT COUNT(*) FROM test_eopzdzwz;

--  Test incremental adding.
SELECT curr.recno,
       curr.union_compressed_multiset,
       hll_add(prev.union_compressed_multiset, hll_hashval(curr.raw_value))
  FROM test_eopzdzwz prev, test_eopzdzwz curr
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
  FROM test_eopzdzwz prev, test_eopzdzwz curr
 WHERE curr.recno > 1
   AND curr.recno = prev.recno + 1
   AND curr.cardinality != 
       hll_cardinality(hll_add(prev.union_compressed_multiset,
                       hll_hashval(curr.raw_value)))
 ORDER BY curr.recno;

DROP TABLE test_eopzdzwz;

