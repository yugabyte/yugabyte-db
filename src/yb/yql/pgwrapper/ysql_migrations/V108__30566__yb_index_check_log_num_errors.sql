-- Extend yb_index_check() with optional argument 'log_num_errors' and return index errors via SRF
BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  UPDATE pg_catalog.pg_proc SET
    proretset = true,
    prorows = 1000,
    prorettype = 2249,
    proargtypes = '26 16 23',
    proallargtypes = '{26,16,23,26,26,17,3802,17,3802,25}',
    proargmodes = '{i,i,i,t,t,t,t,t,t,t}',
    proargnames = '{indexrelid,single_snapshot_mode,log_num_errors,tablerelid,indexrelid,ybctid,table_cols,ybbasectid,index_cols,error_category}',
    pronargs = 3,
    pronargdefaults = 2,
    proargdefaults = '({CONST :consttype 16 :consttypmod -1 :constcollid 0 :constlen 1 :constbyval true :constisnull false :location 94 :constvalue 1 [ 0 0 0 0 0 0 0 0 ]} {CONST :consttype 23 :consttypmod -1 :constcollid 0 :constlen 4 :constbyval true :constisnull false :location 145 :constvalue 4 [ 0 0 0 0 0 0 0 0 ]})'
  WHERE oid = 8090;

COMMIT;
