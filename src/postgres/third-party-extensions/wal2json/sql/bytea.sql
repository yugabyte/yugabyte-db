\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;
SET extra_float_digits = 0;

DROP TABLE IF EXISTS xpto;
DROP SEQUENCE IF EXISTS xpto_rand_seq;

CREATE SEQUENCE xpto_rand_seq START 11 INCREMENT 997;
CREATE TABLE xpto (
id			serial primary key,
rand1		float8 DEFAULT nextval('xpto_rand_seq'),
bincol		bytea
);

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

INSERT INTO xpto (bincol) SELECT decode(string_agg(to_char(round(g.i * 0.08122019), 'FM0000'), ''), 'hex') FROM generate_series(500, 5000) g(i);
UPDATE xpto SET rand1 = 123.456 WHERE id = 1;
DELETE FROM xpto WHERE id = 1;

SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'pretty-print', '1', 'include-typmod', '0');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2');
SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
