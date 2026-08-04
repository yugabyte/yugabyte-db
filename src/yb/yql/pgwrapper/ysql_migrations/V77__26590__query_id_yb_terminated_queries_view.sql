-- Update yb_pg_stat_get_queries to include new column (query_id)
BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  DELETE FROM pg_catalog.pg_proc WHERE proname = 'yb_pg_stat_get_queries' AND 
    pronamespace = 'pg_catalog'::regnamespace;

  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, prosupport,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
    pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
    proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
  ) VALUES
    (8041, 'yb_pg_stat_get_queries', 11, 10, 12, 1, 100, 0, '-', 'f', false, false, false, true,
    's', 'r', 1, 0, 2249, '26', '{26,26,23,20,25,25,1184,1184}', '{i,o,o,o,o,o,o,o}',
    '{db_oid,db_oid,backend_pid,query_id,query_text,termination_reason,query_start,query_end}',
    NULL, NULL, 'yb_pg_stat_get_queries', NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

COMMIT;

-- Update yb_terminated_queries view to include new column (query_id)
BEGIN;
  DROP VIEW pg_catalog.yb_terminated_queries;
  CREATE OR REPLACE VIEW pg_catalog.yb_terminated_queries
    WITH (use_initdb_acl = true)
  AS
  SELECT
    D.datname AS databasename,
    S.backend_pid AS backend_pid,
    S.query_id AS query_id,      --Included from yb_pg_stat_get_queries
    S.query_text AS query_text,
    S.termination_reason AS termination_reason,
    S.query_start AS query_start_time,
    S.query_end AS query_end_time
  FROM
    yb_pg_stat_get_queries(NULL) AS S
  LEFT JOIN
    pg_database AS D
  ON
    (S.db_oid = D.oid);
COMMIT;
