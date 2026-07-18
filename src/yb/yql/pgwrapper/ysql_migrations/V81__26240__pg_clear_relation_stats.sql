BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  -- TODO: As a workaround for GHI #13500, we perform a delete + insert instead
  -- of an update into pg_proc. Restore to UPDATE once fixed.

  DELETE FROM pg_catalog.pg_proc WHERE proname = 'pg_clear_relation_stats' AND
    pronamespace = 'pg_catalog'::regnamespace;
  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, prosupport,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
    pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
    proargdefaults, protrftypes, prosrc, probin, prosqlbody, proconfig, proacl
  ) VALUES
    (8092, 'pg_clear_relation_stats', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, false, false,
    'v', 'u', 2, 0, 2278, '25 25', NULL, NULL, '{schemaname,relname}', NULL, NULL, 'pg_clear_relation_stats',
    NULL, NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  -- TODO: As a workaround for GHI #13500, we perform a delete + insert instead
  -- of an update into pg_proc. Restore to UPDATE once fixed.

  DELETE FROM pg_catalog.pg_proc WHERE proname = 'pg_clear_attribute_stats' AND
    pronamespace = 'pg_catalog'::regnamespace;
  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, prosupport,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
    pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
    proargdefaults, protrftypes, prosrc, probin, prosqlbody, proconfig, proacl
  ) VALUES
    (8094, 'pg_clear_attribute_stats', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, false, false,
    'v', 'u', 4, 0, 2278, '25 25 25 16', NULL, NULL, '{schemaname,relname,attname,inherited}', NULL, NULL,
    'pg_clear_attribute_stats', NULL, NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

COMMIT;
