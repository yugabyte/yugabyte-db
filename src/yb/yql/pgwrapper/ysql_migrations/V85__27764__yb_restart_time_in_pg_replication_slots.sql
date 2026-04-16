BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  -- Add a column for yb_restart_time in pg_get_replication_slots
  DELETE FROM pg_catalog.pg_proc WHERE proname = 'pg_get_replication_slots' AND
    pronamespace = 'pg_catalog'::regnamespace;
  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, prosupport,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel,
    pronargs, pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes,
    proargnames, proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
  ) VALUES (
    3781, 'pg_get_replication_slots', 11, 10, 12, 1, 10, 0, '-', 'f', false, false, false,
    true, 's', 's', 0, 0, 2249, '', '{19,19,25,26,16,16,23,28,28,3220,3220,25,20,16,25,20,25,1184}',
    '{o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o}', '{slot_name,plugin,slot_type,datoid,temporary,active,
    active_pid,xmin,catalog_xmin,restart_lsn,confirmed_flush_lsn,wal_status,safe_wal_size,
    two_phase,yb_stream_id,yb_restart_commit_ht,yb_lsn_type,yb_restart_time}',
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
          AND attname = 'yb_restart_time'
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
            L.wal_status,
            L.safe_wal_size,
            L.two_phase,
            L.yb_stream_id,
            L.yb_restart_commit_ht,
            L.yb_lsn_type,
            L.yb_restart_time
        FROM pg_get_replication_slots() AS L
            LEFT JOIN pg_database D ON (L.datoid = D.oid);
  END IF;
END $$;

DO $$
BEGIN
  CREATE OR REPLACE VIEW pg_catalog.pg_stat_replication_slots
    WITH (use_initdb_acl = true)
    AS
        SELECT
            s.slot_name,
            s.spill_txns,
            s.spill_count,
            s.spill_bytes,
            s.stream_txns,
            s.stream_count,
            s.stream_bytes,
            s.total_txns,
            s.total_bytes,
            s.stats_reset
        FROM pg_replication_slots as r,
            LATERAL pg_stat_get_replication_slot(slot_name) as s
        WHERE r.datoid IS NOT NULL; -- excluding physical slots
END $$;
