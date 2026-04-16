BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang,
    procost, prorows, provariadic, prosupport, prokind,
    prosecdef, proleakproof, proisstrict, proretset, provolatile,
    proparallel, pronargs, pronargdefaults, prorettype, proargtypes,
    proallargtypes, proargmodes,
    proargnames,
    proargdefaults, protrftypes,
    prosrc, probin, prosqlbody, proconfig, proacl
  ) VALUES
    -- implementation of yb_get_tablet_metadata
    (8113, 'yb_stat_auto_analyze', 11, 10, 12,
     1, 0, 0, '-', 'f',
     false, false, true, true, 'v',
     'u', 0, 0, 2249, '',
     '{26,25,25,20,3802}', '{o,o,o,o,o}',
     '{relid,schemaname,relname,mutations,last_analyze_info}',
     NULL, NULL,
     'yb_stat_auto_analyze', NULL, NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES (
    8113, 1255, 0, 'query YB auto analyze service table'
  ) ON CONFLICT DO NOTHING;
COMMIT;
