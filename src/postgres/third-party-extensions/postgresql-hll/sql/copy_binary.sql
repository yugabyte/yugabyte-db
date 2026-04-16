SELECT hll_set_output_version(1);

DROP TABLE IF EXISTS test_binary;

CREATE TABLE test_binary (id SERIAL, v1 hll);

INSERT INTO test_binary(id,v1) VALUES (1, hll_empty() || hll_hash_text('A'));

SELECT hll_cardinality(v1) FROM test_binary;

\COPY test_binary TO 'binary.dat' WITH (FORMAT "binary")

DELETE FROM test_binary;

SELECT hll_cardinality(v1) FROM test_binary;

\COPY test_binary FROM 'binary.dat' WITH (FORMAT "binary")

SELECT hll_cardinality(v1) FROM test_binary;

DROP TABLE test_binary;
