\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;

DROP TABLE IF EXISTS xpto;

SELECT setseed(0);
CREATE TABLE xpto (
id			serial primary key,
rand1		float8 DEFAULT random(),
bincol		bytea
);

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

INSERT INTO xpto (bincol) SELECT decode(string_agg(to_char(round(g.i * random()), 'FM0000'), ''), 'hex') FROM generate_series(500, 5000) g(i);
UPDATE xpto SET rand1 = 123.456 WHERE id = 1;
DELETE FROM xpto WHERE id = 1;

SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'pretty-print', '1', 'include-typmod', '0');
SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
