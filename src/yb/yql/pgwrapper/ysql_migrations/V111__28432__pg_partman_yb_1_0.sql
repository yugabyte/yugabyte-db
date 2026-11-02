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
    (8122, 'yb_pg_partman_check_native_txn_ddl_and_locks', 11, 10, 12,
     1, 0, 0, '-', 'f',
     false, false, true, false, 'v',
     's', 0, 0, 16, '',
     NULL, NULL,
     NULL,
     NULL, NULL,
     'yb_pg_partman_check_native_txn_ddl_and_locks', NULL, NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES (
    8122, 1255, 0, 'whether pg_partman uses transactional DDL and table level locks'
  ) ON CONFLICT DO NOTHING;
COMMIT;

-- Update pg_partman to version 4.7.4-yb-1.0 if it is installed at 4.7.4
DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM pg_extension WHERE extname = 'pg_partman' AND extversion = '4.7.4') THEN
        ALTER EXTENSION pg_partman UPDATE TO '4.7.4-yb-1.0';
    END IF;
END $$;
