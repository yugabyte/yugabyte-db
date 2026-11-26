-- Helper shared by yb.orig.yb_index_check*.sql tests.
-- Defines :force_cache_refresh for invalidating catalog caches after
-- changes to pg_index which are used to induce index inconsistencies.
SELECT oid AS db_oid FROM pg_database WHERE datname = (
    SELECT CASE
        WHEN COUNT(*) = 1 THEN 'template1'
        ELSE current_database() END FROM pg_yb_catalog_version) \gset
SELECT
$force_cache_refresh$
SET yb_non_ddl_txn_for_sys_tables_allowed TO on;
UPDATE pg_yb_catalog_version
   SET current_version       = current_version + 1,
       last_breaking_version = current_version + 1
 WHERE db_oid = :db_oid;
RESET yb_non_ddl_txn_for_sys_tables_allowed;
DO
$$
BEGIN
    PERFORM pg_sleep(1);
END;
$$;
$force_cache_refresh$ AS force_cache_refresh \gset
