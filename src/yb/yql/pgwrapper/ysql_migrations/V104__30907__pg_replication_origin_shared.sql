BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic,
    prosupport, prokind, prosecdef, proleakproof, proisstrict, proretset,
    provolatile, proparallel, pronargs, pronargdefaults, prorettype, proargtypes,
    proallargtypes, proargmodes, proargnames, proargdefaults, protrftypes,
    prosrc, probin, prosqlbody, proconfig, proacl) VALUES
    (8894, 'yb_replication_origin_session_setup_shared', 11, 10, 12, 1, 0, 0,
     '-', 'f', false, false, true, false, 'v', 'u', 1, 0, 2278, '25',
     NULL, NULL, NULL, NULL, NULL,
     'yb_replication_origin_session_setup_shared', NULL, NULL, NULL, '{postgres=X/postgres}'),
    (8895, 'yb_replication_origin_session_reset_shared', 11, 10, 12, 1, 0, 0,
     '-', 'f', false, false, true, false, 'v', 'u', 0, 0, 2278, '',
     NULL, NULL, NULL, NULL, NULL,
     'yb_replication_origin_session_reset_shared', NULL, NULL, NULL, '{postgres=X/postgres}')
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES
    (8894, 1255, 0, 'set session replication origin without exclusive lock (for concurrent write tagging)'),
    (8895, 1255, 0, 'reset shared replication origin set by yb_replication_origin_session_setup_shared')
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_init_privs (
    objoid, classoid, objsubid, privtype, initprivs
  ) VALUES
    (8894, 1255, 0, 'i', '{postgres=X/postgres}'),
    (8895, 1255, 0, 'i', '{postgres=X/postgres}')
  ON CONFLICT DO NOTHING;
COMMIT;
