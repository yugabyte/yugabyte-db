SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

CREATE OR REPLACE VIEW pg_catalog.yb_terminated_queries WITH (use_initdb_acl = true) AS 
SELECT 
    D.datname AS databasename,
    S.backend_pid AS backend_pid,
    S.query_text AS query_text,
    S.termination_reason AS termination_reason,
    S.query_start AS query_start_time,
    S.query_end AS query_end_time 
FROM yb_pg_stat_get_queries(NULL) AS S 
LEFT JOIN pg_database AS D ON (S.db_oid = D.oid);
