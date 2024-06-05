SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

CREATE OR REPLACE VIEW pg_catalog.pg_stat_progress_copy WITH (use_initdb_acl = true) AS
    SELECT
        S.pid AS pid, S.datid AS datid, D.datname AS datname,
        S.relid AS relid,
        CASE S.param5 WHEN 1 THEN 'COPY FROM'
                      WHEN 2 THEN 'COPY TO'
                      END AS command,
        CASE S.param6 WHEN 1 THEN 'FILE'
                      WHEN 2 THEN 'PROGRAM'
                      WHEN 3 THEN 'PIPE'
                      WHEN 4 THEN 'CALLBACK'
                      END AS "type",
        CASE S.param7 WHEN 0 THEN 'IN PROGRESS'
                      WHEN 1 THEN 'ERROR'
                      WHEN 2 THEN 'SUCCESS'
                      END AS yb_status,
        S.param1 AS bytes_processed,
        S.param2 AS bytes_total,
        S.param3 AS tuples_processed,
        S.param4 AS tuples_excluded
    FROM pg_stat_get_progress_info('COPY') AS S
        LEFT JOIN pg_database D ON S.datid = D.oid;

