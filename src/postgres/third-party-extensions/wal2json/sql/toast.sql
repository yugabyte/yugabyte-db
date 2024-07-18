\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;
SET extra_float_digits = 0;

DROP TABLE IF EXISTS xpto;
DROP SEQUENCE IF EXISTS xpto_rand_seq;

CREATE SEQUENCE xpto_rand_seq START 11 INCREMENT 997;
CREATE TABLE xpto (
id				serial primary key,
toasted_col1	text,
rand1			float8 DEFAULT nextval('xpto_rand_seq'),
toasted_col2	text,
rand2			float8 DEFAULT nextval('xpto_rand_seq')
);

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

-- uncompressed external toast data
INSERT INTO xpto (toasted_col1, toasted_col2) SELECT string_agg(g.i::text, ''), string_agg((g.i*2)::text, '') FROM generate_series(1, 2000) g(i);

-- compressed external toast data
INSERT INTO xpto (toasted_col2) SELECT repeat(string_agg(to_char(g.i, 'FM0000'), ''), 50) FROM generate_series(1, 500) g(i);

-- update of existing column
UPDATE xpto SET toasted_col1 = (SELECT string_agg(g.i::text, '') FROM generate_series(1, 2000) g(i)) WHERE id = 1;

UPDATE xpto SET rand1 = 123.456 WHERE id = 1;

DELETE FROM xpto WHERE id = 1;

SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'pretty-print', '1', 'include-typmod', '0');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2');
SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
