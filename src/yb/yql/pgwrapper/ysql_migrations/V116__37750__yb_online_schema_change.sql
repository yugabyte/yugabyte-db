BEGIN;
  -- Column order must match pg_yb_online_schema_change.h, so that attnums match those
  -- of a fresh initdb.
  CREATE TABLE IF NOT EXISTS pg_catalog.pg_yb_online_schema_change (
    job_id           int8        NOT NULL,
    source_table_oid oid         NOT NULL,
    target_table_oid oid         NOT NULL,
    transform_fn_oid oid         NOT NULL,
    created_at       timestamptz NOT NULL,
    updated_at       timestamptz NOT NULL,
    status           text        COLLATE "C" NOT NULL,
    origin           text        COLLATE "C" NOT NULL,
    cutover_mode     text        COLLATE "C" NOT NULL,
    query            text        COLLATE "C",
    CONSTRAINT pg_yb_online_schema_change_job_id_index PRIMARY KEY (job_id ASC)
      WITH (table_oid = 8129)
  ) WITH (
    oids = false,
    table_oid = 8124,
    row_type_oid = 8125
  );

  -- Must match yb_system_views.sql: query can quote literals.
  REVOKE ALL ON pg_catalog.pg_yb_online_schema_change FROM public;
COMMIT;

-- Record the REVOKE above as initial privileges so pg_dump omits it.
BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;
  UPDATE pg_catalog.pg_init_privs ip
    SET initprivs = c.relacl
    FROM pg_catalog.pg_class c
    WHERE c.relnamespace = 'pg_catalog'::regnamespace
      AND c.relname = 'pg_yb_online_schema_change'
      AND ip.objoid = c.oid
      AND ip.classoid = 'pg_catalog.pg_class'::regclass
      AND ip.objsubid = 0
      AND ip.privtype = 'i';
COMMIT;

BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  -- Must match what a fresh cluster gets from pg_proc.dat
  -- (TestYsqlUpgrade#migratingIsEquivalentToReinitdb).
  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang,
    procost, prorows, provariadic, prosupport, prokind,
    prosecdef, proleakproof, proisstrict, proretset, provolatile,
    proparallel, pronargs, pronargdefaults, prorettype, proargtypes,
    proallargtypes, proargmodes,
    proargnames,
    proargdefaults, protrftypes,
    prosrc, probin, prosqlbody, proconfig, proacl
  ) VALUES
    (8126, 'yb_begin_mirror_table', 11, 10, 12,
     1, 0, 0, '-', 'f',
     false, false, true, false, 'v',
     'u', 3, 0, 20, '2205 2205 24',
     NULL, NULL,
     '{source_table,target_table,transform_fn}',
     NULL, NULL,
     'yb_begin_mirror_table', NULL, NULL, NULL,
     NULL),
    (8142, 'yb_wait_mirror_table', 11, 10, 12,
     1, 0, 0, '-', 'f',
     false, false, true, false, 'v',
     'u', 1, 0, 2278, '20',
     NULL, NULL,
     '{job_id}',
     NULL, NULL,
     'yb_wait_mirror_table', NULL, NULL, NULL,
     NULL),
    (8127, 'yb_finish_mirror_table', 11, 10, 12,
     1, 0, 0, '-', 'f',
     false, false, true, false, 'v',
     'u', 1, 0, 2278, '20',
     NULL, NULL,
     '{job_id}',
     NULL, NULL,
     'yb_finish_mirror_table', NULL, NULL, NULL,
     NULL),
    (8136, 'yb_abort_mirror_table', 11, 10, 12,
     1, 0, 0, '-', 'f',
     false, false, true, false, 'v',
     'u', 1, 0, 2278, '20',
     NULL, NULL,
     '{job_id}',
     NULL, NULL,
     'yb_abort_mirror_table', NULL, NULL, NULL,
     NULL),
    (8134, 'yb_online_schema_change_progress', 11, 10, 12,
     1, 0, 0, '-', 'f',
     false, false, true, false, 'v',
     'u', 1, 0, 2249, '20',
     '{20,25,20,23,23,25}', '{i,o,o,o,o,o}',
     '{job_id,status,rows_done,tablets_done,tablets_total,error_message}',
     NULL, NULL,
     'yb_online_schema_change_progress', NULL, NULL, NULL,
     NULL)
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES
    (8126, 1255, 0, 'start an online schema change'),
    (8142, 1255, 0, 'wait until an online schema change is ready to finish'),
    (8127, 1255, 0, 'finish an online schema change by swapping the tables'),
    (8136, 1255, 0, 'abort an online schema change'),
    (8134, 1255, 0, 'state and progress of an online schema change job''s copy, from the master')
  ON CONFLICT DO NOTHING;
COMMIT;

