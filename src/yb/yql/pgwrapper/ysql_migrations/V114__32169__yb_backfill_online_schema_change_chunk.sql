BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  -- EXECUTE is revoked from public; only the tserver's copy connection, as
  -- postgres, calls it, and the function itself rejects any other caller.
  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang,
    procost, prorows, provariadic, prosupport, prokind,
    prosecdef, proleakproof, proisstrict, proretset, provolatile,
    proparallel, pronargs, pronargdefaults, prorettype, proargtypes,
    proallargtypes, proargmodes,
    proargnames,
    proargdefaults, protrftypes,
    prosrc, probin, prosqlbody, proconfig, proacl
  ) VALUES
    (8123, 'yb_backfill_online_schema_change_chunk', 11, 10, 12,
     1, 0, 0, '-', 'f',
     false, false, false, false, 'v',
     'u', 6, 0, 2249, '26 26 26 20 17 17',
     '{26,26,26,20,17,17,20,17}', '{i,i,i,i,i,i,o,o}',
     '{source_table,target_table,transform_fn,read_time_ht,partition_key,bfinstr,rows_processed,next_bfinstr}',
     NULL, NULL,
     'yb_backfill_online_schema_change_chunk', NULL, NULL, NULL,
     '{postgres=X/postgres}')
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES (
    8123, 1255, 0,
    'backfill one chunk of an online schema change source tablet; returns rows_processed and the next bfinstr (NULL when the tablet scan is complete)'
  ) ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_init_privs (
    objoid, classoid, objsubid, privtype, initprivs
  ) VALUES (
    8123, 1255, 0, 'i', '{postgres=X/postgres}'
  ) ON CONFLICT DO NOTHING;
COMMIT;
