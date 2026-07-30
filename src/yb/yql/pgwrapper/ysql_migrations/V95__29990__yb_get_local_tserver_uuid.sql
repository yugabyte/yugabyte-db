BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic,
    prosupport, prokind, prosecdef, proleakproof, proisstrict, proretset,
    provolatile, proparallel, pronargs, pronargdefaults, prorettype, proargtypes,
    proallargtypes, proargmodes, proargnames, proargdefaults, protrftypes,
    prosrc, probin, prosqlbody, proconfig, proacl) VALUES
    (8101, 'yb_get_local_tserver_uuid', 11, 10, 12, 1, 0, 0, '-', 'f',
     false, false, true, false, 'i', 's', 0, 0, 2950, '',
     NULL, NULL, NULL, NULL, NULL,
     'yb_get_local_tserver_uuid', NULL, NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES (
    8101, 1255, 0, 'Get the UUID of the local tserver'
  ) ON CONFLICT DO NOTHING;
COMMIT;
