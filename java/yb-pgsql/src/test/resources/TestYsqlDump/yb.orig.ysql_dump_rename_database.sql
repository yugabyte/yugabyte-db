-- Source database for the --rename-database flag test.
-- The dump runs with --rename-database=rename_db_tgt, so every
-- CREATE DATABASE / ALTER DATABASE / COMMENT ON DATABASE / GRANT/REVOKE-on-DB
-- and \connect line in the captured output should reference rename_db_tgt
-- instead of rename_db_src.
--
-- SPLIT INTO N TABLETS is set explicitly on each HASH-partitioned table and
-- index so the dump output is independent of the cluster's default
-- tablet count (which differs between yb-ctl and the Java mini-cluster).

CREATE DATABASE rename_db_src;
\c rename_db_src

CREATE TABLE t (k int PRIMARY KEY, v text) SPLIT INTO 3 TABLETS;
INSERT INTO t VALUES (1, 'a'), (2, 'b');
CREATE INDEX t_v_idx ON t (v) SPLIT INTO 3 TABLETS;

CREATE SEQUENCE s START WITH 1 INCREMENT BY 1;

CREATE VIEW vw AS SELECT k, v FROM t;

COMMENT ON DATABASE rename_db_src IS 'rename_database test source';

-- Database-scoped GUC: exercises dumpDatabaseConfig's
-- "ALTER DATABASE <name> SET ..." emission path.
ALTER DATABASE rename_db_src SET timezone TO 'UTC';

-- Role-and-database-scoped GUC: exercises dumpDatabaseConfig's
-- "ALTER ROLE <r> IN DATABASE <name> SET ..." emission path.
ALTER ROLE yugabyte_test IN DATABASE rename_db_src SET search_path TO 'public';
