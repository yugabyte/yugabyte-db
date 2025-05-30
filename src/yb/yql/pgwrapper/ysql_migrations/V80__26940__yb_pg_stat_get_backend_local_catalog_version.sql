BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, prosupport,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
    pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
    proargdefaults, protrftypes, prosrc, probin, prosqlbody, proconfig, proacl
  ) VALUES
    (8096, 'yb_pg_stat_get_backend_local_catalog_version', 11, 10, 12, 1, 100, 0, '-', 'f', false, false, false,
     true, 's', 'r', 1, 0, 2249, '23', '{23,26,20}', '{i,o,o}', '{pid,datid,local_catalog_version}', NULL, NULL,
     'yb_pg_stat_get_backend_local_catalog_version', NULL, NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES (
    8096, 1255, 0, 'statistics: datid and local_catalog_version of backends'
  ) ON CONFLICT DO NOTHING;
COMMIT;
