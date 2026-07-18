BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang,
    procost, prorows, provariadic, prosupport, prokind,
    prosecdef, proleakproof, proisstrict, proretset, provolatile,
    proparallel, pronargs, pronargdefaults, prorettype, proargtypes,
    proallargtypes, proargmodes, proargnames, proargdefaults, protrftypes,
    prosrc, probin, prosqlbody, proconfig, proacl
  ) VALUES
    -- implementation of yb_binary_upgrade_set_next_colocation_id
    (8100, 'yb_binary_upgrade_set_next_colocation_id', 11, 10, 12,
     1, 0, 0, '-', 'f',
     false, false, true, false, 'v',
     'u', 1, 0,  2278, '26',
     NULL, NULL, NULL, NULL, NULL,
     'yb_binary_upgrade_set_next_colocation_id', NULL, NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES (
    8100, 1255, 0, 'for use by pg_upgrade'
  ) ON CONFLICT DO NOTHING;
COMMIT;
