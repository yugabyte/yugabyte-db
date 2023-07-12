SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

INSERT INTO pg_catalog.pg_proc (oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic,
                                protransform,
                                prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel,
                                pronargs,
                                pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
                                proargdefaults, protrftypes, prosrc, probin, proconfig, proacl)
VALUES (8062, 'yb_lock_status', 11, 10, 12, 1, 1000, 0, '-', 'f', false, false, false, true,
        'v', 's', 2, 0, 2249, '26 2950', '{26,2950,25,26,26,23,1009,16,16,1184,1184,25,25,2950,23,25,16,1009,1009,21,23,16,2951}',
        '{i,i,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o}',
        '{relation,transaction_id,locktype,database,relation,pid,mode,granted,fastpath,waitstart,waitend,node,tablet_id,transaction_id,subtransaction_id,status_tablet_id,is_explicit,hash_cols,range_cols,attnum,column_id,multiple_rows_locked,blocked_by}',
        NULL, NULL, 'yb_lock_status', NULL, NULL, NULL) ON CONFLICT DO NOTHING;

-- Create dependency records for everything we (possibly) created.
-- Since pg_depend has no OID or unique constraint, using PL/pgSQL instead.
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT FROM pg_catalog.pg_depend
      WHERE refclassid = 1255 AND refobjid = 8062
  ) THEN
    INSERT INTO pg_catalog.pg_depend (
      classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
    ) VALUES
      (0, 0, 0, 1255, 8062, 0, 'p');
  END IF;
END $$;
