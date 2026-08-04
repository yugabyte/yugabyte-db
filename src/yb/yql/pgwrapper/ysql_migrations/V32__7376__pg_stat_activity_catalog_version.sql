BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
    pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
    proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
  ) VALUES
    (8035, 'yb_pg_stat_get_backend_catalog_version', 11, 10, 12, 1, 0, 0, '-',
     'f', false, false, true, false, 's', 'r', 1,
     0, 20, '23', NULL, NULL, NULL,
     NULL, NULL, 'yb_pg_stat_get_backend_catalog_version', NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  -- Create dependency records for everything we (possibly) created.
  -- Since pg_depend has no OID or unique constraint, using PL/pgSQL instead.
  DO $$
  BEGIN
    IF NOT EXISTS (
      SELECT FROM pg_catalog.pg_depend
        WHERE refclassid = 1255 AND refobjid = 8035
    ) THEN
      INSERT INTO pg_catalog.pg_depend (
        classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
      ) VALUES (
        0, 0, 0, 1255, 8035, 0, 'p'
      );
    END IF;
  END $$;
COMMIT;

-- (Using the same indentation as the definition in yb_system_views.sql.)
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT TRUE FROM pg_attribute
    WHERE attrelid = 'pg_catalog.pg_stat_activity'::regclass
          AND attname = 'catalog_version'
          AND NOT attisdropped
  ) THEN
    CREATE OR REPLACE VIEW pg_catalog.pg_stat_activity
    WITH (use_initdb_acl = true)
    AS
        SELECT
            S.datid AS datid,
            D.datname AS datname,
            S.pid,
            S.usesysid,
            U.rolname AS usename,
            S.application_name,
            S.client_addr,
            S.client_hostname,
            S.client_port,
            S.backend_start,
            S.xact_start,
            S.query_start,
            S.state_change,
            S.wait_event_type,
            S.wait_event,
            S.state,
            S.backend_xid,
            s.backend_xmin,
            S.query,
            S.backend_type,
            yb_pg_stat_get_backend_catalog_version(B.beid) AS catalog_version
        FROM pg_stat_get_activity(NULL) AS S
            LEFT JOIN pg_database AS D ON (S.datid = D.oid)
            LEFT JOIN pg_authid AS U ON (S.usesysid = U.oid)
            LEFT JOIN (pg_stat_get_backend_idset() beid CROSS JOIN
                      pg_stat_get_backend_pid(beid) pid) B ON B.pid = S.pid;
  END IF;
END $$;
