-- Test yb_test_make_all_ddl_statements_incrementing GUC.
-- All of the following DDL statements should increment current_version.
\set template1_db_oid 1
\set db_oid 'CASE WHEN (select count(*) from pg_yb_catalog_version) = 1 THEN :template1_db_oid ELSE (SELECT oid FROM pg_database WHERE datname = \'yugabyte\') END'
-- Set the catalog versions to known, fixed values before running the test.
-- This is done so that the test output isn't affected by queries that run
-- run during test setup.
SET yb_non_ddl_txn_for_sys_tables_allowed TO on;
UPDATE pg_yb_catalog_version SET current_version = 10000,
    last_breaking_version = 10000 WHERE db_oid = :db_oid;
RESET yb_non_ddl_txn_for_sys_tables_allowed;
-- Build a SQL query that works with per-database catalog version mode enabled
-- or disabled.
\set display_catalog_version 'SELECT current_version, last_breaking_version FROM pg_yb_catalog_version WHERE db_oid = :db_oid'
:display_catalog_version;

CREATE PROFILE cv_test_profile_guc LIMIT FAILED_LOGIN_ATTEMPTS 2;
:display_catalog_version;

CREATE TABLE t_trunc_guc(id int);
:display_catalog_version;

INSERT INTO t_trunc_guc VALUES (1);

SET yb_enable_alter_table_rewrite = OFF;
TRUNCATE t_trunc_guc;
:display_catalog_version;

CREATE VIEW v_guc AS SELECT 1 AS a;
:display_catalog_version;

CREATE ROLE cv_test_role_guc;
:display_catalog_version;

ALTER ROLE cv_test_role_guc WITH PASSWORD '123';
:display_catalog_version;

CREATE ROLE cv_test_role_opts_guc LOGIN CREATEDB CREATEROLE INHERIT CONNECTION LIMIT 5;
:display_catalog_version;

CREATE TABLE t_ctas_guc AS SELECT 1 AS id;
:display_catalog_version;

CREATE SEQUENCE s_guc;
:display_catalog_version;

DISCARD ALL;
:display_catalog_version;

CREATE TEMP TABLE temp_to_drop_guc(id int);
:display_catalog_version;

CREATE INDEX idx_guc ON temp_to_drop_guc(id);
:display_catalog_version;

ALTER TABLE temp_to_drop_guc ADD COLUMN val int;
:display_catalog_version;

DROP TABLE temp_to_drop_guc;
:display_catalog_version;

-- Additional DDLs that should increment under yb_test_make_all_ddl_statements_incrementing=true.
CREATE OPERATOR ==# (
    LEFTARG = int,
    RIGHTARG = int,
    PROCEDURE = int4eq
);
:display_catalog_version;

CREATE AGGREGATE cv_test_agg(int) (
    SFUNC = int4pl,
    STYPE = int,
    INITCOND = 0
);
:display_catalog_version;

CREATE TYPE cv_type_enum AS ENUM ('a','b');
:display_catalog_version;

CREATE TEXT SEARCH CONFIGURATION cv_ts_conf ( COPY = pg_catalog.simple );
:display_catalog_version;

CREATE TEXT SEARCH DICTIONARY cv_ts_dict ( TEMPLATE = pg_catalog.simple );
:display_catalog_version;

CREATE COLLATION cv_coll (lc_collate = 'C', lc_ctype = 'C');
:display_catalog_version;

COMMENT ON TABLE t_ctas_guc IS 'x';
:display_catalog_version;
