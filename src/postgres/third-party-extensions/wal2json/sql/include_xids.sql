\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;

DROP TABLE IF EXISTS tbl;
CREATE TABLE tbl (id int);

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

-- One row should have one record and one xids
INSERT INTO tbl VALUES (1);
SELECT count(*) = 1, count(distinct ((data::json)->'xid')::text) = 1 FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'format-version', '1', 'include-xids', '1');

-- Two rows should have two records and two xids
INSERT INTO tbl VALUES (2);
INSERT INTO tbl VALUES (3);
SELECT count(*) = 2, count(distinct ((data::json)->'xid')::text) = 2 FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'format-version', '1', 'include-xids', '1');

-- Two rows in one transaction should have one record and one xid
INSERT INTO tbl VALUES (4), (5);
SELECT count(*) = 2, count(distinct ((data::json)->'xid')::text) = 1 FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'format-version', '1', 'include-xids', '1');

SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');

