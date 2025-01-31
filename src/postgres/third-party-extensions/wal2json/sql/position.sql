\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;

CREATE TABLE w2j_position (a integer, b integer, primary key(a));

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

INSERT INTO w2j_position (a, b) VALUES(1,2);
UPDATE w2j_position SET b = 3 WHERE a = 1;
ALTER TABLE w2j_position ADD COLUMN c integer;
ALTER TABLE w2j_position DROP COLUMN b;
INSERT INTO w2j_position (a, c) VALUES(5,6);
UPDATE w2j_position SET c = 7 WHERE a = 5;

-- without include-column-position parameter
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2');

-- with include-column-position parameter
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'include-column-positions', '1');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'include-column-positions', '1');

SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
