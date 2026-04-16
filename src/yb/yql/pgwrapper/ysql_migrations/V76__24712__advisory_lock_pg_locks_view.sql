-- Update the yb_lock_status to include the new columns (classid, objid, objsubid)
BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;
  UPDATE pg_catalog.pg_proc
  SET
      proallargtypes = '{26,2950,25,26,26,23,1009,16,16,1184,1184,25,25,2950,23,25,16,1009,1009,21,23,16,2951,26,26,21}',
      proargmodes = '{i,i,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o}',
      proargnames = '{relation,transaction_id,locktype,database,relation,pid,mode,granted,fastpath,waitstart,waitend,node,tablet_id,transaction_id,subtransaction_id,status_tablet_id,is_explicit,hash_cols,range_cols,attnum,column_id,multiple_rows_locked,blocked_by,classid,objid,objsubid}'
  WHERE oid = 8062 AND prosrc = 'yb_lock_status';
COMMIT;

-- Update the pg_locks view to include the new columns (classid, objid, objsubid)
BEGIN;
  CREATE OR REPLACE VIEW pg_catalog.pg_locks
      WITH (use_initdb_acl = true)
  AS
  SELECT
      l.locktype,
      l.database,
      l.relation,
      NULL::int AS page,
      NULL::smallint AS tuple,
      NULL::text AS virtualxid,
      NULL::xid AS transactionid,
      l.classid,          -- Now included from yb_lock_status
      l.objid,            -- Now included from yb_lock_status
      l.objsubid,         -- Now included from yb_lock_status
      NULL::text AS virtualtransaction,
      l.pid,
      array_to_string(l.mode, ',') AS mode,
      l.granted,
      l.fastpath,
      l.waitstart,
      l.waitend,
      jsonb_build_object(
          'node', l.node,
          'transactionid', l.transaction_id,
          'subtransaction_id', l.subtransaction_id,
          'is_explicit', l.is_explicit,
          'tablet_id', l.tablet_id,
          'blocked_by', l.blocked_by,
          'keyrangedetails', jsonb_build_object(
              'cols', to_jsonb(l.hash_cols || l.range_cols),
              'attnum', l.attnum,
              'column_id', l.column_id,
              'multiple_rows_locked', l.multiple_rows_locked
          )
      ) AS ybdetails
  FROM
      pg_catalog.yb_lock_status(NULL, NULL) AS l;
COMMIT;
