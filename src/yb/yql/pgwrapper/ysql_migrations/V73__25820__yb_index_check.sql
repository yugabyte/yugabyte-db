BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang,
    procost, prorows, provariadic, prosupport, prokind,
    prosecdef, proleakproof, proisstrict, proretset, provolatile,
    proparallel, pronargs, pronargdefaults, prorettype, proargtypes,
    proallargtypes, proargmodes, proargnames, proargdefaults, protrftypes,
    prosrc, probin, proconfig, proacl
  ) VALUES
    -- implementation of yb_index_check
    (8090, 'yb_index_check', 11, 10, 12,
     1, 0, 0, '-', 'f',
     false, false, true, false, 's',
     's', 1, 0, '2278', '26',
     NULL, NULL, NULL, NULL, NULL,
     'yb_index_check', NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES (
    8090, 1255, 0, 'returns whether the index is logically consistent with its base relation'
  ) ON CONFLICT DO NOTHING;
COMMIT;
