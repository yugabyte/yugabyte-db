\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'format-version', '1', 'nosuchopt', '42');

SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'format-version', '1', 'include-unchanged-toast', '1');

SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'filter-origins', '16, 27, 123');

-- don't include not-null constraint by default
CREATE TABLE table_optional (
a smallserial,
b integer,
c boolean not null,
PRIMARY KEY(a)
);
INSERT INTO table_optional (b, c) VALUES(NULL, TRUE);
UPDATE table_optional SET b = 123 WHERE a = 1;
DELETE FROM table_optional WHERE a = 1;
DROP TABLE table_optional;
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'format-version', '1', 'include-xids', '0', 'include-not-null', '1');

-- By default don't write in chunks
CREATE TABLE x ();
DROP TABLE x;
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'include-xids', '0');
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'format-version', '1', 'include-xids', '0', 'write-in-chunks', '1');

-- By default don't write xids
CREATE TABLE gimmexid (id integer PRIMARY KEY);
INSERT INTO gimmexid values (1);
DROP TABLE gimmexid;
SELECT max(((data::json) -> 'xid')::text::int) < txid_current() FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'include-xids', '1');
SELECT max(((data::json) -> 'xid')::text::int) + 10 > txid_current() FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'include-xids', '1');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL) where ((data::json) -> 'xid') IS NOT NULL;


SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
