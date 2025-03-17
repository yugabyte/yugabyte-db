BEGIN;
    SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

    INSERT INTO pg_catalog.pg_proc (
      oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, prosupport,
      prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
      pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
      proargdefaults, protrftypes, prosrc, probin, prosqlbody, proconfig, proacl
    ) VALUES
      (8091, 'pg_restore_relation_stats', 11, 10, 12, 1, 0, 2276, '-', 'f', false, false, false,
       false, 'v', 'u', 1, 0, 16, '2276', NULL, '{v}', '{kwargs}', NULL, NULL,
       'pg_restore_relation_stats', NULL, NULL, NULL, NULL)
    ON CONFLICT DO NOTHING;

    -- Insert the record for pg_description.
    DO $$
    BEGIN
      IF NOT EXISTS (
        SELECT FROM pg_catalog.pg_description
          WHERE objoid = 8091 AND classoid = 1255 AND objsubid = 0
      ) THEN
        INSERT INTO pg_catalog.pg_description (
          objoid, classoid, objsubid, description
        ) VALUES
          (8091, 1255, 0, 'restore statistics on relation');
      END IF;
    END $$;
COMMIT;

BEGIN;
    SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

    INSERT INTO pg_catalog.pg_proc (
      oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, prosupport,
      prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
      pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
      proargdefaults, protrftypes, prosrc, probin, prosqlbody, proconfig, proacl
    ) VALUES
      (8092, 'pg_clear_relation_stats', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, false, false,
      'v', 'u', 1, 0, 2278, '2205', NULL, NULL, '{relation}', NULL, NULL, 'pg_clear_relation_stats',
      NULL, NULL, NULL, NULL)
    ON CONFLICT DO NOTHING;

    -- Insert the record for pg_description.
    DO $$
    BEGIN
      IF NOT EXISTS (
        SELECT FROM pg_catalog.pg_description
          WHERE objoid = 8092 AND classoid = 1255 AND objsubid = 0
      ) THEN
        INSERT INTO pg_catalog.pg_description (
          objoid, classoid, objsubid, description
        ) VALUES
          (8092, 1255, 0, 'clear statistics on relation');
      END IF;
    END $$;
COMMIT;

BEGIN;
    SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

    INSERT INTO pg_catalog.pg_proc (
      oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, prosupport,
      prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
      pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
      proargdefaults, protrftypes, prosrc, probin, prosqlbody, proconfig, proacl
    ) VALUES
      (8093, 'pg_restore_attribute_stats', 11, 10, 12, 1, 0, 2276, '-', 'f', false, false, false,
       false, 'v', 'u', 1, 0, 16, '2276', NULL, '{v}', '{kwargs}', NULL, NULL,
       'pg_restore_attribute_stats', NULL, NULL, NULL, NULL)
    ON CONFLICT DO NOTHING;

    -- Insert the record for pg_description.
    DO $$
    BEGIN
      IF NOT EXISTS (
        SELECT FROM pg_catalog.pg_description
          WHERE objoid = 8093 AND classoid = 1255 AND objsubid = 0
      ) THEN
        INSERT INTO pg_catalog.pg_description (
          objoid, classoid, objsubid, description
        ) VALUES
          (8093, 1255, 0, 'restore statistics on attribute');
      END IF;
    END $$;
COMMIT;

BEGIN;
    SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

    INSERT INTO pg_catalog.pg_proc (
      oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, prosupport,
      prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
      pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
      proargdefaults, protrftypes, prosrc, probin, prosqlbody, proconfig, proacl
    ) VALUES
      (8094, 'pg_clear_attribute_stats', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, false, false,
       'v', 'u', 3, 0, 2278, '2205 19 16', NULL, NULL, '{relation,attname,inherited}', NULL, NULL,
       'pg_clear_attribute_stats', NULL, NULL, NULL, NULL)
    ON CONFLICT DO NOTHING;

    -- Insert the record for pg_description.
    DO $$
    BEGIN
      IF NOT EXISTS (
        SELECT FROM pg_catalog.pg_description
          WHERE objoid = 8094 AND classoid = 1255 AND objsubid = 0
      ) THEN
        INSERT INTO pg_catalog.pg_description (
          objoid, classoid, objsubid, description
        ) VALUES
          (8094, 1255, 0, 'clear statistics on attribute');
      END IF;
    END $$;
COMMIT;
