BEGIN;
    SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

    INSERT INTO pg_catalog.pg_proc (
      oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, prosupport,
      prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
      pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
      proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
    ) VALUES
      (8085, 'yb_backend_heap_snapshot', 11, 10, 12, 1, 100000, 0, 0, 'f', false, false, true, true,
      'v', 'r', 0, 0, 2249, '', '{20,20,20,20,20,25}', '{o,o,o,o,o,o}',
      '{estimated_bytes,estimated_count,avg_bytes_per_allocation,sampled_bytes,sample_count,call_stack}',
      NULL, NULL, 'yb_backend_heap_snapshot', NULL, NULL, NULL)
    ON CONFLICT DO NOTHING;

    -- Insert the record for pg_description.
    DO $$
    BEGIN
      IF NOT EXISTS (
        SELECT FROM pg_catalog.pg_description
          WHERE objoid = 8085 AND classoid = 1255 AND objsubid = 0
      ) THEN
        INSERT INTO pg_catalog.pg_description (
          objoid, classoid, objsubid, description
        ) VALUES
          (8085, 1255, 0, 'Gets the current TCMalloc heap snapshot for the current PG backend process');
      END IF;
    END $$;
COMMIT;

BEGIN;
    SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

    INSERT INTO pg_catalog.pg_proc (
      oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, prosupport,
      prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
      pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
      proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
    ) VALUES
      (8086, 'yb_backend_heap_snapshot_peak', 11, 10, 12, 1, 100000, 0, 0, 'f', false, false, true, true,
      'v', 'r', 0, 0, 2249, '', '{20,20,20,20,20,25}', '{o,o,o,o,o,o}',
      '{estimated_bytes,estimated_count,avg_bytes_per_allocation,sampled_bytes,sample_count,call_stack}',
      NULL, NULL, 'yb_backend_heap_snapshot_peak', NULL, NULL, NULL)
    ON CONFLICT DO NOTHING;

    -- Insert the record for pg_description.
    DO $$
    BEGIN
      IF NOT EXISTS (
        SELECT FROM pg_catalog.pg_description
          WHERE objoid = 8086 AND classoid = 1255 AND objsubid = 0
      ) THEN
        INSERT INTO pg_catalog.pg_description (
          objoid, classoid, objsubid, description
        ) VALUES
          (8086, 1255, 0, 'Gets the peak TCMalloc heap snapshot for the current PG backend process');
      END IF;
    END $$;
COMMIT;

BEGIN;
    SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

    INSERT INTO pg_catalog.pg_proc (
      oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, prosupport,
      prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
      pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
      proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
    ) VALUES
      (8087, 'yb_log_backend_heap_snapshot', 11, 10, 12, 1, 0, 0, 0, 'f', false, false, true, false,
      'v', 'r', 1, 0, 16, '23', '{23}', '{i}', '{pid}', NULL, NULL,
      'yb_log_backend_heap_snapshot', NULL, NULL, NULL) ON CONFLICT DO NOTHING;

    -- Insert the record for pg_description.
    DO $$
    BEGIN
      IF NOT EXISTS (
        SELECT FROM pg_catalog.pg_description
          WHERE objoid = 8087 AND classoid = 1255 AND objsubid = 0
      ) THEN
        INSERT INTO pg_catalog.pg_description (
          objoid, classoid, objsubid, description
        ) VALUES
          (8087, 1255, 0, 'Logs the current TCMalloc heap snapshot for the specified PG backend process');
      END IF;
    END $$;
COMMIT;

BEGIN;
    SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

    INSERT INTO pg_catalog.pg_proc (
      oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, prosupport,
      prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
      pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
      proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
    ) VALUES
      (8088, 'yb_log_backend_heap_snapshot_peak', 11, 10, 12, 1, 0, 0, 0, 'f', false, false, true, false,
      'v', 'r', 1, 0, 16, '23', '{23}', '{i}', '{pid}', NULL, NULL,
      'yb_log_backend_heap_snapshot_peak', NULL, NULL, NULL) ON CONFLICT DO NOTHING;

    -- Insert the record for pg_description.
    DO $$
    BEGIN
      IF NOT EXISTS (
        SELECT FROM pg_catalog.pg_description
          WHERE objoid = 8088 AND classoid = 1255 AND objsubid = 0
      ) THEN
        INSERT INTO pg_catalog.pg_description (
          objoid, classoid, objsubid, description
        ) VALUES
          (8088, 1255, 0, 'Logs the peak TCMalloc heap snapshot for the specified PG backend process');
      END IF;
    END $$;
COMMIT;
