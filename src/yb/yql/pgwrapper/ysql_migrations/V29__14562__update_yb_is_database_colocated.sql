SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

-- (1) TODO: when issue #11105 is fixed, use WHERE oid = 8034.
-- 8034 is the oid of function: yb_is_database_colocated.
-- (2) The current query should target a single row since the filter has all the key columns of
-- unique index pg_proc_proname_args_nsp_index.
-- (3) Setting the field proargdefaults is equivalent to running the following query in
-- yb_system_views.sql in initdb to provide a default value for the input argument.
-- CREATE OR REPLACE FUNCTION
--   yb_is_database_colocated(check_legacy DEFAULT false)
-- RETURNS bool
-- LANGUAGE INTERNAL
-- STRICT STABLE PARALLEL SAFE
-- AS 'yb_is_database_colocated';
UPDATE pg_catalog.pg_proc SET
  pronargs = 1,
  pronargdefaults = 1,
  proargtypes = '16',
  proargnames = '{check_legacy}',
  proargdefaults = '({CONST :consttype 16 :consttypmod -1 :constcollid 0 :constlen 1 :constbyval true :constisnull false :location 83 :constvalue 1 [ 0 0 0 0 0 0 0 0 ]})'
WHERE proname = 'yb_is_database_colocated' AND proargtypes = '' AND pronamespace = 'pg_catalog'::regnamespace;
