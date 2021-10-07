SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

-- Load collations via pg_import_system_collations. Use PL/pgSQL PERFORM to make the command
-- not return anything. In this way the command execution can return PGRES_COMMAND_OK 
-- instead of PGRES_TUPLES_OK to avoid PGConn::Execute failure.
DO $$
BEGIN
  -- TODO(myang) : replace this with a bunch of INSERTs to improve performance.
  PERFORM pg_import_system_collations('pg_catalog');
END $$;
