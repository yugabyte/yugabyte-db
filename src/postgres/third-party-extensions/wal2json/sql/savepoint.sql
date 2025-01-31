\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;

CREATE TABLE xpto (a SERIAL PRIMARY KEY, b text);

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

INSERT INTO xpto (b) VALUES('john');
INSERT INTO xpto (b) VALUES('smith');
INSERT INTO xpto (b) VALUES('robert');

BEGIN;
INSERT INTO xpto (b) VALUES('marie');
SAVEPOINT sp1;
INSERT INTO xpto (b) VALUES('ernesto');
SAVEPOINT sp2;
INSERT INTO xpto (b) VALUES('peter');	-- discard
SAVEPOINT sp3;
INSERT INTO xpto (b) VALUES('albert');	-- discard
ROLLBACK TO SAVEPOINT sp2;
RELEASE SAVEPOINT sp1;
INSERT INTO xpto (b) VALUES('francisco');
END;

SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'format-version', '1', 'pretty-print', '1', 'include-typmod', '0');
SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
