SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

INSERT INTO pg_catalog.pg_proc (
  oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
  prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
  pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
  proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
) VALUES
  (8060, 'yb_fix_catalog_version_table', 11, 10, 14, 1000, 0, 0, '-', 'f',
   false, false, true, false, 'v', 'u', 1, 0, 2278, '16', NULL, NULL,
   '{per_database_mode}', NULL, NULL,
   'insert into pg_catalog.pg_yb_catalog_version select oid, 1, 1 from pg_catalog.pg_database where per_database_mode and (oid not in (select db_oid from pg_catalog.pg_yb_catalog_version)); delete from pg_catalog.pg_yb_catalog_version where (not per_database_mode and db_oid != 1) or (per_database_mode and db_oid not in (select oid from pg_catalog.pg_database))',
   NULL, NULL, '{postgres=X/postgres}')
ON CONFLICT DO NOTHING;

INSERT INTO pg_catalog.pg_init_privs (
  objoid, classoid, objsubid, privtype, initprivs
) VALUES
  (8060, 1255, 0, 'i', '{postgres=X/postgres}')
ON CONFLICT DO NOTHING;

-- Create dependency records for everything we (possibly) created.
-- Since pg_depend has no OID or unique constraint, using PL/pgSQL instead.
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT FROM pg_catalog.pg_depend
      WHERE refclassid = 1255 AND refobjid = 8060
  ) THEN
    INSERT INTO pg_catalog.pg_depend (
      classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
    ) VALUES
      (0, 0, 0, 1255, 8060, 0, 'p');
  END IF;
END $$;
