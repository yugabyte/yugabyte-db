BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang,
    procost, prorows, provariadic, protransform, prokind,
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

-- Create dependency records for the new function
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT FROM pg_catalog.pg_depend
      WHERE refclassid = 1255 AND refobjid = 8090
  ) THEN
    INSERT INTO pg_catalog.pg_depend (
      classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
    ) VALUES
      (0, 0, 0, 1255, 8090, 0, 'p');
  END IF;
END $$;
COMMIT;
