BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  INSERT INTO pg_catalog.pg_proc (
  oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, prosupport,
  prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
  pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
  proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
  ) VALUES
  -- implementation of yb_database_clones
  (8076, 'yb_database_clones', 11, 10, 12, 1, 10, 0, '-', 'f', false, false, true, true,
  'v', 'r', 0, 0, 2249, '', '{26,25,26,25,25,1184,25}', '{o,o,o,o,o,o,o}',
  '{db_oid,db_name,parent_db_oid,parent_db_name,state,as_of_time,failure_reason}',
  NULL, NULL, 'yb_database_clones', NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES (
    8076, 1255, 0, 'get the YB database clones'
  ) ON CONFLICT DO NOTHING;
COMMIT;
