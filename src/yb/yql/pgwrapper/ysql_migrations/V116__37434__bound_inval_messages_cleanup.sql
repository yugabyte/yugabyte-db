-- Bound the pg_yb_invalidation_messages cleanup in
-- yb_increment_db_catalog_version_with_inval_messages to the last 4096
-- versions, so a cheap DDL no longer scans every expired row and tombstone.
BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  UPDATE pg_catalog.pg_proc SET
  prosrc = 'with changed_version as (update pg_yb_catalog_version set current_version = current_version + 1, last_breaking_version = case when is_breaking_change then current_version + 1 else last_breaking_version end where pg_yb_catalog_version.db_oid = yb_increment_db_catalog_version_with_inval_messages.db_oid returning pg_yb_catalog_version.db_oid, pg_yb_catalog_version.current_version), expired as (delete from pg_yb_invalidation_messages where pg_yb_invalidation_messages.db_oid = yb_increment_db_catalog_version_with_inval_messages.db_oid and pg_yb_invalidation_messages.current_version >= (select changed_version.current_version from changed_version) - 4096 and (message_time < extract(epoch from clock_timestamp())::bigint - expiration_sec or pg_yb_invalidation_messages.current_version = (select changed_version.current_version from changed_version) - 4096)) insert into pg_yb_invalidation_messages select changed_version.db_oid, current_version, extract(epoch from clock_timestamp())::bigint, messages from changed_version returning current_version'
  WHERE oid = 8083;
COMMIT;
