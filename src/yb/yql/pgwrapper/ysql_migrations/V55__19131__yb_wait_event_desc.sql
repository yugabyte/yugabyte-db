BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel,
    pronargs, pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes,
    proargnames, proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
  ) VALUES (
    8069, 'yb_wait_event_desc', 11, 10, 12, 1, 100, 0, '-', 'f', false,
    false, true, true, 'i', 'r', 0, 0, 2249, '', '{25,25,25,25}',
    '{o,o,o,o}', '{wait_event_class,wait_event_type,wait_event,
    wait_event_description}',
    NULL, NULL, 'yb_wait_event_desc', NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  -- Create dependency records for everything we (possibly) created.
  -- Since pg_depend has no OID or unique constraint, using PL/pgSQL instead.
  DO $$
  BEGIN
    IF NOT EXISTS (
      SELECT FROM pg_catalog.pg_depend
        WHERE refclassid = 1255 AND refobjid = 8069
    ) THEN
      INSERT INTO pg_catalog.pg_depend (
        classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
      ) VALUES
        (0, 0, 0, 1255, 8069, 0, 'p');
    END IF;
  END $$;
COMMIT;

-- Creating the system view yb_wait_event_desc
CREATE OR REPLACE VIEW pg_catalog.yb_wait_event_desc WITH (use_initdb_acl = true) AS
  SELECT *
  FROM yb_wait_event_desc();
