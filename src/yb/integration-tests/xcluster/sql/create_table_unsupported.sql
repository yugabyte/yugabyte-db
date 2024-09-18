--
-- CREATE_TABLE
--

-- Taken from Postgres test/regress/sql/create_table.sql
-- Commands that aren't supported currently by YB or yb_xcluster_ddl_replication.

-- invalid: non-lowercase quoted reloptions identifiers
CREATE TABLE tas_case WITH ("Fillfactor" = 10) AS SELECT 1 a;
CREATE TABLE tas_case (a text) WITH ("Oids" = true);

CREATE UNLOGGED TABLE unlogged1 (a int primary key);			-- OK
CREATE TEMPORARY TABLE unlogged2 (a int primary key);			-- OK
SELECT relname, relkind, relpersistence FROM pg_class WHERE relname ~ '^unlogged\d' ORDER BY relname;
REINDEX INDEX unlogged1_pkey;
REINDEX INDEX unlogged2_pkey;
SELECT relname, relkind, relpersistence FROM pg_class WHERE relname ~ '^unlogged\d' ORDER BY relname;
DROP TABLE unlogged2; -- Keep this here since this is a drop for a temp table.
INSERT INTO unlogged1 VALUES (42);
CREATE UNLOGGED TABLE public.unlogged2 (a int primary key);		-- also OK
CREATE UNLOGGED TABLE pg_temp.unlogged3 (a int primary key);	-- not OK
CREATE TABLE pg_temp.implicitly_temp (a int primary key);		-- OK
CREATE TEMP TABLE explicitly_temp (a int primary key);			-- also OK
CREATE TEMP TABLE pg_temp.doubly_temp (a int primary key);		-- also OK
CREATE TEMP TABLE public.temp_to_perm (a int primary key);		-- not OK
DROP TABLE unlogged1, public.unlogged2;

CREATE TABLE as_select1 AS SELECT * FROM pg_class WHERE relkind = 'r';
CREATE TABLE as_select1 AS SELECT * FROM pg_class WHERE relkind = 'r';
CREATE TABLE IF NOT EXISTS as_select1 AS SELECT * FROM pg_class WHERE relkind = 'r';
DROP TABLE as_select1;

-- check that the oid column is added before the primary key is checked
CREATE TABLE oid_pk (f1 INT, PRIMARY KEY(oid)) WITH OIDS;
DROP TABLE oid_pk;
