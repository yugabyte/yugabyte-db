--
-- This file is to check the effect of DDL statements on the catalog version
-- stored in pg_yb_catalog_table. In particular, not all DDL query causes
-- changes in catalog. For example, a grant statement may not really change
-- anything if the same privileges are already granted. In such cases it is
-- benefitial to avoid catalog version increment to prevent catalog cache
-- update which may overload yb-master if there are a large number of active
-- Postgres connections.
--
--
\set template1_db_oid 1
-- Build a SQL query that works with per-database catalog version mode enabled
-- or disabled.
\set display_catalog_version 'SELECT current_version, last_breaking_version FROM pg_yb_catalog_version WHERE db_oid = CASE WHEN (select count(*) from pg_yb_catalog_version) = 1 THEN :template1_db_oid ELSE (SELECT oid FROM pg_database WHERE datname = \'yugabyte\') END'

-- Display the initial catalog version.
:display_catalog_version;

-- The next CREATE ROLE will not increment current_version.
CREATE ROLE cv_test_role;
:display_catalog_version;

-- The next CREATE ROLE fails and should not cause any catalog version change.
CREATE ROLE cv_test_role;
:display_catalog_version;

-- The next CREATE ROLE will increment current_version.
CREATE ROLE cv_test_role2 IN ROLE cv_test_role;
:display_catalog_version;

-- The next CREATE ROLE will increment current_version.
CREATE ROLE cv_test_role3 ADMIN cv_test_role;
:display_catalog_version;

-- The next CREATE ROLE will increment current_version.
CREATE ROLE cv_test_role4 ROLE cv_test_role2, cv_test_role3;
:display_catalog_version;

-- The next CREATE DATABASE will not increment current_version.
CREATE DATABASE cv_test_database;
:display_catalog_version;

-- The next GRANT CONNECT will increment current_version.
GRANT CONNECT ON DATABASE cv_test_database TO cv_test_role;
:display_catalog_version;

-- The next GRANT CONNECT should not cause any catalog version change.
GRANT CONNECT ON DATABASE cv_test_database TO cv_test_role;
:display_catalog_version;

-- The next REVOKE CONNECT is a "breaking" catalog change. It will increment
-- both current_version and last_breaking_version.
REVOKE CONNECT ON DATABASE cv_test_database from cv_test_role;
:display_catalog_version;

-- The next REVOKE CONNECT should not cause any catalog version change.
REVOKE CONNECT ON DATABASE cv_test_database from cv_test_role;
:display_catalog_version;

-- The next CREATE TABLE should not cause any catalog version change.
CREATE TABLE cv_test_table(id int);
:display_catalog_version;

-- The next GRANT SELECT will increment current_version.
GRANT SELECT ON cv_test_table TO cv_test_role;
:display_catalog_version;

-- The next GRANT SELECT should not cause any catalog version change.
GRANT SELECT ON cv_test_table TO cv_test_role;
:display_catalog_version;

-- The next REVOKE SELECT is a "breaking" catalog change. It will increment
-- both current_version and last_breaking_version.
REVOKE SELECT ON cv_test_table FROM cv_test_role;
:display_catalog_version;

-- The next REVOKE SELECT should not cause any catalog version change.
REVOKE SELECT ON cv_test_table FROM cv_test_role;
:display_catalog_version;

-- The next CREATE FOREIGN DATA WRAPPER will increment current_version.
CREATE FOREIGN DATA WRAPPER cv_test_fdw_wrapper;
-- The next GRANT USAGE will increment current_version.
GRANT USAGE ON FOREIGN DATA WRAPPER cv_test_fdw_wrapper TO cv_test_role;
:display_catalog_version;

-- The next GRANT USAGE should not cause any catalog version change.
GRANT USAGE ON FOREIGN DATA WRAPPER cv_test_fdw_wrapper TO cv_test_role;
:display_catalog_version;

-- The next REVOKE USAGE is a "breaking" catalog change. It will increment
-- both current_version and last_breaking_version.
REVOKE USAGE ON FOREIGN DATA WRAPPER cv_test_fdw_wrapper FROM cv_test_role;
:display_catalog_version;

