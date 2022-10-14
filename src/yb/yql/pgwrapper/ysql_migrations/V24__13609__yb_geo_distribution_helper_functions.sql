SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

INSERT INTO pg_catalog.pg_proc (
  oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
  prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
  pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
  proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
) VALUES
  -- implementation of yb_server_region
  (8042, 'yb_server_region', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   's', 's', 0, 0, 1043, '', NULL, NULL, NULL,
   NULL, NULL, 'yb_server_region', NULL, NULL, NULL),
  -- implementation of yb_server_cloud
  (8043, 'yb_server_cloud', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   's', 's', 0, 0, 1043, '', NULL, NULL, NULL,
   NULL, NULL, 'yb_server_cloud', NULL, NULL, NULL),
  -- implementation of yb_server_zone
  (8044, 'yb_server_zone', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   's', 's', 0, 0, 1043, '', NULL, NULL, NULL,
   NULL, NULL, 'yb_server_zone', NULL, NULL, NULL)
ON CONFLICT DO NOTHING;

-- Create dependency records for everything we (possibly) created.
-- Since pg_depend has no OID or unique constraint, using PL/pgSQL instead.
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT FROM pg_catalog.pg_depend
      WHERE refclassid = 1255 AND refobjid = 8042
  ) THEN
    INSERT INTO pg_catalog.pg_depend (
      classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
    ) VALUES
      (0, 0, 0, 1255, 8042, 0, 'p'),
      (0, 0, 0, 1255, 8043, 0, 'p'),
      (0, 0, 0, 1255, 8044, 0, 'p');
  END IF;
END $$;
