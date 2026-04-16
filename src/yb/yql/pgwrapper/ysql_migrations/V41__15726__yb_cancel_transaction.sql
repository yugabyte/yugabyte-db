SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

-- Add catalog entry for the procedure yb_cancel_transaction()
INSERT INTO pg_catalog.pg_proc (
  oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
  prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
  pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
  proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
) VALUES
  (8063, 'yb_cancel_transaction', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true,
   false, 'v', 's', 1, 0, 16, '2950', NULL, NULL, '{txid}', NULL, NULL,
   'yb_cancel_transaction', NULL, NULL, NULL)
ON CONFLICT DO NOTHING;

-- Create dependency records for everything we (possibly) created.
-- Since pg_depend has no OID or unique constraint, using PL/pgSQL instead.
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT FROM pg_catalog.pg_depend
      WHERE refclassid = 1255 AND refobjid = 8063
  ) THEN
    INSERT INTO pg_catalog.pg_depend (
      classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
    ) VALUES
      (0, 0, 0, 1255, 8063, 0, 'p');
  END IF;
END $$;
