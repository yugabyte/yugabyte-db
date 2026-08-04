BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  -- yb_tablegroup_size(oid): leader SST+WAL bytes for a tablegroup's
  -- colocation parent tablet (same semantics as GetTableDiskSize).
  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic,
    prosupport, prokind, prosecdef, proleakproof, proisstrict, proretset,
    provolatile, proparallel, pronargs, pronargdefaults, prorettype, proargtypes,
    proallargtypes, proargmodes, proargnames, proargdefaults, protrftypes,
    prosrc, probin, prosqlbody, proconfig, proacl) VALUES
    (8116, 'yb_tablegroup_size', 11, 10, 12, 1, 0, 0, '-', 'f',
     false, false, true, false, 'v', 's', 1, 0, 20, '26',
     NULL, NULL, NULL, NULL, NULL,
     'yb_tablegroup_size', NULL, NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES (
    8116, 1255, 0,
    'Disk size in bytes of a tablegroup colocation parent tablet (leader SST+WAL)'
  ) ON CONFLICT DO NOTHING;

  -- Create dependency records for everything we (possibly) created.
  DO $$
  BEGIN
    IF NOT EXISTS (
      SELECT FROM pg_catalog.pg_depend
        WHERE refclassid = 1255 AND refobjid = 8116
    ) THEN
      INSERT INTO pg_catalog.pg_depend (
        classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
      ) VALUES
        (0, 0, 0, 1255, 8116, 0, 'p');
    END IF;
  END $$;
COMMIT;
