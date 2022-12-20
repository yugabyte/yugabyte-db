SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

INSERT INTO pg_catalog.pg_proc (
  oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
  prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
  pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
  proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
) VALUES
  -- implementation of yb_pg_stat_get_backend_allocated_mem_bytes function
  (8046, 'yb_pg_stat_get_backend_allocated_mem_bytes', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   's', 'r', 1, 0, 20, '23', NULL, NULL, NULL,
   NULL, NULL, 'yb_pg_stat_get_backend_allocated_mem_bytes', NULL, NULL, NULL),
  -- implementation of yb_pg_stat_get_backend_rss_mem_bytes function
  (8047, 'yb_pg_stat_get_backend_rss_mem_bytes', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   's', 'r', 1, 0, 20, '23', NULL, NULL, NULL,
   NULL, NULL, 'yb_pg_stat_get_backend_rss_mem_bytes', NULL, NULL, NULL)
ON CONFLICT DO NOTHING;

-- Create dependency records for everything we (possibly) created.
-- Since pg_depend has no OID or unique constraint, using PL/pgSQL instead.
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT FROM pg_catalog.pg_depend
      WHERE refclassid = 1255 AND refobjid = 8046
  ) THEN
    INSERT INTO pg_catalog.pg_depend (
      classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
    ) VALUES
      (0, 0, 0, 1255, 8046, 0, 'p'),
      (0, 0, 0, 1255, 8047, 0, 'p');
  END IF;
END $$;
