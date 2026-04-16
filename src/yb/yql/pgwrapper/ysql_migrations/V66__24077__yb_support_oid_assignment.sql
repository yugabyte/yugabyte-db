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
    -- implementation of yb_xcluster_set_next_oid_assignments
    (8079, 'yb_xcluster_set_next_oid_assignments', 11, 10, 12,
    1, 0, 0, '-', 'f',
    false, false, true, false, 'v',
     'r', 1, 0, 2278, '25',
     NULL, NULL, NULL, NULL, NULL,
    'yb_xcluster_set_next_oid_assignments', NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES (
    8079, 1255, 0, 'private function used by xCluster to control OID assignment'
  ) ON CONFLICT DO NOTHING;
COMMIT;
