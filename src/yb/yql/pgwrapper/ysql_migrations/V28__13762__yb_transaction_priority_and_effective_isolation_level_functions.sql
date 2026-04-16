SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

-- Add catalog entries for the procedure yb_get_current_transaction_priority() and
-- yb_get_effective_transaction_isolation_level()
INSERT INTO pg_catalog.pg_proc (
  oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
  prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
  pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
  proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
) VALUES
  (8049, 'yb_get_current_transaction_priority', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true,
   false, 'v', 's', 0, 0, 2275, '', NULL, NULL, NULL, NULL, NULL,
   'yb_get_current_transaction_priority', NULL, NULL, NULL),
  (8050, 'yb_get_effective_transaction_isolation_level', 11, 10, 12, 1, 0, 0, '-', 'f', false,
   false, true, false, 'v', 's', 0, 0, 2275, '', NULL, NULL, NULL, NULL, NULL,
   'yb_get_effective_transaction_isolation_level', NULL, NULL, NULL)
ON CONFLICT DO NOTHING;

-- Create dependency records for everything we (possibly) created.
-- Since pg_depend has no OID or unique constraint, using PL/pgSQL instead.
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT FROM pg_catalog.pg_depend
      WHERE refclassid = 1255 AND refobjid = 8049
  ) THEN
    INSERT INTO pg_catalog.pg_depend (
      classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
    ) VALUES
      (0, 0, 0, 1255, 8049, 0, 'p'),
      (0, 0, 0, 1255, 8050, 0, 'p');
  END IF;
END $$;
