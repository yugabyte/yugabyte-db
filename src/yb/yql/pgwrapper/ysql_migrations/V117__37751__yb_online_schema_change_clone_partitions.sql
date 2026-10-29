BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  -- prosecdef is false: the function only issues DDL, which should run as, and
  -- leave the clones owned by, the caller.
  -- Values must stay byte-identical to what a fresh cluster gets from
  -- pg_proc.dat + yb_system_functions.sql
  -- (TestYsqlUpgrade#migratingIsEquivalentToReinitdb); the :location values in
  -- proargdefaults are the DEFAULT expressions' offsets in that CREATE FUNCTION.
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
    (8140, 'yb_online_schema_change_clone_partitions', 11, 10, 12,
     1, 0, 0, '-', 'f',
     false, false, true, false, 'v',
     'u', 4, 2, 2278, '2205 2205 25 16',
     NULL, NULL,
     '{source_table,target_table,suffix,clone_local_indexes}',
     '({CONST :consttype 25 :consttypmod -1 :constcollid 100 :constlen -1 :constbyval false :constisnull false :location 180 :constvalue 8 [ 32 0 0 0 95 110 101 119 ]} {CONST :consttype 16 :consttypmod -1 :constcollid 0 :constlen 1 :constbyval true :constisnull false :location 250 :constvalue 1 [ 0 0 0 0 0 0 0 0 ]})', NULL,
     'yb_online_schema_change_clone_partitions', NULL, NULL, NULL,
     NULL)
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES (
    8140, 1255, 0, 'recreate the source table''s partitions under an empty partitioned target'
  ) ON CONFLICT DO NOTHING;
COMMIT;

-- Must match the definition in yb_system_views.sql
-- (TestYsqlUpgrade#migratingIsEquivalentToReinitdb).
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT TRUE FROM pg_attribute
    WHERE attrelid = 'pg_catalog.yb_online_schema_change'::regclass
      AND attname = 'source_leaf_count'
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
            m.cutover_mode,
            -- 0 once the table has been dropped.
            (SELECT count(*)::int FROM pg_catalog.pg_partition_tree(m.source_table_oid)
             WHERE isleaf)                      AS source_leaf_count,
            (SELECT count(*)::int FROM pg_catalog.pg_partition_tree(m.target_table_oid)
             WHERE isleaf)                      AS target_leaf_count
        FROM pg_catalog.pg_yb_online_schema_change m,
             -- The master reports an active job's copy. The function is strict, so a
             -- NULL id skips the call for a job that has ended.
             LATERAL pg_catalog.yb_online_schema_change_progress(
                 CASE WHEN m.status = 'active' THEN m.job_id END) p
        -- Active jobs first: a failed copy keeps its trigger on the source until it is aborted.
        ORDER BY m.status <> 'active', m.created_at DESC, m.job_id DESC;
  END IF;
END $$;

BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  COMMENT ON COLUMN yb_online_schema_change.source_leaf_count IS 'Data-bearing partitions of the source (1 for an ordinary table).';
  COMMENT ON COLUMN yb_online_schema_change.target_leaf_count IS 'Data-bearing partitions of the target (1 for an ordinary table).';
COMMIT;
