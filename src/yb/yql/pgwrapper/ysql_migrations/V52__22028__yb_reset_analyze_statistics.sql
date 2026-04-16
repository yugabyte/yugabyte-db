BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel,
    pronargs, pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes,
    proargnames, proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
  ) VALUES (
    8067, 'yb_reset_analyze_statistics', 11, 10, 14, 100, 0, 0, '-', 'f', true,
    false, false, false, 'v', 'u', 1, 0, 2278, '26', NULL, NULL, '{table_oid}',
    NULL, NULL,
$$
    UPDATE pg_class c SET reltuples = -1
    WHERE
        relkind IN ('r', 'p', 'm', 'i')
        AND reltuples >= 0
        AND NOT EXISTS (
            SELECT 0 FROM pg_namespace ns
            WHERE ns.oid = relnamespace
                  AND nspname IN ('pg_toast', 'pg_toast_temp')
        )
        AND (table_oid IS NULL
             OR c.oid = table_oid
             OR c.oid IN (SELECT indexrelid FROM pg_index WHERE indrelid = table_oid))
        AND ((SELECT rolsuper FROM pg_roles r WHERE rolname = session_user)
             OR (NOT relisshared
                 AND (relowner = session_user::regrole
                      OR ((SELECT datdba
                           FROM pg_database d
                           WHERE datname = current_database())
                          = session_user::regrole)
                     )
                 )
        );
    DELETE
    FROM pg_statistic
    WHERE
        EXISTS (
            SELECT 0 FROM pg_class c
            WHERE c.oid = starelid
                  AND (table_oid IS NULL OR c.oid = table_oid)
                  AND ((SELECT rolsuper FROM pg_roles r
                        WHERE rolname = session_user)
                       OR (NOT relisshared
                           AND (relowner = session_user::regrole
                                OR ((SELECT datdba
                                     FROM pg_database d
                                     WHERE datname = current_database())
                                    = session_user::regrole)
                               )
                          )
                      )
        );
    DELETE FROM pg_statistic_ext_data
    WHERE
        EXISTS (
            SELECT 0 FROM pg_class c
            JOIN pg_statistic_ext e ON c.oid = e.stxrelid
            WHERE e.oid = stxoid
                  AND (table_oid IS NULL OR c.oid = table_oid)
                  AND ((SELECT rolsuper FROM pg_roles r
                        WHERE rolname = session_user)
                       OR (NOT relisshared
                           AND (relowner = session_user::regrole
                                OR ((SELECT datdba
                                     FROM pg_database d
                                     WHERE datname = current_database())
                                    = session_user::regrole)
                               )
                          )
                      )
        );
    UPDATE pg_yb_catalog_version SET current_version = current_version + 1
    WHERE db_oid = (SELECT oid FROM pg_database
                    WHERE datname = current_database());
$$,
    NULL, '{yb_non_ddl_txn_for_sys_tables_allowed=on}', NULL)
  ON CONFLICT DO NOTHING;

  -- Create dependency records for everything we (possibly) created.
  -- Since pg_depend has no OID or unique constraint, using PL/pgSQL instead.
  DO $$
  BEGIN
    IF NOT EXISTS (
      SELECT FROM pg_catalog.pg_depend
        WHERE refclassid = 1255 AND refobjid = 8067
    ) THEN
      INSERT INTO pg_catalog.pg_depend (
        classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
      ) VALUES
        (0, 0, 0, 1255, 8067, 0, 'p');
    END IF;
  END $$;

COMMIT;