-- The next REVOKE USAGE should not cause any catalog version change.
REVOKE USAGE ON FOREIGN DATA WRAPPER cv_test_fdw_wrapper FROM cv_test_role;
:display_catalog_version;

-- The next CREATE SERVER will increment current_version.
CREATE SERVER cv_test_fdw_server FOREIGN DATA WRAPPER cv_test_fdw_wrapper;
-- The next GRANT USAGE will increment current_version.
GRANT USAGE ON FOREIGN SERVER cv_test_fdw_server TO cv_test_role;
:display_catalog_version;

-- The next GRANT USAGE should not cause any catalog version change.
GRANT USAGE ON FOREIGN SERVER cv_test_fdw_server TO cv_test_role;
:display_catalog_version;

-- The next REVOKE USAGE is a "breaking" catalog change. It will increment
-- both current_version and last_breaking_version.
REVOKE USAGE ON FOREIGN SERVER cv_test_fdw_server FROM cv_test_role;
:display_catalog_version;

-- The next REVOKE USAGE should not cause any catalog version change.
REVOKE USAGE ON FOREIGN SERVER cv_test_fdw_server FROM cv_test_role;
:display_catalog_version;

-- The next CREATE FUNCTION will increment current_version.
CREATE FUNCTION cv_test_function() RETURNS int AS $$ SELECT 1 $$ LANGUAGE sql;

-- The next GRANT EXECUTE will increment current_version.
GRANT EXECUTE ON FUNCTION cv_test_function TO cv_test_role;
:display_catalog_version;

-- The next GRANT EXECUTE should not cause any catalog version change.
GRANT EXECUTE ON FUNCTION cv_test_function TO cv_test_role;
:display_catalog_version;

-- The next REVOKE EXECUTE is a "breaking" catalog change. It will increment
-- both current_version and last_breaking_version.
REVOKE EXECUTE ON FUNCTION cv_test_function from cv_test_role;
:display_catalog_version;

-- The next REVOKE EXECUTE should not cause any catalog version change.
REVOKE EXECUTE ON FUNCTION cv_test_function from cv_test_role;
:display_catalog_version;

-- The next GRANT USAGE will increment current_version.
GRANT USAGE ON LANGUAGE sql TO cv_test_role;
:display_catalog_version;

-- The next GRANT USAGE should not cause any catalog version change.
GRANT USAGE ON LANGUAGE sql TO cv_test_role;
:display_catalog_version;

-- The next REVOKE USAGE is a "breaking" catalog change. It will increment
-- both current_version and last_breaking_version.
REVOKE USAGE ON LANGUAGE sql from cv_test_role;
:display_catalog_version;

-- The next REVOKE USAGE should not cause any catalog version change.
REVOKE USAGE ON LANGUAGE sql from cv_test_role;
:display_catalog_version;

SET yb_non_ddl_txn_for_sys_tables_allowed=1;
SELECT lo_create(1001);
SET yb_non_ddl_txn_for_sys_tables_allowed=0;

-- The next GRANT SELECT will increment current_version.
GRANT SELECT ON LARGE OBJECT 1001 TO cv_test_role;
:display_catalog_version;

-- The next GRANT SELECT should not cause any catalog version change.
GRANT SELECT ON LARGE OBJECT 1001 TO cv_test_role;
:display_catalog_version;

-- The next REVOKE SELECT is a "breaking" catalog change. It will increment
-- both current_version and last_breaking_version.
REVOKE SELECT ON LARGE OBJECT 1001 from cv_test_role;
:display_catalog_version;

-- The next REVOKE SELECT should not cause any catalog version change.
REVOKE SELECT ON LARGE OBJECT 1001 from cv_test_role;
:display_catalog_version;

-- The next GRANT USAGE will increment current_version.
GRANT USAGE ON SCHEMA public TO cv_test_role;
:display_catalog_version;

-- The next GRANT USAGE should not cause any catalog version change.
GRANT USAGE ON SCHEMA public TO cv_test_role;
:display_catalog_version;

-- The next REVOKE USAGE is a "breaking" catalog change. It will increment
-- both current_version and last_breaking_version.
REVOKE USAGE ON SCHEMA public from cv_test_role;
:display_catalog_version;

-- The next REVOKE USAGE should not cause any catalog version change.
REVOKE USAGE ON SCHEMA public from cv_test_role;
:display_catalog_version;

