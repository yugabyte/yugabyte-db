SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

INSERT INTO pg_catalog.pg_proc (
  oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
  prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
  pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
  proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
) VALUES
  -- implementation of yb_heap_stats
  (8048, 'yb_heap_stats', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   'v', 's', 0, 0, 2249, '', '{20,20,20,20,20,20,20}', '{o,o,o,o,o,o,o}', 
   '{total_heap_requested, total_heap_usage, total_heap_allocation, cached_free_memory,
    total_heap_released, PostgreSQL_memory_usage, PostgreSQL_storage_gateway_usage}',
   NULL, NULL, 'yb_heap_stats', NULL, NULL, NULL)
ON CONFLICT DO NOTHING;

-- Create dependency records for everything we (possibly) created.
-- Since pg_depend has no OID or unique constraint, using PL/pgSQL instead.
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT FROM pg_catalog.pg_depend
      WHERE refclassid = 1255 AND refobjid = 8048
  ) THEN
    INSERT INTO pg_catalog.pg_depend (
      classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
    ) VALUES
      (0, 0, 0, 1255, 8048, 0, 'p');
  END IF;
END $$;
