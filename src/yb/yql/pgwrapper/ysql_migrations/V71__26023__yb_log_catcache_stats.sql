BEGIN;
    SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

    INSERT INTO pg_catalog.pg_proc (
      oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, prosupport,
      prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
      pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
      proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
    ) VALUES
      (8089, 'yb_log_catcache_stats', 11, 10, 12, 1, 0, 0, 0, 'f', false, false, true, false,
      'v', 'r', 1, 0, 2278, '23', '{23}', '{i}', '{pid}',
      NULL, NULL, 'yb_log_catcache_stats', NULL, NULL, NULL)
    ON CONFLICT DO NOTHING;

    -- Insert the record for pg_description.  
    DO $$
    BEGIN
      IF NOT EXISTS (
        SELECT FROM pg_catalog.pg_description
          WHERE objoid = 8089 AND classoid = 1255 AND objsubid = 0
      ) THEN
        INSERT INTO pg_catalog.pg_description (
          objoid, classoid, objsubid, description
        ) VALUES
          (8089, 1255, 0, 'log the catcache stats of a given process');
      END IF;
    END $$;
COMMIT; 
