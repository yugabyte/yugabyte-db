-- predictability
SET synchronous_commit = on;
SET extra_float_digits = 0;

CREATE TABLE table_truncate_1 (a integer, b text);
CREATE TABLE table_truncate_2 (a integer, b text);
CREATE TABLE table_truncate_3 (a integer, b text);
CREATE TABLE table_truncate_4 (a integer, b text);
CREATE TABLE table_truncate_5 (a integer, b text);
CREATE TABLE table_truncate_6 (a integer, b text);

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

TRUNCATE table_truncate_1;

BEGIN;
TRUNCATE table_truncate_2;
INSERT INTO table_truncate_1 (a, b) VALUES(1, 'test1');
INSERT INTO table_truncate_3 (a, b) VALUES(2, 'test2');
TRUNCATE table_truncate_3;
INSERT INTO table_truncate_3 (a, b) VALUES(3, 'test3');
COMMIT;

BEGIN;
TRUNCATE table_truncate_4;
ROLLBACK;

BEGIN;
TRUNCATE table_truncate_5, table_truncate_6;
COMMIT;

SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'pretty-print', '1');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'filter-tables', '*.table_truncate_5');
SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
