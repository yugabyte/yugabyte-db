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
    -- implementation of yb_binary_upgrade_set_next_pg_enum_sortorder 
    (8095, 'yb_binary_upgrade_set_next_pg_enum_sortorder', 11, 10, 12,
     1, 0, 0, '-', 'f',
     false, false, true, false, 'v',
     'r', 1, 0,  2278, '700',
     NULL, NULL, NULL, NULL, NULL,
     'yb_binary_upgrade_set_next_pg_enum_sortorder', NULL, NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES (
    8095, 1255, 0, 'for use by pg_upgrade'
  ) ON CONFLICT DO NOTHING;
COMMIT;
