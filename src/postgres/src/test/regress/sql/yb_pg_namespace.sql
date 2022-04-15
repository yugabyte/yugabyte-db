--
-- Regression tests for schemas (namespaces)
--

CREATE SCHEMA test_ns_schema_1
       CREATE UNIQUE INDEX abc_a_idx ON abc (a)

       CREATE VIEW abc_view AS
              SELECT a+1 AS a, b+1 AS b FROM abc

       CREATE TABLE abc (
              a serial,
              b int UNIQUE
       );

-- TODO(dmitry): Remove separate statements for creation each element in schema after
--               `schema creation with elements` command will be supported.
CREATE SCHEMA test_ns_schema_1;
CREATE TABLE test_ns_schema_1.abc (a serial, b int UNIQUE);
CREATE UNIQUE INDEX abc_a_idx ON test_ns_schema_1.abc (a);
CREATE VIEW test_ns_schema_1.abc_view AS SELECT a+1 AS a, b+1 AS b FROM test_ns_schema_1.abc;

-- verify that the objects were created
SELECT COUNT(*) FROM pg_class WHERE relnamespace =
    (SELECT oid FROM pg_namespace WHERE nspname = 'test_ns_schema_1');

INSERT INTO test_ns_schema_1.abc DEFAULT VALUES;
INSERT INTO test_ns_schema_1.abc DEFAULT VALUES;
INSERT INTO test_ns_schema_1.abc DEFAULT VALUES;

SELECT * FROM test_ns_schema_1.abc ORDER BY a;
SELECT * FROM test_ns_schema_1.abc_view ORDER BY a;

ALTER SCHEMA test_ns_schema_1 RENAME TO test_ns_schema_renamed;
SELECT COUNT(*) FROM pg_class WHERE relnamespace =
    (SELECT oid FROM pg_namespace WHERE nspname = 'test_ns_schema_1');

-- test IF NOT EXISTS cases
CREATE SCHEMA test_ns_schema_renamed; -- fail, already exists
CREATE SCHEMA IF NOT EXISTS test_ns_schema_renamed; -- ok with notice
CREATE SCHEMA IF NOT EXISTS test_ns_schema_renamed -- fail, disallowed
       CREATE TABLE abc (
              a serial,
              b int UNIQUE
       );

DROP SCHEMA test_ns_schema_renamed CASCADE;

-- verify that the objects were dropped
SELECT COUNT(*) FROM pg_class WHERE relnamespace =
    (SELECT oid FROM pg_namespace WHERE nspname = 'test_ns_schema_renamed');

-- verify yb_db_admin role can manage schemas like a superuser
CREATE SCHEMA test_ns_schema_other;
CREATE ROLE test_regress_user1;
SET SESSION AUTHORIZATION yb_db_admin;
ALTER SCHEMA test_ns_schema_other RENAME TO test_ns_schema_other_new;
ALTER SCHEMA test_ns_schema_other_new OWNER TO test_regress_user1;
DROP SCHEMA test_ns_schema_other_new;
-- verify that the objects were dropped
SELECT COUNT(*) FROM pg_class WHERE relnamespace =
    (SELECT oid FROM pg_namespace WHERE nspname = 'test_ns_schema_other_new');
CREATE SCHEMA test_ns_schema_yb_db_admin;
ALTER SCHEMA test_ns_schema_yb_db_admin RENAME TO test_ns_schema_yb_db_admin_new;
ALTER SCHEMA test_ns_schema_yb_db_admin_new OWNER TO test_regress_user1;
DROP SCHEMA test_ns_schema_yb_db_admin_new;
-- verify that the objects were dropped
SELECT COUNT(*) FROM pg_class WHERE relnamespace =
    (SELECT oid FROM pg_namespace WHERE nspname = 'test_ns_schema_yb_db_admin_new');

