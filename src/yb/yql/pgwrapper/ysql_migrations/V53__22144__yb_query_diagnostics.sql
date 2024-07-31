BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel,
    pronargs, pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes,
    proargnames, proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
  ) VALUES
    (8068, 'yb_query_diagnostics', 11, 10, 12, 1, 0, 0, '-', 'f', false,
    false, true, false, 'v', 's', 7, 6, 25, '20 20 20 16 16 16 20',
    NULL, NULL, '{query_id,diagnostics_interval_sec,explain_sample_rate,
    explain_analyze,explain_dist,explain_debug,bind_var_query_min_duration_ms}',
    '({FUNCEXPR :funcid 481 :funcresulttype 20 :funcretset false :funcvariadic false :funcformat 2 :funccollid 0 :inputcollid 0 :args ({CONST :consttype 23 :consttypmod -1 :constcollid 0 :constlen 4 :constbyval true :constisnull false :location 103 :constvalue 4 [ 44 1 0 0 0 0 0 0 ]}) :location -1} {FUNCEXPR :funcid 481 :funcresulttype 20 :funcretset false :funcvariadic false :funcformat 2 :funccollid 0 :inputcollid 0 :args ({CONST :consttype 23 :consttypmod -1 :constcollid 0 :constlen 4 :constbyval true :constisnull false :location 164 :constvalue 4 [ 1 0 0 0 0 0 0 0 ]}) :location -1} {CONST :consttype 16 :consttypmod -1 :constcollid 0 :constlen 1 :constbyval true :constisnull false :location 196 :constvalue 1 [ 0 0 0 0 0 0 0 0 ]} {CONST :consttype 16 :consttypmod -1 :constcollid 0 :constlen 1 :constbyval true :constisnull false :location 252 :constvalue 1 [ 0 0 0 0 0 0 0 0 ]} {CONST :consttype 16 :consttypmod -1 :constcollid 0 :constlen 1 :constbyval true :constisnull false :location 286 :constvalue 1 [ 0 0 0 0 0 0 0 0 ]} {FUNCEXPR :funcid 481 :funcresulttype 20 :funcretset false :funcvariadic false :funcformat 2 :funccollid 0 :inputcollid 0 :args ({CONST :consttype 23 :consttypmod -1 :constcollid 0 :constlen 4 :constbyval true :constisnull false :location 360 :constvalue 4 [ 10 0 0 0 0 0 0 0 ]}) :location -1})',
    NULL, 'yb_query_diagnostics', NULL, NULL, '{postgres=X/postgres,yb_db_admin=X/postgres}')
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_init_privs (
    objoid, classoid, objsubid, privtype, initprivs
  ) VALUES
    (8068, 1255, 0, 'i', '{postgres=X/postgres,yb_db_admin=X/postgres}')
  ON CONFLICT DO NOTHING;

  -- Create dependency records for everything we (possibly) created.
  -- Since pg_depend has no OID or unique constraint, using PL/pgSQL instead.
  DO $$
  BEGIN
    IF NOT EXISTS (
      SELECT FROM pg_catalog.pg_depend
        WHERE refclassid = 1255 AND refobjid = 8068
    ) THEN
      INSERT INTO pg_catalog.pg_depend (
        classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
      ) VALUES
        (0, 0, 0, 1255, 8068, 0, 'p');
    END IF;
  END $$;
COMMIT;
