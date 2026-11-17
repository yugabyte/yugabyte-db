--
-- pgrowlocks and pgstattuple read heap pages directly, so they are installed
-- as libraries only: they can no longer be created, but a database restored
-- from a backup that has them keeps working.  Their functions reject YB
-- relations and keep working on temp tables.
--
\set VERBOSITY terse
CREATE EXTENSION pgrowlocks;
CREATE EXTENSION pgstattuple;
\set VERBOSITY default
SELECT name FROM pg_available_extensions
  WHERE name IN ('pgrowlocks', 'pgstattuple');

-- Recreate the extensions the way a backup restore does.
SET yb_binary_restore = true;
SET yb_non_ddl_txn_for_sys_tables_allowed = true;
SELECT binary_upgrade_create_empty_extension('pgrowlocks', 'public', true, '1.2', NULL, NULL, ARRAY[]::text[]);
CREATE FUNCTION pgrowlocks(relname text, OUT locked_row tid, OUT locker xid, OUT multi boolean, OUT xids xid[], OUT modes text[], OUT pids integer[]) RETURNS SETOF record
    LANGUAGE c STRICT PARALLEL SAFE
    AS '$libdir/pgrowlocks', 'pgrowlocks';
ALTER EXTENSION pgrowlocks ADD FUNCTION pgrowlocks(text);
SELECT binary_upgrade_create_empty_extension('pgstattuple', 'public', true, '1.5', NULL, NULL, ARRAY[]::text[]);
CREATE FUNCTION pgstattuple(reloid regclass, OUT table_len bigint, OUT tuple_count bigint, OUT tuple_len bigint, OUT tuple_percent double precision, OUT dead_tuple_count bigint, OUT dead_tuple_len bigint, OUT dead_tuple_percent double precision, OUT free_space bigint, OUT free_percent double precision) RETURNS record
    LANGUAGE c STRICT PARALLEL SAFE
    AS '$libdir/pgstattuple', 'pgstattuplebyid_v1_5';
ALTER EXTENSION pgstattuple ADD FUNCTION pgstattuple(regclass);
CREATE FUNCTION pgstattuple_approx(reloid regclass, OUT table_len bigint, OUT scanned_percent double precision, OUT approx_tuple_count bigint, OUT approx_tuple_len bigint, OUT approx_tuple_percent double precision, OUT dead_tuple_count bigint, OUT dead_tuple_len bigint, OUT dead_tuple_percent double precision, OUT approx_free_space bigint, OUT approx_free_percent double precision) RETURNS record
    LANGUAGE c STRICT PARALLEL SAFE
    AS '$libdir/pgstattuple', 'pgstattuple_approx_v1_5';
ALTER EXTENSION pgstattuple ADD FUNCTION pgstattuple_approx(regclass);
CREATE FUNCTION pg_relpages(relname regclass) RETURNS bigint
    LANGUAGE c STRICT PARALLEL SAFE
    AS '$libdir/pgstattuple', 'pg_relpagesbyid_v1_5';
ALTER EXTENSION pgstattuple ADD FUNCTION pg_relpages(regclass);
CREATE FUNCTION pgstatindex(relname regclass, OUT version integer, OUT tree_level integer, OUT index_size bigint, OUT root_block_no bigint, OUT internal_pages bigint, OUT leaf_pages bigint, OUT empty_pages bigint, OUT deleted_pages bigint, OUT avg_leaf_density double precision, OUT leaf_fragmentation double precision) RETURNS record
    LANGUAGE c STRICT PARALLEL SAFE
    AS '$libdir/pgstattuple', 'pgstatindexbyid_v1_5';
ALTER EXTENSION pgstattuple ADD FUNCTION pgstatindex(regclass);
RESET yb_non_ddl_txn_for_sys_tables_allowed;
RESET yb_binary_restore;
SELECT extname, extversion FROM pg_extension
  WHERE extname IN ('pgrowlocks', 'pgstattuple') ORDER BY 1;

CREATE TABLE yb_t (k int PRIMARY KEY, v int);
CREATE INDEX yb_t_v_idx ON yb_t (v);
INSERT INTO yb_t SELECT g, g FROM generate_series(1, 10) g;

-- pgrowlocks
SELECT * FROM pgrowlocks('yb_t');
BEGIN;
SELECT k FROM yb_t WHERE k = 1 FOR UPDATE;
SELECT * FROM pgrowlocks('yb_t');
ROLLBACK;

-- pgstattuple
SELECT * FROM pgstattuple('yb_t');
SELECT * FROM pgstattuple('yb_t_pkey');
SELECT * FROM pgstattuple_approx('yb_t');
SELECT pg_relpages('yb_t');
SELECT pg_relpages('yb_t_v_idx');
SELECT * FROM pgstatindex('yb_t_v_idx');

-- Temp tables are heap tables and keep working
CREATE TEMP TABLE tmp_t (k int PRIMARY KEY, v int);
INSERT INTO tmp_t SELECT g, g FROM generate_series(1, 10) g;
BEGIN;
SELECT k FROM tmp_t WHERE k IN (1, 2) FOR UPDATE;
SELECT locked_row, multi, modes FROM pgrowlocks('tmp_t');
COMMIT;
SELECT tuple_count, dead_tuple_count FROM pgstattuple('tmp_t');
SELECT table_len > 0 AS has_pages FROM pgstattuple_approx('tmp_t');
SELECT pg_relpages('tmp_t');
SELECT leaf_pages FROM pgstatindex('tmp_t_pkey');

-- DROP EXTENSION works without the control file
DROP TABLE tmp_t;
DROP TABLE yb_t;
DROP EXTENSION pgstattuple;
DROP EXTENSION pgrowlocks;
