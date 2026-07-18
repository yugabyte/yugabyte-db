\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;

DROP TABLE IF EXISTS xpto;

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

CREATE TABLE xpto (a SERIAL PRIMARY KEY, b bool, c varchar(60), d real);
COMMIT;

BEGIN;
INSERT INTO xpto (b, c, d) VALUES('t', 'test1', '+inf');
INSERT INTO xpto (b, c, d) VALUES('f', 'test2', 'nan');
INSERT INTO xpto (b, c, d) VALUES(NULL, 'null', '-inf');
INSERT INTO xpto (b, c, d) VALUES(TRUE, E'valid: '' " \\ / \b \f \n \r \t \u207F \u967F invalid: \\g \\k end', 123.456);
COMMIT;

SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'format-version', '1', 'pretty-print', '1', 'include-typmod', '0');
SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
