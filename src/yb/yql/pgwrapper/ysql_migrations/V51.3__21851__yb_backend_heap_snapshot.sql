BEGIN;
    SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

    INSERT INTO pg_catalog.pg_proc (
      oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
      prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
      pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
      proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
    ) VALUES
      (8085, 'yb_backend_heap_snapshot', 11, 10, 12, 1, 100000, 0, '-', 'f', false, false, true, true,
      'v', 'r', 0, 0, 2249, '', '{20,20,20,20,20,25}', '{o,o,o,o,o,o}',
      '{estimated_bytes,estimated_count,avg_bytes_per_allocation,sampled_bytes,sample_count,call_stack}',
      NULL, NULL, 'yb_backend_heap_snapshot', NULL, NULL, NULL)
    ON CONFLICT DO NOTHING;

    -- Insert the record for pg_depend.
    DO $$
    BEGIN
      IF NOT EXISTS (
        SELECT FROM pg_catalog.pg_depend
          WHERE refclassid = 1255 AND refobjid = 8085
      ) THEN
        INSERT INTO pg_catalog.pg_depend (
          classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
        ) VALUES
          (0, 0, 0, 1255, 8085, 0, 'p');
      END IF;
    END $$;
COMMIT;

BEGIN;
    SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

    INSERT INTO pg_catalog.pg_proc (
      oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
      prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
      pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
      proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
    ) VALUES
      (8086, 'yb_backend_heap_snapshot_peak', 11, 10, 12, 1, 100000, 0, '-', 'f', false, false, true, true,
      'v', 'r', 0, 0, 2249, '', '{20,20,20,20,20,25}', '{o,o,o,o,o,o}',
      '{estimated_bytes,estimated_count,avg_bytes_per_allocation,sampled_bytes,sample_count,call_stack}',
      NULL, NULL, 'yb_backend_heap_snapshot_peak', NULL, NULL, NULL)
    ON CONFLICT DO NOTHING;

    -- Insert the record for pg_depend.
    DO $$
    BEGIN
      IF NOT EXISTS (
        SELECT FROM pg_catalog.pg_depend
          WHERE refclassid = 1255 AND refobjid = 8086
      ) THEN
        INSERT INTO pg_catalog.pg_depend (
          classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
        ) VALUES
          (0, 0, 0, 1255, 8086, 0, 'p');
      END IF;
    END $$;
COMMIT;

BEGIN;
    SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

    INSERT INTO pg_catalog.pg_proc (
      oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
      prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
      pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
      proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
    ) VALUES
      (8087, 'yb_log_backend_heap_snapshot', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
      'v', 'r', 1, 0, 16, '23', '{23}', '{i}', '{pid}', NULL, NULL,
      'yb_log_backend_heap_snapshot', NULL, NULL, NULL) ON CONFLICT DO NOTHING;

    -- Insert the record for pg_depend.
    DO $$
    BEGIN
      IF NOT EXISTS (
        SELECT FROM pg_catalog.pg_depend
          WHERE refclassid = 1255 AND refobjid = 8087
      ) THEN
        INSERT INTO pg_catalog.pg_depend (
          classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
        ) VALUES
          (0, 0, 0, 1255, 8087, 0, 'p');
      END IF;
    END $$;
COMMIT;

BEGIN;
    SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

    INSERT INTO pg_catalog.pg_proc (
      oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
      prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
      pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
      proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
    ) VALUES
      (8088, 'yb_log_backend_heap_snapshot_peak', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
      'v', 'r', 1, 0, 16, '23', '{23}', '{i}', '{pid}', NULL, NULL,
      'yb_log_backend_heap_snapshot_peak', NULL, NULL, NULL) ON CONFLICT DO NOTHING;

    -- Insert the record for pg_depend.
    DO $$
    BEGIN
      IF NOT EXISTS (
        SELECT FROM pg_catalog.pg_depend
          WHERE refclassid = 1255 AND refobjid = 8088
      ) THEN
        INSERT INTO pg_catalog.pg_depend (
          classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
        ) VALUES
          (0, 0, 0, 1255, 8088, 0, 'p');
      END IF;
    END $$;
COMMIT;
