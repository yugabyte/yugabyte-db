BEGIN;
    SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;
    DELETE FROM pg_catalog.pg_yb_logical_client_version;
    INSERT INTO pg_catalog.pg_yb_logical_client_version(db_oid, current_version)
    VALUES (0::oid, 0);
COMMIT;
