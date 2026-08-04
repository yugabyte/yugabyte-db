BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel,
    pronargs, pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes,
    proargnames, proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
  ) VALUES (
    8066, 'yb_local_tablets', 11, 10, 12, 1, 100, 0, '-', 'f', false,
    false, true, true, 'v', 'r', 0, 0, 2249, '', '{25,25,25,25,25,25,17,17}',
    '{o,o,o,o,o,o,o,o}', '{tablet_id,table_id,table_type,namespace_name,ysql_schema_name,
    table_name,partition_key_start,partition_key_end}',
    NULL, NULL, 'yb_local_tablets', NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  -- Create dependency records for everything we (possibly) created.
  -- Since pg_depend has no OID or unique constraint, using PL/pgSQL instead.
  DO $$
  BEGIN
    IF NOT EXISTS (
      SELECT FROM pg_catalog.pg_depend
        WHERE refclassid = 1255 AND refobjid = 8066
    ) THEN
      INSERT INTO pg_catalog.pg_depend (
        classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
      ) VALUES
        (0, 0, 0, 1255, 8066, 0, 'p');
    END IF;
  END $$;
COMMIT;

-- Creating the system view yb_local_tablets
CREATE OR REPLACE VIEW pg_catalog.yb_local_tablets WITH (use_initdb_acl = true) AS
  SELECT *
  FROM yb_local_tablets();
