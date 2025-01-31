/* src/test/modules/test_pg_dump/test_pg_dump--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION test_pg_dump" to load this file. \quit

CREATE TABLE regress_pg_dump_table (
	col1 serial,
	col2 int check (col2 > 0)
);

CREATE SEQUENCE regress_pg_dump_seq;

CREATE SEQUENCE regress_seq_dumpable;
SELECT pg_catalog.pg_extension_config_dump('regress_seq_dumpable', '');

CREATE TABLE regress_table_dumpable (
	col1 int check (col1 > 0)
);
SELECT pg_catalog.pg_extension_config_dump('regress_table_dumpable', '');

CREATE SCHEMA regress_pg_dump_schema;

GRANT USAGE ON regress_pg_dump_seq TO regress_dump_test_role;

GRANT SELECT ON regress_pg_dump_table TO regress_dump_test_role;
GRANT SELECT(col1) ON regress_pg_dump_table TO public;

GRANT SELECT(col2) ON regress_pg_dump_table TO regress_dump_test_role;
REVOKE SELECT(col2) ON regress_pg_dump_table FROM regress_dump_test_role;

CREATE FUNCTION wgo_then_no_access() RETURNS int LANGUAGE SQL AS 'SELECT 1';
GRANT ALL ON FUNCTION wgo_then_no_access()
	TO pg_signal_backend WITH GRANT OPTION;

CREATE SEQUENCE wgo_then_regular;
GRANT ALL ON SEQUENCE wgo_then_regular TO pg_signal_backend WITH GRANT OPTION;
REVOKE GRANT OPTION FOR SELECT ON SEQUENCE wgo_then_regular
	FROM pg_signal_backend;

CREATE ACCESS METHOD regress_test_am TYPE INDEX HANDLER bthandler;

-- Create a set of objects that are part of the schema created by
-- this extension.
CREATE TABLE regress_pg_dump_schema.test_table (
	col1 int,
	col2 int check (col2 > 0)
);
GRANT SELECT ON regress_pg_dump_schema.test_table TO regress_dump_test_role;

CREATE SEQUENCE regress_pg_dump_schema.test_seq;
GRANT USAGE ON regress_pg_dump_schema.test_seq TO regress_dump_test_role;

CREATE TYPE regress_pg_dump_schema.test_type AS (col1 int);
GRANT USAGE ON TYPE regress_pg_dump_schema.test_type TO regress_dump_test_role;

CREATE FUNCTION regress_pg_dump_schema.test_func () RETURNS int
AS 'SELECT 1;' LANGUAGE SQL;
GRANT EXECUTE ON FUNCTION regress_pg_dump_schema.test_func() TO regress_dump_test_role;

CREATE AGGREGATE regress_pg_dump_schema.test_agg(int2)
(SFUNC = int2_sum, STYPE = int8);
GRANT EXECUTE ON FUNCTION regress_pg_dump_schema.test_agg(int2) TO regress_dump_test_role;
