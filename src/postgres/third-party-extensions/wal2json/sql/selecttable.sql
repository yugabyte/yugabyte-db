\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;

DROP TABLE IF EXISTS select_table_1;
DROP TABLE IF EXISTS select_table_2;
DROP TABLE IF EXISTS select_table_3;
DROP SCHEMA IF EXISTS select_schema_1 CASCADE;
DROP SCHEMA IF EXISTS select_schema_2 CASCADE;

CREATE SCHEMA select_schema_1;
CREATE SCHEMA select_schema_2;

CREATE TABLE select_table_1 (a integer, b text, primary key(a));
CREATE TABLE select_schema_1.select_table_1 (a integer, b text, primary key(a));
CREATE TABLE select_schema_1.select_table_2 (a integer, b text, primary key(a));
CREATE TABLE select_schema_2.select_table_1 (a integer, b text, primary key(a));
CREATE TABLE select_schema_2.select_table_2 (a integer, b text, primary key(a));
CREATE TABLE select_schema_2.select_table_3 (a integer, b text, primary key(a));
CREATE TABLE select_table_2 (a integer, b text, primary key(a));
CREATE TABLE select_table_3 (a integer, b text, primary key(a));

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

INSERT INTO select_table_1 (a, b) VALUES(1, 'public.select_table_1');
INSERT INTO select_schema_1.select_table_1 (a, b) VALUES(1, 'select_schema_1.select_table_1');
INSERT INTO select_schema_1.select_table_2 (a, b) VALUES(1, 'select_schema_1.select_table_2');
INSERT INTO select_schema_2.select_table_1 (a, b) VALUES(1, 'select_schema_2.select_table_1');
INSERT INTO select_schema_2.select_table_2 (a, b) VALUES(1, 'select_schema_2.select_table_2');
INSERT INTO select_schema_2.select_table_3 (a, b) VALUES(1, 'select_schema_2.select_table_3');
INSERT INTO select_table_2 (a, b) VALUES(1, 'public.select_table_2');
INSERT INTO select_table_3 (a, b) VALUES(1, 'public.select_table_3');

SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'pretty-print', '1', 'add-tables', '   foo.bar,*.select_table_1  ,select_schema_2.* , public.select_table_3  ');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'add-tables', '   foo.bar,*.select_table_1  ,select_schema_2.* , public.select_table_3  ');
SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
   
