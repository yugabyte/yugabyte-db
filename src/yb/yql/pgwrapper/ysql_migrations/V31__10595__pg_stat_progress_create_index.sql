-- TODO: Use update instead of delete and insert after #13500 is fixed.
-- Also, use WHERE oid = 3318 after #11105 is fixed.
BEGIN;
    SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;
    DELETE FROM pg_catalog.pg_proc WHERE proname = 'pg_stat_get_progress_info' AND
        proargtypes = '25' AND pronamespace = 'pg_catalog'::regnamespace;
    INSERT INTO pg_catalog.pg_proc (
        oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
        prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel,
        pronargs, pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes,
        proargnames, proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
    ) VALUES
        (3318, 'pg_stat_get_progress_info', 11, 10, 12, 1, 100, 0, '-', 'f', false, false, true,
         true, 's', 'r', 1, 0, 2249, '25', '{25,23,26,26,20,20,20,20,20,20,20,20,20,20,20,20,
         20,20,20,20,20}', '{i,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o,o}',
         '{cmdtype,pid,datid,relid,param1,param2,param3,param4,param5,param6,param7,param8,
         param9,param10,param11,param12,param13,param14,param15,param16,param17}',
         NULL, NULL, 'pg_stat_get_progress_info', NULL, NULL, NULL)
    ON CONFLICT DO NOTHING;
COMMIT;

CREATE OR REPLACE VIEW pg_catalog.pg_stat_progress_create_index WITH (use_initdb_acl = true) AS
    SELECT
        S.pid AS pid, S.datid AS datid, D.datname AS datname,
        S.relid AS relid,
        CAST(S.param7 AS oid) AS index_relid,
        CASE S.param1 WHEN 1 THEN 'CREATE INDEX NONCONCURRENTLY'
                      WHEN 2 THEN 'CREATE INDEX CONCURRENTLY'
                      WHEN 3 THEN 'REINDEX NONCONCURRENTLY'
                      WHEN 4 THEN 'REINDEX CONCURRENTLY'
                      END AS command,
        CASE S.param10 WHEN 0 THEN 'initializing'
                       WHEN 1 THEN 'backfilling'
                       END AS phase,
        S.param4 AS lockers_total,
        S.param5 AS lockers_done,
        S.param6 AS current_locker_pid,
        S.param16 AS blocks_total,
        S.param17 AS blocks_done,
        S.param12 AS tuples_total,
        S.param13 AS tuples_done,
        S.param14 AS partitions_total,
        S.param15 AS partitions_done
    FROM pg_stat_get_progress_info('CREATE INDEX') AS S
        LEFT JOIN pg_database D ON S.datid = D.oid;

-- We also need to recreate existing views that use the pg_stat_get_progress_info function in order
-- to update their pg_rewrite entries.
CREATE OR REPLACE VIEW pg_catalog.pg_stat_progress_vacuum WITH (use_initdb_acl = true) AS
    SELECT
        S.pid AS pid, S.datid AS datid, D.datname AS datname,
        S.relid AS relid,
        CASE S.param1 WHEN 0 THEN 'initializing'
                      WHEN 1 THEN 'scanning heap'
                      WHEN 2 THEN 'vacuuming indexes'
                      WHEN 3 THEN 'vacuuming heap'
                      WHEN 4 THEN 'cleaning up indexes'
                      WHEN 5 THEN 'truncating heap'
                      WHEN 6 THEN 'performing final cleanup'
                      END AS phase,
        S.param2 AS heap_blks_total, S.param3 AS heap_blks_scanned,
        S.param4 AS heap_blks_vacuumed, S.param5 AS index_vacuum_count,
        S.param6 AS max_dead_tuples, S.param7 AS num_dead_tuples
    FROM pg_stat_get_progress_info('VACUUM') AS S
        LEFT JOIN pg_database D ON S.datid = D.oid;

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
