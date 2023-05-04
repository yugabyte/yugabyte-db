-- ================================================================
-- Setup the table
--

SELECT hll_set_output_version(1);

DROP TABLE IF EXISTS test_seznjqbb;

CREATE TABLE test_seznjqbb (
    recno                       SERIAL,
    seed                        integer,
    pre_hash_long               bigint,
    post_hash_long              bigint
);

\copy test_seznjqbb (seed, pre_hash_long, post_hash_long) from sql/data/murmur_bigint.csv with csv header

SELECT COUNT(*) FROM test_seznjqbb;

SELECT recno, post_hash_long, hll_hash_bigint(pre_hash_long, seed)
  FROM test_seznjqbb
 WHERE hll_hashval(post_hash_long) != hll_hash_bigint(pre_hash_long, seed)
 ORDER BY recno;

DROP TABLE test_seznjqbb;