-- The next CREATE TABLEGROUP will increment current_version.
CREATE TABLEGROUP cv_test_tablegroup;

-- The next GRANT CREATE will increment current_version.
GRANT CREATE ON TABLEGROUP cv_test_tablegroup TO cv_test_role;
:display_catalog_version;

-- The next GRANT CREATE should not cause any catalog version change.
GRANT CREATE ON TABLEGROUP cv_test_tablegroup TO cv_test_role;
:display_catalog_version;

-- The next REVOKE CREATE is a "breaking" catalog change. It will increment
-- both current_version and last_breaking_version.
REVOKE CREATE ON TABLEGROUP cv_test_tablegroup from cv_test_role;
:display_catalog_version;

-- The next REVOKE CREATE should not cause any catalog version change.
REVOKE CREATE ON TABLEGROUP cv_test_tablegroup from cv_test_role;
:display_catalog_version;

-- The next CREATE TABLESPACE will increment current_version.
CREATE TABLESPACE cv_test_tablespace WITH (replica_placement=
  '{"num_replicas":1,"placement_blocks":[{"cloud":"c1","region":"r1","zone":"z1","min_num_replicas":1}]}');

-- The next GRANT CREATE will increment current_version.
GRANT CREATE ON TABLESPACE cv_test_tablespace TO cv_test_role;
:display_catalog_version;

-- The next GRANT CREATE should not cause any catalog version change.
GRANT CREATE ON TABLESPACE cv_test_tablespace TO cv_test_role;
:display_catalog_version;

-- The next REVOKE CREATE is a "breaking" catalog change. It will increment
-- both current_version and last_breaking_version.
REVOKE CREATE ON TABLESPACE cv_test_tablespace from cv_test_role;
:display_catalog_version;

-- The next REVOKE CREATE should not cause any catalog version change.
REVOKE CREATE ON TABLESPACE cv_test_tablespace from cv_test_role;
:display_catalog_version;

-- The next CREATE TYPE will increment current_version.
CREATE TYPE cv_test_type AS (a int, b text);

-- The next GRANT USAGE will increment current_version.
GRANT USAGE ON TYPE cv_test_type TO cv_test_role;
:display_catalog_version;

-- The next GRANT USAGE should not cause any catalog version change.
GRANT USAGE ON TYPE cv_test_type TO cv_test_role;
:display_catalog_version;

-- The next REVOKE USAGE is a "breaking" catalog change. It will increment
-- both current_version and last_breaking_version.
REVOKE USAGE ON TYPE cv_test_type from cv_test_role;
:display_catalog_version;

-- The next REVOKE USAGE should not cause any catalog version change.
REVOKE USAGE ON TYPE cv_test_type from cv_test_role;
:display_catalog_version;

-- Tables with various constraint types should not increment catalog version.
CREATE TABLE t_check (col INT CHECK (col > 0));
:display_catalog_version;
CREATE TABLE t_not_null (col INT NOT NULL);
:display_catalog_version;
CREATE TABLE t_primary_key (col INT PRIMARY KEY);
:display_catalog_version;
CREATE TABLE t_sequence (col SERIAL, value TEXT);
:display_catalog_version;
CREATE TABLE t_unique (col INT UNIQUE);
:display_catalog_version;
CREATE TABLE t_identity (col INT GENERATED ALWAYS AS IDENTITY);
:display_catalog_version;
CREATE TABLE t_primary_key_sequence_identity (c1 INT PRIMARY KEY, c2 SERIAL, c3 INT GENERATED ALWAYS AS IDENTITY);
:display_catalog_version;

-- The CREATE TABLE with FOREIGN KEY will increment current_version.
CREATE TABLE t1 (col INT UNIQUE);
CREATE TABLE t2 (col INT REFERENCES t1(col));
:display_catalog_version;

-- The ALTER TABLE will increment current_version.
ALTER TABLE t1 ADD COLUMN val INT;
:display_catalog_version;

-- The CREATE PROCEDURE will not increment current_version.
CREATE PROCEDURE test() AS $$ BEGIN INSERT INTO t1 VALUES(1); END; $$ LANGUAGE 'plpgsql';
:display_catalog_version;
-- The CALL to PROCEDURE will not increment current_version.
CALL test();
:display_catalog_version;

