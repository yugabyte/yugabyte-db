BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, prosupport,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
    pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
    proargdefaults, protrftypes, prosrc, probin, prosqlbody, proconfig, proacl
  ) VALUES
    (8097, 'yb_pg_stat_get_backend_pss_mem_bytes', 11, 10, 12, 1, 0, 0, '-',
    'f', false, false, true, false, 's', 'r', 1, 
    0, 20, '23', NULL, NULL, NULL,
    NULL, NULL, 'yb_pg_stat_get_backend_pss_mem_bytes', NULL, NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;
  
  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES (
    8097, 1255, 0, 'statistics: PSS memory usage of backend retrieved from OS'
  ) ON CONFLICT DO NOTHING;
COMMIT;

DO
$rename$
BEGIN
    IF NOT EXISTS (
      SELECT TRUE FROM pg_attribute
      WHERE attrelid = 'pg_catalog.pg_stat_activity'::regclass
            AND attname = 'pss_mem_bytes'
            AND NOT attisdropped
    ) THEN
      ALTER VIEW pg_catalog.pg_stat_activity 
      RENAME COLUMN rss_mem_bytes TO pss_mem_bytes;
      -- although we're recreating the view, we still need to rename the column first
      -- because PG forbids dropping a column from a view, even with CREATE OR REPLACE.
      -- (see the commit message for more details)

      CREATE OR REPLACE VIEW pg_catalog.pg_stat_activity 
      WITH (use_initdb_acl = true) 
      AS
          SELECT
              S.datid AS datid,
              D.datname AS datname,
              S.pid,
              S.leader_pid,
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
              s.backend_xmin,
              S.query_id,
              S.query,
              S.backend_type,
              yb_pg_stat_get_backend_catalog_version(B.beid) AS catalog_version,
              yb_pg_stat_get_backend_allocated_mem_bytes(B.beid) AS allocated_mem_bytes,
              yb_pg_stat_get_backend_pss_mem_bytes(B.beid) AS pss_mem_bytes,
              S.yb_backend_xid
      FROM pg_stat_get_activity(NULL) AS S
          LEFT JOIN pg_database AS D ON (S.datid = D.oid)
          LEFT JOIN pg_authid AS U ON (S.usesysid = U.oid)
          LEFT JOIN (pg_stat_get_backend_idset() beid CROSS JOIN
                    pg_stat_get_backend_pid(beid) pid) B ON B.pid = S.pid;
    END IF;
END;
$rename$;
