BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  -- EXECUTE is revoked from public because CREATE TRIGGER is the only place it
  -- is checked, and a firing writes to whatever table the trigger arguments
  -- name.
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
    (8128, 'yb_online_schema_change_trigger', 11, 10, 12,
     1, 0, 0, '-', 'f',
     false, false, false, false, 'v',
     'u', 0, 0, 2279, '',
     NULL, NULL,
     NULL,
     NULL, NULL,
     'yb_online_schema_change_trigger', NULL, NULL, NULL,
     '{postgres=X/postgres}')
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES (
    8128, 1255, 0,
    'trigger function for online online schema change'
  ) ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_init_privs (
    objoid, classoid, objsubid, privtype, initprivs
  ) VALUES (
    8128, 1255, 0, 'i', '{postgres=X/postgres}'
  ) ON CONFLICT DO NOTHING;
COMMIT;
