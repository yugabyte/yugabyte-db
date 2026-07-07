CREATE USER user1 IN ROLE duckdb_group;
CREATE USER user2 IN ROLE duckdb_group;
CREATE USER user3;
CREATE USER user4 IN ROLE duckdb_group, pg_write_server_files, pg_read_server_files;
CREATE TABLE t (a int);
GRANT SELECT ON t TO user1;
GRANT SELECT ON t TO user3;
-- Should be allowed because access was granted
SET ROLE user1;
SELECT * FROM t;
-- Should fail
SET ROLE user2;
SELECT * FROM t;

-- Should fail because we're not allowed to read the internal tables by default
SELECT * from duckdb.tables;
SELECT * from duckdb.extensions;

-- Should fail because any Postgres tables accesesd from DuckDB will have their
-- permissions checked even if it happens straight from DuckDB.
SET duckdb.force_execution = false;
SELECT * FROM duckdb.raw_query($$ SELECT * FROM pgduckdb.public.t $$);
SET duckdb.force_execution = true;

-- read_csv from the local filesystem should be disallowed
SELECT count(r['sepal.length']) FROM read_csv('../../data/iris.csv') r;
CALL duckdb.recycle_ddb();
-- It's allowed for users with pg_read_server_files and pg_write_server_files.
SET ROLE user4;
SELECT count(r['sepal.length']) FROM read_csv('../../data/iris.csv') r;

-- Should fail because DuckDB execution is not allowed for this user
SET ROLE user3;
SELECT * FROM t;

-- And all these duckdb functions should also fail, even the ones that don't
-- actually open a duckdb connection.
SET duckdb.force_execution = false;
SELECT * FROM duckdb.raw_query($$ SELECT * FROM pgduckdb.public.t $$);
CALL duckdb.recycle_ddb();
SET duckdb.force_execution = true;

-- Should work with regular postgres execution though, because this user is
-- allowed to read the table.
SET duckdb.force_execution = false;
SELECT * FROM t;
SET duckdb.force_execution = true;

-- Let's add RLS
RESET ROLE;
INSERT INTO t VALUES (1), (2), (3);
ALTER TABLE t ENABLE ROW LEVEL SECURITY;
CREATE POLICY t_policy ON t FOR SELECT USING (a <= 2);
-- Should still see all rows, superusers bypass RLS
SELECT * FROM t;

-- Should only see rows where a <= 2 due to RLS policy
SET ROLE user1;
SELECT * FROM t;

-- Should also enforce RLS via raw_query
SET duckdb.force_execution = false;
SELECT * FROM duckdb.raw_query($$ SELECT * FROM pgduckdb.public.t $$);
SET duckdb.force_execution = true;


-- Extension installation
SET duckdb.force_execution = false;
-- Should fail because installing extensions is restricted for super users by default
SELECT * FROM duckdb.install_extension('iceberg');
-- Similarly when trying using raw duckdb commands
SELECT * FROM duckdb.raw_query($$ INSTALL someextension $$);
SET duckdb.force_execution = true;


-- It should be possible to install extensions as non-superuser after the
-- following grants.
RESET ROLE;
GRANT ALL ON FUNCTION duckdb.install_extension(TEXT, TEXT) TO user1;
GRANT ALL ON TABLE duckdb.extensions TO user1;
GRANT ALL ON SEQUENCE duckdb.extensions_table_seq TO user1;

-- You need to reconnect though (or run recycle_ddb), because
-- disabled_filesystems cannot be changed after it has been set.
\c
SET ROLE user1;
SET duckdb.force_execution = false;
SELECT * FROM duckdb.install_extension('iceberg');
TRUNCATE duckdb.extensions;
SET duckdb.force_execution = true;

-- Test Issue #931
RESET ROLE;
CALL duckdb.recycle_ddb();
ALTER SYSTEM SET duckdb.disabled_filesystems = 'LocalFileSystem';
SELECT pg_reload_conf();
CREATE USER admin_user IN ROLE duckdb_group;
SET ROLE admin_user;
CREATE TEMP TABLE duckdb_tbl (id int) USING DUCKDB;
DROP TABLE duckdb_tbl;

-- Cleanup
RESET ROLE;
CALL duckdb.recycle_ddb();
ALTER SYSTEM SET duckdb.disabled_filesystems = '';
SELECT pg_reload_conf();
DROP USER admin_user;
DROP TABLE t;
DROP OWNED BY user1;
DROP USER user1, user2, user3;
