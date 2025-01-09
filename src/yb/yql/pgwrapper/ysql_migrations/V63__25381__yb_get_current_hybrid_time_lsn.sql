BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  -- Even though it is expected that no such method will exist,
  -- we need to delete it to avoid conflicts.
  DELETE FROM pg_catalog.pg_proc WHERE proname = 'yb_get_current_hybrid_time_lsn' AND
    pronamespace = 'pg_catalog'::regnamespace;
  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, prosupport,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel,
    pronargs, pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes,
    proargnames, proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
  ) VALUES (
    8078, 'yb_get_current_hybrid_time_lsn', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true,
    true, 'v', 's', 0, 0, 20, '', '{20}', '{o}', '{current_hybrid_time_lsn}',
    NULL, NULL, 'yb_get_current_hybrid_time_lsn', NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES (
    8078, 1255, 0, 'returns current hybrid time'
  ) ON CONFLICT DO NOTHING;
COMMIT;
