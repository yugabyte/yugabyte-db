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
    (8115, 'yb_pg_validate_conf_file', 11, 10, 12,
     1, 0, 0, '-', 'f',
     false, false, false, false, 'v',
     'u', 3, 0, 2249, '25 25 25',
     '{25,25,25,25,25,25}', '{i,i,i,o,o,o}',
     '{hba_path,guc_path,ident_path,hba_error,guc_error,ident_error}',
     NULL, NULL,
     'yb_pg_validate_conf_file', NULL, NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES (
    8115, 1255, 0,
    'validate PostgreSQL configuration files without applying settings'
  ) ON CONFLICT DO NOTHING;
COMMIT;
