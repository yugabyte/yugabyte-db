SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

DO $$
BEGIN
  -- The pg_yb_catalog_version will be upgraded so that it has one row per database.
  -- Use the word pg_global in this comment to trigger a heartbeat wait to
  -- propagate new catalog versions. The ysql_upgrade code searches for
  -- pg_global and introduces extra wait time if found for shared relations.
  PERFORM pg_catalog.yb_fix_catalog_version_table(true);
END $$;