-- The CREATE FUNCTION will increment current_version.
CREATE TABLE evt_trig_table (id serial PRIMARY KEY, evt_trig_name text);
CREATE OR REPLACE FUNCTION evt_trig_fn() RETURNS event_trigger AS $$ BEGIN INSERT INTO evt_trig_table (evt_trig_name) VALUES (TG_EVENT); END; $$ LANGUAGE plpgsql;
:display_catalog_version;
-- The CREATE EVENT TRIGGER will increment current_version.
CREATE EVENT TRIGGER evt_ddl_start ON ddl_command_start EXECUTE PROCEDURE evt_trig_fn();
:display_catalog_version;
-- The DDLs proceeding the trigger will increment current_version based on the command's individual behaviour.
-- The ALTER TABLE will increment current_version.
ALTER TABLE evt_trig_table ADD COLUMN val INT;
:display_catalog_version;
-- The CREATE TABLE will not increment current_version.
CREATE TABLE post_trigger_table (id INT);
:display_catalog_version;

-- The execution on atomic SPI context function will increment current_version.
CREATE FUNCTION atomic_spi(INT) RETURNS INT AS $$
DECLARE TOTAL INT;
BEGIN
  CREATE TEMP TABLE temp_table(a INT);
  INSERT INTO temp_table VALUES($1);
  INSERT INTO temp_table VALUES(1);
  SELECT SUM(a) INTO TOTAL FROM temp_table;
  DROP TABLE temp_table;
  RETURN TOTAL;
END
$$ LANGUAGE PLPGSQL;

SELECT atomic_spi(1);
:display_catalog_version;
SELECT atomic_spi(2);
:display_catalog_version;

-- The execution on non-atomic SPI context will not increment current_version.
CREATE TABLE non_atomic_spi(a INT, b INT);
CREATE PROCEDURE proc(x INT, y INT) LANGUAGE PLPGSQL AS $$
BEGIN FOR i IN 0..x LOOP INSERT INTO non_atomic_spi(a, b) VALUES (i, y);
IF i % 2 = 0 THEN COMMIT;
ELSE ROLLBACK;
END IF;
END LOOP;
END $$;
CREATE PROCEDURE p1() LANGUAGE PLPGSQL AS $$ BEGIN CALL p2(); END $$;
CREATE PROCEDURE p2() LANGUAGE PLPGSQL AS $$ BEGIN CALL proc(2, 2); END $$;

CALL p1();
:display_catalog_version;

-- Verify that variants of CREATE TABLE AS SELECT do not increment catalog version.
CREATE TABLE t (id INT);
SELECT id INTO TEMPORARY TABLE temp FROM t;
:display_catalog_version;
CREATE TEMPORARY TABLE temp2 AS SELECT id FROM t;
:display_catalog_version;
CREATE TABLE t_1 AS SELECT id FROM t;
:display_catalog_version;

-- Verify that catalog version gets incremented if CREATE TABLE AS invokes other
-- DDL operations.
CREATE TABLE t_2 (id INT);
GRANT ALL ON t_2 TO public;
CREATE OR REPLACE FUNCTION f1() RETURNS float8 AS $$
BEGIN
	ALTER TABLE t_2 ADD COLUMN v2 int;
	REVOKE SELECT ON t_2 FROM public;
	RETURN 1;
END$$ LANGUAGE plpgsql;
:display_catalog_version;
CREATE TABLE t_3 AS SELECT c FROM (SELECT 1 AS c, f1()) AS s;
:display_catalog_version;

-- The next GRANT SELECT will increment current_version.
GRANT SELECT (rolname, rolsuper) ON pg_authid TO CURRENT_USER;
:display_catalog_version;

-- The next GRANT SELECT will increment current_version due to evt_ddl_start.
GRANT SELECT (rolname, rolsuper) ON pg_authid TO CURRENT_USER;
:display_catalog_version;

DROP EVENT TRIGGER evt_ddl_start;
:display_catalog_version;

-- The next GRANT SELECT should not cause any catalog version change.
GRANT SELECT (rolname, rolsuper) ON pg_authid TO CURRENT_USER;
:display_catalog_version;
