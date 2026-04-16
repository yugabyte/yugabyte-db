DO
$addcol$
BEGIN
    IF NOT EXISTS (
      -- Checks for only one of the columns - allocated_mem_bytes
      SELECT TRUE FROM pg_attribute
      WHERE attrelid = 'pg_catalog.pg_stat_activity'::regclass
            AND attname = 'allocated_mem_bytes'
            AND NOT attisdropped
    ) THEN
      CREATE OR REPLACE VIEW pg_catalog.pg_stat_activity
      WITH (use_initdb_acl = true)
      AS
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
              yb_pg_stat_get_backend_rss_mem_bytes(B.beid) AS rss_mem_bytes
          FROM pg_stat_get_activity(NULL) AS S
              LEFT JOIN pg_database AS D ON (S.datid = D.oid)
              LEFT JOIN pg_authid AS U ON (S.usesysid = U.oid)
              LEFT JOIN (pg_stat_get_backend_idset() beid CROSS JOIN
                         pg_stat_get_backend_pid(beid) pid) B ON B.pid = S.pid;
    END IF;
END;
$addcol$
