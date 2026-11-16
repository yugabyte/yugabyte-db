-- YB: pg_partman's functions call pg_catalog.yb_pg_partman_check_native_txn_ddl_and_locks(). A new
-- cluster has it from initdb, but an upgraded one gets it from migration V111 only when the upgrade
-- is finalized, so refuse to install or update pg_partman while it is missing.
DO $$
BEGIN
    IF to_regprocedure('pg_catalog.yb_pg_partman_check_native_txn_ddl_and_locks()') IS NULL THEN
        RAISE EXCEPTION 'This version of pg_partman needs pg_catalog.yb_pg_partman_check_native_txn_ddl_and_locks(), which yb-admin upgrade_ysql adds when the upgrade is finalized. Finalize the upgrade and try again.';
    END IF;
END
$$;
