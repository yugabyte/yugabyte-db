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
    (8114, 'yb_get_tablet_for_key', 11, 10, 12,
     1, 0, 0, '-', 'f',
     false, false, true, false, 's',
     's', 3, 0, 25, '25 26 2249',
     NULL, NULL,
     '{db_name,table_oid,row_values}',
     NULL, NULL,
     'yb_get_tablet_for_key', NULL, NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES (
    8114, 1255, 0, 'Returns the tablet id for the given row key'
  ) ON CONFLICT DO NOTHING;
COMMIT;
