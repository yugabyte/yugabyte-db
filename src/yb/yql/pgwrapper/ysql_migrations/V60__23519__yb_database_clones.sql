BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  INSERT INTO pg_catalog.pg_proc (
  oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
  prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
  pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
  proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
  ) VALUES
  -- implementation of yb_database_clones
  (8076, 'yb_database_clones', 11, 10, 12, 1, 3, 0, '-', 'f', false, false, true, true,
  'v', 's', 0, 0, 2249, '', '{26,25,26,25,25,1184,25}', '{o,o,o,o,o,o,o}',
  '{db_oid,db_name,parent_db_oid,parent_db_name,state,as_of_time,failure_reason}',
  NULL, NULL, 'yb_database_clones', NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  -- Create dependency records for everything we (possibly) created.
  -- Since pg_depend has no OID or unique constraint, using PL/pgSQL instead.
  DO $$
  BEGIN
    IF NOT EXISTS (
      SELECT FROM pg_catalog.pg_depend
        WHERE refclassid = 1255 AND refobjid = 8076
    ) THEN
      INSERT INTO pg_catalog.pg_depend (
        classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
      ) VALUES
        (0, 0, 0, 1255, 8076, 0, 'p');
    END IF;
  END $$;
COMMIT;