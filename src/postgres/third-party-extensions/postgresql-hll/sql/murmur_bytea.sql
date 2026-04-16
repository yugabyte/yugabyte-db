-- ================================================================
-- Setup the table
--

SELECT hll_set_output_version(1);

DROP TABLE IF EXISTS test_qfwzdmoy;

CREATE TABLE test_qfwzdmoy (
    recno                       SERIAL,
    seed                        integer,
    pre_hash_long               bytea,
    post_hash_long              bigint
);

\copy test_qfwzdmoy (seed, pre_hash_long, post_hash_long) from sql/data/murmur_bytea.csv with csv header

SELECT COUNT(*) FROM test_qfwzdmoy;

SELECT recno, post_hash_long, hll_hash_bytea(pre_hash_long, seed)
  FROM test_qfwzdmoy
 WHERE hll_hashval(post_hash_long) != hll_hash_bytea(pre_hash_long, seed)
 ORDER BY recno;

DROP TABLE test_qfwzdmoy;
