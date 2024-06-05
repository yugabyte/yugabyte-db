-- Setup the table
--

SELECT hll_set_output_version(1);

DROP TABLE IF EXISTS test_rhswkjtc;

CREATE TABLE test_rhswkjtc (
    recno                       SERIAL,
    cardinality                 double precision,
    compressed_multiset         hll,
    union_cardinality           double precision,
    union_compressed_multiset   hll
);

-- Copy the CSV data into the table
--
\copy test_rhswkjtc (cardinality,compressed_multiset,union_cardinality,union_compressed_multiset) from sql/data/cumulative_union_comprehensive.csv with csv header

SELECT COUNT(*) FROM test_rhswkjtc;

-- Cardinality of incremental multisets
--
SELECT recno,
       cardinality,
       hll_cardinality(compressed_multiset)
  FROM test_rhswkjtc
 WHERE cardinality != hll_cardinality(compressed_multiset);

-- Cardinality of unioned multisets
--
SELECT recno,
       union_cardinality,
       hll_cardinality(union_compressed_multiset)
  FROM test_rhswkjtc
 WHERE union_cardinality != hll_cardinality(union_compressed_multiset);

-- Test union of incremental multiset.
--
SELECT curr.recno,
       curr.union_compressed_multiset,
       hll_union(curr.compressed_multiset, prev.union_compressed_multiset) 
  FROM test_rhswkjtc prev, test_rhswkjtc curr
 WHERE curr.recno > 1
   AND curr.recno = prev.recno + 1
   AND curr.union_compressed_multiset != 
       hll_union(curr.compressed_multiset, prev.union_compressed_multiset);

-- Test cardinality of union of incremental multiset.
--
SELECT curr.recno,
       curr.union_cardinality,
       hll_cardinality(hll_union(curr.compressed_multiset,
                                 prev.union_compressed_multiset))
  FROM test_rhswkjtc prev, test_rhswkjtc curr
 WHERE curr.recno > 1
   AND curr.recno = prev.recno + 1
   AND curr.union_cardinality != 
       hll_cardinality(hll_union(curr.compressed_multiset,
                                 prev.union_compressed_multiset));

-- Test aggregate accumulation
--
SELECT v1.recno,
       v1.union_compressed_multiset,
       (select hll_union_agg(compressed_multiset)
          from test_rhswkjtc
         where recno <= v1.recno) as hll_union_agg
  FROM test_rhswkjtc v1
 WHERE v1.union_compressed_multiset !=
       (select hll_union_agg(compressed_multiset)
          from test_rhswkjtc
         where recno <= v1.recno);

-- Test aggregate accumulation with cardinality
--
SELECT v1.recno,
       ceil(v1.union_cardinality),
       (select ceiling(hll_cardinality(hll_union_agg(compressed_multiset)))
          from test_rhswkjtc
         where recno <= v1.recno) as ceiling
  FROM test_rhswkjtc v1
 WHERE ceil(v1.union_cardinality) !=
       (select ceiling(hll_cardinality(hll_union_agg(compressed_multiset)))
          from test_rhswkjtc
         where recno <= v1.recno);

-- Window function which calls final functions more then once.
SELECT
    recno,
    hll_union_agg(test_rhswkjtc.compressed_multiset) OVER prev30
FROM test_rhswkjtc
window prev30 as (order by recno asc rows 29 preceding);

DROP TABLE test_rhswkjtc;
