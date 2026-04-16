BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  -- Add a column for ysql_dbid in yb_active_session_history
  -- TODO: As a workaround for GHI #13500, we perform a delete + insert instead
  -- of an update into pg_proc. Restore to UPDATE once fixed.
  DELETE FROM pg_catalog.pg_proc WHERE proname = 'yb_active_session_history' AND
    pronamespace = 'pg_catalog'::regnamespace;
  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel,
    pronargs, pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes,
    proargnames, proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
  ) VALUES (
    8065, 'yb_active_session_history', 11, 10, 12, 1, 100000, 0, '-', 'f', false,
    false, true, true, 'v', 'r', 0, 0, 2249, '', '{1184,2950,20,25,25,25,2950,20,23,25,25,700,25,26}',
    '{o,o,o,o,o,o,o,o,o,o,o,o,o,o}', '{sample_time,root_request_id,rpc_request_id,wait_event_component,
    wait_event_class,wait_event,top_level_node_id,query_id,pid,client_node_ip,wait_event_aux,
    sample_weight,wait_event_type,ysql_dbid}',
    NULL, NULL, 'yb_active_session_history', NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;
COMMIT;

-- Altering column name in the system view yb_active_session_history
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT TRUE FROM pg_attribute
    WHERE attrelid = 'pg_catalog.yb_active_session_history'::regclass
          AND attname = 'pid'
          AND NOT attisdropped
  ) THEN
    DROP VIEW pg_catalog.yb_active_session_history;
    CREATE OR REPLACE VIEW pg_catalog.yb_active_session_history
    WITH (use_initdb_acl = true)
    AS
      SELECT *
      FROM yb_active_session_history();
  END IF;
END $$;
