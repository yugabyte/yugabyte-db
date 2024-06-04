BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  -- Add a column for restart commit ht in pg_get_replication_slots
  -- TODO: As a workaround for GHI #13500, we perform a delete + insert instead
  -- of an update into pg_proc. Restore to UPDATE once fixed.
  DELETE FROM pg_catalog.pg_proc WHERE proname = 'pg_get_replication_slots' AND
    pronamespace = 'pg_catalog'::regnamespace;
  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel,
    pronargs, pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes,
    proargnames, proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
  ) VALUES (
    3781, 'pg_get_replication_slots', 11, 10, 12, 1, 10, 0, '-', 'f', false, false, false,
    true, 's', 's', 0, 0, 2249, '', '{19,19,25,26,16,16,23,28,28,3220,3220,25,20}',
    '{o,o,o,o,o,o,o,o,o,o,o,o,o}', '{slot_name,plugin,slot_type,datoid,temporary,active,
    active_pid,xmin,catalog_xmin,restart_lsn,confirmed_flush_lsn,yb_stream_id,yb_restart_commit_ht}',
    NULL, NULL, 'pg_get_replication_slots', NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;
COMMIT;

-- Recreating system views that use pg_get_replication_slots to update their corresponding
-- pg_rewrite entries.
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT TRUE FROM pg_attribute
    WHERE attrelid = 'pg_catalog.pg_replication_slots'::regclass
          AND attname = 'yb_restart_commit_ht'
          AND NOT attisdropped
  ) THEN
    CREATE OR REPLACE VIEW pg_catalog.pg_replication_slots
    WITH (use_initdb_acl = true)
    AS
        SELECT
            L.slot_name,
            L.plugin,
            L.slot_type,
            L.datoid,
            D.datname AS database,
            L.temporary,
            L.active,
            L.active_pid,
            L.xmin,
            L.catalog_xmin,
            L.restart_lsn,
            L.confirmed_flush_lsn,
            L.yb_stream_id,
            L.yb_restart_commit_ht
        FROM pg_get_replication_slots() AS L
            LEFT JOIN pg_database D ON (L.datoid = D.oid);
  END IF;
END $$;
