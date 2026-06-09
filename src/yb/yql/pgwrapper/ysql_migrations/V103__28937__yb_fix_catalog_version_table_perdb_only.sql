SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

-- yb_fix_catalog_version_table(per_database_mode boolean) is now per-database-only:
-- the legacy global / shared catalog version mode has been removed, so the
-- per_database_mode argument is no longer meaningful. The function signature is
-- preserved for backward compatibility with existing callers that still pass a
-- value; the body now ignores the argument and always synchronizes
-- pg_yb_catalog_version with pg_database (one row per database).
UPDATE pg_catalog.pg_proc SET
  prosrc = 'insert into pg_catalog.pg_yb_catalog_version select oid, 1, 1 from pg_catalog.pg_database where oid not in (select db_oid from pg_catalog.pg_yb_catalog_version); delete from pg_catalog.pg_yb_catalog_version where db_oid not in (select oid from pg_catalog.pg_database)'
WHERE proname = 'yb_fix_catalog_version_table'
  AND proargtypes = '16'
  AND pronamespace = 'pg_catalog'::regnamespace;

-- Keep pg_description in sync with the updated `descr` field in pg_proc.dat.
UPDATE pg_catalog.pg_description SET
  description = 'sync pg_yb_catalog_version with pg_database (one row per database)'
WHERE objoid = 8060 AND classoid = 1255 AND objsubid = 0;
