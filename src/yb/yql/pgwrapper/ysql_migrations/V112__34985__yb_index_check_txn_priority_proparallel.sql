-- Mark yb_index_check() PARALLEL UNSAFE and yb_get_current_transaction_priority()
-- PARALLEL RESTRICTED: neither works in a parallel worker.
BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  -- yb_index_check
  UPDATE pg_catalog.pg_proc SET proparallel = 'u' WHERE oid = 8090;

  -- yb_get_current_transaction_priority
  UPDATE pg_catalog.pg_proc SET proparallel = 'r' WHERE oid = 8049;
COMMIT;
