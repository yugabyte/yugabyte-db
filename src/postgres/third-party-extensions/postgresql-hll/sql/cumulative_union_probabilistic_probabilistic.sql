-- Setup the table
--

set extra_float_digits=0;
SELECT hll_set_output_version(1);

DROP TABLE IF EXISTS test_mpuahgwy;

CREATE TABLE test_mpuahgwy (
    recno                       SERIAL,
    cardinality                 double precision,
    compressed_multiset         hll,
    union_cardinality           double precision,
    union_compressed_multiset   hll
);

-- Copy the CSV data into the table
--
\copy test_mpuahgwy (cardinality,compressed_multiset,union_cardinality,union_compressed_multiset) from sql/data/cumulative_union_probabilistic_probabilistic.csv with csv header

SELECT COUNT(*) FROM test_mpuahgwy;

-- Cardinality of incremental multisets
--
SELECT recno,
       cardinality,
       hll_cardinality(compressed_multiset)
  FROM test_mpuahgwy
 WHERE cardinality != hll_cardinality(compressed_multiset)
 ORDER BY recno;

-- Cardinality of unioned multisets
--
SELECT recno,
       union_cardinality,
       hll_cardinality(union_compressed_multiset)
  FROM test_mpuahgwy
 WHERE union_cardinality != hll_cardinality(union_compressed_multiset)
 ORDER BY recno;

-- Test union of incremental multiset.
--
SELECT curr.recno,
       curr.union_compressed_multiset,
       hll_union(curr.compressed_multiset, prev.union_compressed_multiset) 
  FROM test_mpuahgwy prev, test_mpuahgwy curr
 WHERE curr.recno > 1
   AND curr.recno = prev.recno + 1
   AND curr.union_compressed_multiset != 
       hll_union(curr.compressed_multiset, prev.union_compressed_multiset)
 ORDER BY curr.recno;

-- Test cardinality of union of incremental multiset.
--
SELECT curr.recno,
       curr.union_cardinality,
       hll_cardinality(hll_union(curr.compressed_multiset,
                                 prev.union_compressed_multiset))
  FROM test_mpuahgwy prev, test_mpuahgwy curr
 WHERE curr.recno > 1
   AND curr.recno = prev.recno + 1
   AND curr.union_cardinality != 
       hll_cardinality(hll_union(curr.compressed_multiset,
                                 prev.union_compressed_multiset))
 ORDER BY curr.recno;

-- Test aggregate accumulation
--
SELECT v1.recno,
       v1.union_compressed_multiset,
       (select hll_union_agg(compressed_multiset)
          from test_mpuahgwy
         where recno <= v1.recno) as hll_union_agg
  FROM test_mpuahgwy v1
 WHERE v1.union_compressed_multiset !=
       (select hll_union_agg(compressed_multiset)
          from test_mpuahgwy
         where recno <= v1.recno)
 ORDER BY v1.recno;

-- Test aggregate accumulation with cardinality
--
SELECT v1.recno,
       ceil(v1.union_cardinality),
       (select ceiling(hll_cardinality(hll_union_agg(compressed_multiset)))
          from test_mpuahgwy
         where recno <= v1.recno) as ceiling
  FROM test_mpuahgwy v1
 WHERE ceil(v1.union_cardinality) !=
       (select ceiling(hll_cardinality(hll_union_agg(compressed_multiset)))
          from test_mpuahgwy
         where recno <= v1.recno)
 ORDER BY v1.recno;

DROP TABLE test_mpuahgwy;

