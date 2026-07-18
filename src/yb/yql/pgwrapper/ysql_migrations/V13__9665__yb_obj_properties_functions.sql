SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

INSERT INTO pg_catalog.pg_proc (
  oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
  prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
  pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
  proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
) VALUES
  -- implementation of yb_table_properties
  (8033, 'yb_table_properties', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   's', 's', 1, 0, 2249, '26', '{26,20,20,16}', '{i,o,o,o}',
   '{table_oid,num_tablets,num_hash_key_columns,is_colocated}',
   NULL, NULL, 'yb_table_properties', NULL, NULL, NULL),
  -- implementation of yb_is_database_colocated
  (8034, 'yb_is_database_colocated', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   's', 's', 0, 0, 16, '', NULL, NULL, NULL,
   NULL, NULL, 'yb_is_database_colocated', NULL, NULL, NULL) 
ON CONFLICT DO NOTHING;

-- Create dependency records for everything we (possibly) created.
-- Since pg_depend has no OID or unique constraint, using PL/pgSQL instead.
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT FROM pg_catalog.pg_depend
      WHERE refclassid = 1255 AND refobjid = 8033
  ) THEN
    INSERT INTO pg_catalog.pg_depend (
      classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
    ) VALUES
      (0, 0, 0, 1255, 8033, 0, 'p'),
      (0, 0, 0, 1255, 8034, 0, 'p');
  END IF;
END $$;
