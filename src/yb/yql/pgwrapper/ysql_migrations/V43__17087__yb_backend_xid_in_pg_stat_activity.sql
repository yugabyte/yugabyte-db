BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  -- Add a column for distributed transaction ID in pg_stat_get_activity
  -- TODO: As a workaround for GHI #13500, we perform a delete + insert instead
  -- of an update into pg_proc. Restore to UPDATE once fixed.
  DELETE FROM pg_catalog.pg_proc WHERE proname = 'pg_stat_get_activity' AND
    proargtypes = '23' AND pronamespace = 'pg_catalog'::regnamespace;
  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel,
    pronargs, pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes,
    proargnames, proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
  ) VALUES (
    2022, 'pg_stat_get_activity', 11, 10, 12, 1, 100, 0, '-', 'f', false, false, false, 
    true, 's', 'r', 1, 0, 2249, '23', '{23,26,23,26,25,25,25,25,25,1184,1184,1184,1184,869,25,23,
    28,28,25,16,25,25,23,16,25,2950}', '{i,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o}',
    '{pid,datid,pid,usesysid,application_name,state,query,wait_event_type,wait_event,xact_start,
    query_start,backend_start,state_change,client_addr,client_hostname,client_port,backend_xid,
    backend_xmin,backend_type,ssl,sslversion,sslcipher,sslbits,sslcompression,sslclientdn,
    yb_backend_xid}', NULL, NULL, 'pg_stat_get_activity', NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;
COMMIT;

BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  -- Add catalog entries for the procedure yb_get_current_transaction()
  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
    pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
    proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
  ) VALUES
    (8064, 'yb_get_current_transaction', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true,
    false, 'v', 's', 0, 0, 2950, '', NULL, NULL, NULL, NULL, NULL,
    'yb_get_current_transaction', NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  -- Create dependency records for everything we (possibly) created.
  -- Since pg_depend has no OID or unique constraint, using PL/pgSQL instead.
  DO $$
  BEGIN
    IF NOT EXISTS (
      SELECT FROM pg_catalog.pg_depend
        WHERE refclassid = 1255 AND refobjid = 8064
    ) THEN
      INSERT INTO pg_catalog.pg_depend (
        classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
      ) VALUES
        (0, 0, 0, 1255, 8064, 0, 'p');
    END IF;
  END $$;

COMMIT;

-- Recreating system views that use pg_stat_get_activity to update their corresponding
-- pg_rewrite entries.
CREATE OR REPLACE VIEW pg_catalog.pg_stat_activity WITH (use_initdb_acl = true) AS
    SELECT
        S.datid AS datid,
        D.datname AS datname,
        S.pid,
        S.usesysid,
        U.rolname AS usename,
        S.application_name,
        S.client_addr,
        S.client_hostname,
        S.client_port,
        S.backend_start,
        S.xact_start,
        S.query_start,
        S.state_change,
        S.wait_event_type,
        S.wait_event,
        S.state,
        S.backend_xid,
        S.backend_xmin,
        S.query,
        S.backend_type,
        yb_pg_stat_get_backend_catalog_version(B.beid) AS catalog_version,
        yb_pg_stat_get_backend_allocated_mem_bytes(B.beid) AS allocated_mem_bytes,
        yb_pg_stat_get_backend_rss_mem_bytes(B.beid) AS rss_mem_bytes,
        S.yb_backend_xid
    FROM pg_stat_get_activity(NULL) AS S
        LEFT JOIN pg_database AS D ON (S.datid = D.oid)
        LEFT JOIN pg_authid AS U ON (S.usesysid = U.oid)
        LEFT JOIN (pg_stat_get_backend_idset() beid CROSS JOIN
                   pg_stat_get_backend_pid(beid) pid) B ON B.pid = S.pid;

CREATE OR REPLACE VIEW pg_catalog.pg_stat_replication WITH (use_initdb_acl = true) AS
    SELECT
        S.pid,
        S.usesysid,
        U.rolname AS usename,
        S.application_name,
        S.client_addr,
        S.client_hostname,
        S.client_port,
        S.backend_start,
        S.backend_xmin,
        W.state,
        W.sent_lsn,
        W.write_lsn,
        W.flush_lsn,
        W.replay_lsn,
        W.write_lag,
        W.flush_lag,
        W.replay_lag,
        W.sync_priority,
        W.sync_state
    FROM pg_stat_get_activity(NULL) AS S
        JOIN pg_stat_get_wal_senders() AS W ON (S.pid = W.pid)
        LEFT JOIN pg_authid AS U ON (S.usesysid = U.oid);

CREATE OR REPLACE VIEW pg_catalog.pg_stat_ssl WITH (use_initdb_acl = true) AS
    SELECT
        S.pid,
        S.ssl,
        S.sslversion AS version,
        S.sslcipher AS cipher,
        S.sslbits AS bits,
        S.sslcompression AS compression,
        S.sslclientdn AS clientdn
    FROM pg_stat_get_activity(NULL) AS S;
