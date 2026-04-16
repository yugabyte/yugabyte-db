BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, prosupport,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
    pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
    proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
  ) VALUES
  (8077, 'yb_cancel_query_diagnostics', 11, 10, 12, 1, 0, 0, 0, 'f', false,
  false, true, false, 'v', 's', 1, 0, 2278, '20', NULL, NULL, '{query_id}', NULL,
  NULL, 'yb_cancel_query_diagnostics', NULL, NULL,
  '{postgres=X/postgres,yb_db_admin=X/postgres}')
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES (
    8077, 1255, 0, 'cancels query diagnostics bundle'
  ) ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_init_privs (
    objoid, classoid, objsubid, privtype, initprivs
  ) VALUES (
    8077, 1255, 0, 'i', '{postgres=X/postgres,yb_db_admin=X/postgres}'
  ) ON CONFLICT DO NOTHING;
COMMIT;
