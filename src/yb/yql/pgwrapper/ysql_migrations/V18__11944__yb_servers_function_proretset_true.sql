SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

-- Replace with the following when issue #11105 is fixed.
---- pg_proc.oid=8019 refers to yb_servers function.
-- UPDATE pg_catalog.pg_proc SET proretset = true WHERE oid = 8019;

-- This should target a single row since the filter has all the key columns of
-- unique index pg_proc_proname_args_nsp_index.
UPDATE pg_catalog.pg_proc SET proretset = true
WHERE (proname = 'yb_servers'
       AND proargtypes = ''
       AND pronamespace = 'pg_catalog'::regnamespace);
