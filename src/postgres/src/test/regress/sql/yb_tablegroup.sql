--
-- YB_TABLEGROUP Testsuite: Testing Statments for TABLEGROUP.
--

--
-- pg_catalog alterations. Validate columns of pg_tablegroup and oids.
--
\d pg_tablegroup
SELECT oid, relname, reltype, relnatts FROM pg_class WHERE relname IN ('pg_tablegroup', 'pg_tablegroup_oid_index');
SELECT oid, typname, typrelid FROM pg_type WHERE typname LIKE 'pg_tablegroup';
--
-- CREATE TABLEGROUP
--

CREATE TABLEGROUP tgroup1;
CREATE TABLEGROUP tgroup2;
CREATE TABLEGROUP tgroup3;
CREATE TABLE tgroup_test1 (col1 int, col2 int) TABLEGROUP tgroup1;
CREATE TABLE tgroup_test2 (col1 int, col2 int) TABLEGROUP tgroup1;
SELECT grpname FROM pg_tablegroup;
SELECT relname
    FROM (SELECT relname, unnest(reloptions) AS opts FROM pg_class) s
    WHERE opts LIKE '%tablegroup%';
CREATE INDEX ON tgroup_test1(col2);
CREATE TABLE tgroup_test3 (col1 int, col2 int) TABLEGROUP tgroup2;
SELECT relname, relhasindex
    FROM (SELECT relname, relhasindex, unnest(reloptions) AS opts FROM pg_class) s, pg_tablegroup
    WHERE opts LIKE CONCAT('%', CAST(pg_tablegroup.oid AS text), '%');
-- These should fail.
CREATE TABLEGROUP tgroup1;
CREATE TABLE tgroup_test (col1 int, col2 int) TABLEGROUP bad_tgroupname;
CREATE TABLE tgroup_optout (col1 int, col2 int) WITH (colocated=false) TABLEGROUP tgroup1;
CREATE TABLE tgroup_optout (col1 int, col2 int) WITH (colocated=true) TABLEGROUP tgroup1;
CREATE TABLE tgroup_optout (col1 int, col2 int) WITH (colocated=false) TABLEGROUP bad_tgroupname;
CREATE TEMP TABLE tgroup_temp (col1 int, col2 int) TABLEGROUP tgroup1;

--
-- Usage of WITH clause or specifying tablegroup name for CREATE INDEX. These all fail.
--

CREATE TABLE tgroup_with (col1 int, col2 int) WITH (tablegroup=123);
CREATE TABLE tgroup_with (col1 int, col2 int) WITH (tablegroup=123, colocated=true);
CREATE TABLE tgroup_with (col1 int, col2 int) WITH (tablegroup=123) TABLEGROUP tgroup1;
CREATE INDEX ON tgroup_test1(col1) WITH (tablegroup=123);
CREATE INDEX ON tgroup_test1(col1) WITH (tablegroup=123, colocated=true);
CREATE INDEX ON tgroup_test1(col1) WITH (tablegroup=123) TABLEGROUP tgroup1;
CREATE INDEX ON tgroup_test1(col1) TABLEGROUP tgroup1;

--
-- DROP TABLEGROUP
--

DROP TABLEGROUP tgroup3;
-- These should fail. CREATE TABLE is to check that the row entry was deleted from pg_tablegroup.
CREATE TABLE tgroup_test4 (col1 int, col2 int) TABLEGROUP tgroup3;
DROP TABLEGROUP tgroup1;
DROP TABLEGROUP bad_tgroupname;
-- This drop should work now.
DROP TABLE tgroup_test1;
DROP TABLE tgroup_test2;
DROP TABLEGROUP tgroup1;
-- Create a tablegroup with the name of a dropped tablegroup.
CREATE TABLEGROUP tgroup1;

--
-- Interactions with colocated database.
--

CREATE DATABASE db_colocated colocated=true;
\c db_colocated
-- These should fail.
CREATE TABLEGROUP tgroup1;
CREATE TABLE tgroup_test (col1 int, col2 int) TABLEGROUP tgroup1;
CREATE TABLE tgroup_optout (col1 int, col2 int) WITH (colocated=false) TABLEGROUP tgroup1;
