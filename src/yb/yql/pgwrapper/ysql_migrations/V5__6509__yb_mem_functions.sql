SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

INSERT INTO pg_catalog.pg_proc (
  oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
  prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
  pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
  proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
) VALUES
  (8013, 'yb_getrusage', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   'v', 's', 0, 0, 2249, '', '{20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20}',
   '{o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o}',
   '{user_cpu,system_cpu,maxrss,ixrss,idrss,isrss,minflt,majflt,nswap,inblock,oublock,msgsnd,msgrcv,nsignals,nvcsw,nivcsw}',
   NULL, NULL, 'yb_getrusage', NULL, NULL, NULL),
  (8014, 'yb_mem_usage', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   'v', 's', 0, 0, 25, '', NULL, NULL, NULL, NULL, NULL, 'yb_mem_usage', NULL, NULL, NULL),
  (8015, 'yb_mem_usage_kb', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   'v', 's', 0, 0, 20, '', NULL, NULL, NULL, NULL, NULL, 'yb_mem_usage_kb', NULL, NULL, NULL),
  (8016, 'yb_mem_usage_sql', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   'v', 's', 0, 0, 25, '', NULL, NULL, NULL, NULL, NULL, 'yb_mem_usage_sql', NULL, NULL, NULL),
  (8017, 'yb_mem_usage_sql_b', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   'v', 's', 0, 0, 20, '', NULL, NULL, NULL, NULL, NULL, 'yb_mem_usage_sql_b', NULL, NULL, NULL),
  (8018, 'yb_mem_usage_sql_kb', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   'v', 's', 0, 0, 20, '', NULL, NULL, NULL, NULL, NULL, 'yb_mem_usage_sql_kb', NULL, NULL, NULL)
ON CONFLICT DO NOTHING;

-- Create dependency records for everything we (possibly) created.
-- Since pg_depend has no OID or unique constraint, using PL/pgSQL instead.
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT FROM pg_catalog.pg_depend
      WHERE refclassid = 1255 AND refobjid = 8013
  ) THEN
    INSERT INTO pg_catalog.pg_depend (
      classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
    ) VALUES
      (0, 0, 0, 1255, 8013, 0, 'p'),
      (0, 0, 0, 1255, 8014, 0, 'p'),
      (0, 0, 0, 1255, 8015, 0, 'p'),
      (0, 0, 0, 1255, 8016, 0, 'p'),
      (0, 0, 0, 1255, 8017, 0, 'p'),
      (0, 0, 0, 1255, 8018, 0, 'p');
  END IF;
END $$;
