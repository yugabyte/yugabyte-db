BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel,
    pronargs, pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes,
    proargnames, proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
  ) VALUES
    (8070, 'yb_get_query_diagnostics_status', 11, 10, 12, 1, 100, 0, '-', 'f', false,
    false, true, true, 'v', 'r', 0, 0, 2249, '', '{25,25,20,1184,20,20,3802 ,25}',
    '{o,o,o,o,o,o,o,o}', '{status,description,query_id,start_time,diagnostics_interval_sec,
    bind_var_query_min_duration_ms,explain_params,path}',
    NULL, NULL, 'yb_get_query_diagnostics_status', NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  -- Create dependency records for everything we (possibly) created.
  -- Since pg_depend has no OID or unique constraint, using PL/pgSQL instead.
  DO $$
  BEGIN
    IF NOT EXISTS (
      SELECT FROM pg_catalog.pg_depend
        WHERE refclassid = 1255 AND refobjid = 8070
    ) THEN
      INSERT INTO pg_catalog.pg_depend (
        classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
      ) VALUES
        (0, 0, 0, 1255, 8070, 0, 'p');
    END IF;
  END $$;
COMMIT;

-- Creating the system view yb_query_diagnostics_status
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT TRUE FROM pg_views
    WHERE viewname = 'yb_query_diagnostics_status'
  ) THEN
    CREATE OR REPLACE VIEW pg_catalog.yb_query_diagnostics_status
    WITH (use_initdb_acl = true)
    AS
      SELECT *
      FROM yb_get_query_diagnostics_status();
  END IF;
END $$;
