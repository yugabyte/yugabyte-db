BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  -- Add a column for state in yb_local_tablets
  -- TODO: As a workaround for GHI #13500, we perform a delete + insert instead
  -- of an update into pg_proc. Restore to UPDATE once fixed.
  DELETE FROM pg_catalog.pg_proc WHERE proname = 'yb_local_tablets' AND
    pronamespace = 'pg_catalog'::regnamespace;
  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, prosupport,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel,
    pronargs, pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes,
    proargnames, proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
  ) VALUES (
    8066, 'yb_local_tablets', 11, 10, 12, 1, 100, 0, '-', 'f', false,
    false, true, true, 'v', 'r', 0, 0, 2249, '', '{25,25,25,25,25,25,17,17,25}',
    '{o,o,o,o,o,o,o,o,o}', '{tablet_id,table_id,table_type,namespace_name,ysql_schema_name,
    table_name,partition_key_start,partition_key_end,state}',
    NULL, NULL, 'yb_local_tablets', NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;
COMMIT;

-- Recreating the system view yb_local_tablets
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT TRUE FROM pg_attribute
    WHERE attrelid = 'pg_catalog.yb_local_tablets'::regclass
          AND attname = 'state'
          AND NOT attisdropped
  ) THEN
    CREATE OR REPLACE VIEW pg_catalog.yb_local_tablets
    WITH (use_initdb_acl = true)
    AS
      SELECT *
      FROM yb_local_tablets();
  END IF;
END $$;