-- Must match yb_system_views.sql.
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT TRUE FROM pg_views
    WHERE viewname = 'yb_online_schema_change'
  ) THEN
    CREATE OR REPLACE VIEW pg_catalog.yb_online_schema_change
    WITH (use_initdb_acl = true) AS
        SELECT
            m.job_id,
            m.source_table_oid::regclass        AS source_table,
            m.target_table_oid::regclass        AS target_table,
            m.transform_fn_oid::regprocedure    AS transform_function,
            COALESCE(p.status, m.status)        AS status,
            p.rows_done,
            NULLIF(p.tablets_total, 0)          AS tablets_total,
            p.tablets_done,
            CASE WHEN p.tablets_total > 0
                 THEN round((100.0 * p.tablets_done / p.tablets_total)::numeric, 1)
            END                                 AS progress_pct,
            p.error_message,
            m.created_at,
            m.updated_at,
            CASE m.origin WHEN 'manual' THEN 'yb_begin_mirror_table'
                          WHEN 'alter_table' THEN 'ALTER TABLE CONCURRENTLY'
            END                                 AS command,
            m.cutover_mode
        FROM pg_catalog.pg_yb_online_schema_change m,
             -- The master reports an active job's copy. The function is strict, so a
             -- NULL id skips the call for a job that has ended.
             LATERAL pg_catalog.yb_online_schema_change_progress(
                 CASE WHEN m.status = 'active' THEN m.job_id END) p
        -- Active jobs first: a failed copy keeps its trigger on the source until it is aborted.
        ORDER BY m.status <> 'active', m.created_at DESC, m.job_id DESC;
  END IF;
END $$;

-- Must match yb_system_views.sql.  COMMENT ON overwrites, so this is
-- idempotent.
BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  COMMENT ON VIEW yb_online_schema_change IS
      'Online schema change jobs, with table and function names, and the progress of the copy of each active job. Active jobs are listed first, then finished and aborted ones, newest first within each.';
  COMMENT ON COLUMN yb_online_schema_change.job_id IS 'Online schema change job identifier, which the lifecycle functions take.';
  COMMENT ON COLUMN yb_online_schema_change.source_table IS 'Table being changed (source).';
  COMMENT ON COLUMN yb_online_schema_change.target_table IS 'Table the source is copied into (target).';
  COMMENT ON COLUMN yb_online_schema_change.transform_function IS 'Function that turns a source row into a target row.';
  COMMENT ON COLUMN yb_online_schema_change.status IS 'For an active job, the state of its copy: backfilling, ready or failed (active if the master cannot be reached). Otherwise finished or aborted.';
  COMMENT ON COLUMN yb_online_schema_change.rows_done IS 'Rows copied into the target so far, while the job is active.';
  COMMENT ON COLUMN yb_online_schema_change.tablets_total IS 'Source tablets to copy, while the job is active (NULL until known).';
  COMMENT ON COLUMN yb_online_schema_change.tablets_done IS 'Source tablets fully copied, while the job is active.';
  COMMENT ON COLUMN yb_online_schema_change.progress_pct IS 'Copy completion percentage, by tablet.';
  COMMENT ON COLUMN yb_online_schema_change.error_message IS 'Why the copy failed, shown only to a role that may ALTER the source.';
  COMMENT ON COLUMN yb_online_schema_change.created_at IS 'When the job began.';
  COMMENT ON COLUMN yb_online_schema_change.updated_at IS 'When the job last changed status.';
  COMMENT ON COLUMN yb_online_schema_change.command IS 'What started the job: yb_begin_mirror_table or ALTER TABLE CONCURRENTLY.';
  COMMENT ON COLUMN yb_online_schema_change.cutover_mode IS 'What finish does: rename (the target takes the source''s name) or swap (the source keeps its OID and takes the target''s storage).';

  COMMENT ON TABLE pg_catalog.pg_yb_online_schema_change IS
      'Online schema change jobs, one row per job. The yb_online_schema_change view adds names and the progress of the copy.';
  COMMENT ON COLUMN pg_catalog.pg_yb_online_schema_change.job_id IS 'Online schema change job identifier.';
  COMMENT ON COLUMN pg_catalog.pg_yb_online_schema_change.source_table_oid IS 'OID of the table being changed (source).';
  COMMENT ON COLUMN pg_catalog.pg_yb_online_schema_change.target_table_oid IS 'OID of the table the source is copied into (target).';
  COMMENT ON COLUMN pg_catalog.pg_yb_online_schema_change.transform_fn_oid IS 'OID of the function that turns a source row into a target row.';
  COMMENT ON COLUMN pg_catalog.pg_yb_online_schema_change.created_at IS 'When the job began.';
  COMMENT ON COLUMN pg_catalog.pg_yb_online_schema_change.updated_at IS 'When the job last changed status.';
  COMMENT ON COLUMN pg_catalog.pg_yb_online_schema_change.status IS 'active, finished or aborted.';
  COMMENT ON COLUMN pg_catalog.pg_yb_online_schema_change.origin IS 'Who started the job: manual (the SQL lifecycle functions) or alter_table (ALTER TABLE ... CONCURRENTLY).';
  COMMENT ON COLUMN pg_catalog.pg_yb_online_schema_change.cutover_mode IS 'What finish does: rename (the target takes the source''s name) or swap (the source keeps its OID and takes the target''s storage).';
  COMMENT ON COLUMN pg_catalog.pg_yb_online_schema_change.query IS 'Text of the statement that started the job: the yb_begin_mirror_table call or the ALTER TABLE ... CONCURRENTLY statement, which a swap re-runs on the source. NULL if unknown. The catalog is not publicly readable, since the text can hold literals.';
COMMIT;
