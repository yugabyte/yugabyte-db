BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel,
    pronargs, pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes,
    proargnames, proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
  ) VALUES
    (8065, 'yb_active_session_history', 11, 10, 12, 1, 100000, 0, '-', 'f', false,
    false, true, true, 'v', 'r', 0, 0, 2249, '', '{1184,2950,20,25,25,25,2950,20,20,25,25,700}',
    '{o,o,o,o,o,o,o,o,o,o,o,o}', '{sample_time,root_request_id,rpc_request_id,
    wait_event_component,wait_event_class,wait_event,top_level_node_id,query_id,
    ysql_session_id,client_node_ip,wait_event_aux,sample_weight}',
    NULL, NULL, 'yb_active_session_history', NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  -- Create dependency records for everything we (possibly) created.
  -- Since pg_depend has no OID or unique constraint, using PL/pgSQL instead.
  DO $$
  BEGIN
    IF NOT EXISTS (
      SELECT FROM pg_catalog.pg_depend
        WHERE refclassid = 1255 AND refobjid = 8065
    ) THEN
      INSERT INTO pg_catalog.pg_depend (
        classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
      ) VALUES
        (0, 0, 0, 1255, 8065, 0, 'p');
    END IF;
  END $$;
COMMIT;

-- Creating the system view yb_active_session_history
CREATE OR REPLACE VIEW pg_catalog.yb_active_session_history WITH (use_initdb_acl = true) AS
  SELECT *
  FROM yb_active_session_history();
