--
-- pgrowlocks reads heap pages directly, so it is installed as a library only:
-- it can no longer be created, but a database restored from a backup that has
-- it keeps working.  pgrowlocks() rejects YB tables and keeps working on temp
-- tables.
--
\set VERBOSITY terse
CREATE EXTENSION pgrowlocks;
\set VERBOSITY default
SELECT name FROM pg_available_extensions WHERE name = 'pgrowlocks';

-- Recreate the extension the way a backup restore does.
SET yb_binary_restore = true;
SET yb_non_ddl_txn_for_sys_tables_allowed = true;
SELECT binary_upgrade_create_empty_extension('pgrowlocks', 'public', true, '1.2', NULL, NULL, ARRAY[]::text[]);
CREATE FUNCTION pgrowlocks(relname text, OUT locked_row tid, OUT locker xid, OUT multi boolean, OUT xids xid[], OUT modes text[], OUT pids integer[]) RETURNS SETOF record
    LANGUAGE c STRICT PARALLEL SAFE
    AS '$libdir/pgrowlocks', 'pgrowlocks';
ALTER EXTENSION pgrowlocks ADD FUNCTION pgrowlocks(text);
RESET yb_non_ddl_txn_for_sys_tables_allowed;
RESET yb_binary_restore;
SELECT extname, extversion FROM pg_extension WHERE extname = 'pgrowlocks';

CREATE TABLE yb_t (k int PRIMARY KEY, v int);
INSERT INTO yb_t SELECT g, g FROM generate_series(1, 10) g;
SELECT * FROM pgrowlocks('yb_t');
BEGIN;
SELECT k FROM yb_t WHERE k = 1 FOR UPDATE;
SELECT * FROM pgrowlocks('yb_t');
ROLLBACK;

-- Temp tables are heap tables and keep working
CREATE TEMP TABLE tmp_t (k int PRIMARY KEY, v int);
INSERT INTO tmp_t SELECT g, g FROM generate_series(1, 10) g;
BEGIN;
SELECT k FROM tmp_t WHERE k IN (1, 2) FOR UPDATE;
SELECT locked_row, multi, modes FROM pgrowlocks('tmp_t');
COMMIT;

-- DROP EXTENSION works without the control file
DROP TABLE tmp_t;
DROP TABLE yb_t;
DROP EXTENSION pgrowlocks;
