SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

INSERT INTO pg_catalog.pg_proc (
  oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
  prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
  pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
  proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
) VALUES
  (8059, 'yb_increment_all_db_catalog_versions', 11, 10, 14, 1000, 0, 0, '-', 'f',
   false, false, true, false, 'v', 'u', 1, 0, 2278, '16', NULL, NULL,
   '{is_breaking_change}', NULL, NULL,
   'update pg_catalog.pg_yb_catalog_version set current_version = current_version + 1, last_breaking_version = case when is_breaking_change then current_version + 1 else last_breaking_version end',
   NULL, NULL, '{postgres=X/postgres,yb_db_admin=X/postgres}')
ON CONFLICT DO NOTHING;

INSERT INTO pg_catalog.pg_init_privs (
  objoid, classoid, objsubid, privtype, initprivs
) VALUES
  (8059, 1255, 0, 'i', '{postgres=X/postgres,yb_db_admin=X/postgres}')
ON CONFLICT DO NOTHING;

-- Create dependency records for everything we (possibly) created.
-- Since pg_depend has no OID or unique constraint, using PL/pgSQL instead.
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT FROM pg_catalog.pg_depend
      WHERE refclassid = 1255 AND refobjid = 8059
  ) THEN
    INSERT INTO pg_catalog.pg_depend (
      classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
    ) VALUES
      (0, 0, 0, 1255, 8059, 0, 'p');
  END IF;
END $$;
