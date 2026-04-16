\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;

DROP TABLE IF EXISTS filter_table_1;
DROP TABLE IF EXISTS filter_table_2;
DROP TABLE IF EXISTS filter_table_3;
DROP TABLE IF EXISTS filter_table_4;
DROP TABLE IF EXISTS "Filter_table_5";
DROP TABLE IF EXISTS "filter table_6";
DROP TABLE IF EXISTS "filter.table_7";
DROP TABLE IF EXISTS "filter,table_8";
DROP TABLE IF EXISTS "filter""table_9";
DROP TABLE IF EXISTS " filter_table_10";
DROP TABLE IF EXISTS "*";
DROP SCHEMA IF EXISTS filter_schema_1 CASCADE;
DROP SCHEMA IF EXISTS filter_schema_2 CASCADE;
DROP SCHEMA IF EXISTS "*" CASCADE;

CREATE SCHEMA filter_schema_1;
CREATE SCHEMA filter_schema_2;
CREATE SCHEMA "*";

CREATE TABLE filter_table_1 (a integer, b text, primary key(a));
CREATE TABLE filter_schema_1.filter_table_1 (a integer, b text, primary key(a));
CREATE TABLE filter_schema_1.filter_table_2 (a integer, b text, primary key(a));
CREATE TABLE filter_schema_2.filter_table_1 (a integer, b text, primary key(a));
CREATE TABLE filter_schema_2.filter_table_2 (a integer, b text, primary key(a));
CREATE TABLE filter_schema_2.filter_table_3 (a integer, b text, primary key(a));
CREATE TABLE filter_table_2 (a integer, b text, primary key(a));
CREATE TABLE filter_table_3 (a integer, b text, primary key(a));
CREATE TABLE filter_table_4 (a integer, b text, primary key(a));
CREATE TABLE "Filter_table_5" (a integer, b text, primary key(a));
CREATE TABLE "filter table_6" (a integer, b text, primary key(a));
CREATE TABLE "filter.table_7" (a integer, b text, primary key(a));
CREATE TABLE "filter,table_8" (a integer, b text, primary key(a));
CREATE TABLE "filter""table_9" (a integer, b text, primary key(a));
CREATE TABLE " filter_table_10" (a integer, b text, primary key(a));
CREATE TABLE "*" (a integer, b text, primary key(a));
CREATE TABLE "*".filter_table_0 (a integer, b text, primary key(a));

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

INSERT INTO filter_table_1 (a, b) VALUES(1, 'public.filter_table_1');
INSERT INTO filter_schema_1.filter_table_1 (a, b) VALUES(1, 'filter_schema_1.filter_table_1');
INSERT INTO filter_schema_1.filter_table_2 (a, b) VALUES(1, 'filter_schema_1.filter_table_2');
INSERT INTO filter_schema_2.filter_table_1 (a, b) VALUES(1, 'filter_schema_2.filter_table_1');
INSERT INTO filter_schema_2.filter_table_2 (a, b) VALUES(1, 'filter_schema_2.filter_table_2');
INSERT INTO filter_schema_2.filter_table_3 (a, b) VALUES(1, 'filter_schema_2.filter_table_3');
INSERT INTO filter_table_2 (a, b) VALUES(1, 'public.filter_table_2');
INSERT INTO filter_table_3 (a, b) VALUES(1, 'public.filter_table_3');
INSERT INTO filter_table_4 (a, b) VALUES(1, 'public.filter_table_4');
INSERT INTO "Filter_table_5" (a, b) VALUES(1, 'public.Filter_table_5');
INSERT INTO "filter table_6" (a, b) VALUES(1, 'public.filter table_6');
INSERT INTO "filter.table_7" (a, b) VALUES(1, 'public.filter.table_7');
INSERT INTO "filter,table_8" (a, b) VALUES(1, 'public.filter,table_8');
INSERT INTO "filter""table_9" (a, b) VALUES(1, 'public.filter"table_9');
INSERT INTO " filter_table_10" (a, b) VALUES(1, 'public. filter_table_10');
INSERT INTO "*" (a, b) VALUES(1, 'public.*');
INSERT INTO "*".filter_table_0 (a, b) VALUES(1, '*.filter_table_0');

SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'pretty-print', '1', 'filter-tables', '   foo.bar,*.filter_table_1  ,filter_schema_2.* , public.filter_table_3 , public.Filter_table_5, public.filter\ table_6, public.filter\.table_7 , public.filter\,table_8 , public.filter"table_9, *.\ filter_table_10 , public.\* , \*.filter_table_0  ');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'filter-tables', '   foo.bar,*.filter_table_1  ,filter_schema_2.* , public.filter_table_3 , public.Filter_table_5, public.filter\ table_6, public.filter\.table_7 , public.filter\,table_8 , public.filter"table_9, *.\ filter_table_10 , public.\* , \*.filter_table_0  ');
SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
