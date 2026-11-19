/*
 * YugabyteDB System Views
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 * src/backend/catalog/yb_system_views.sql
 *
 */

CREATE VIEW yb_terminated_queries AS
SELECT
    D.datname AS databasename,
    S.backend_pid AS backend_pid,
    S.query_id AS query_id,
    S.query_text AS query_text,
    S.termination_reason AS termination_reason,
    S.query_start AS query_start_time,
    S.query_end AS query_end_time
FROM yb_pg_stat_get_queries(NULL) AS S
LEFT JOIN pg_database AS D ON (S.db_oid = D.oid);

CREATE VIEW yb_active_session_history AS
    SELECT *
    FROM yb_active_session_history();

CREATE VIEW yb_local_tablets AS
    SELECT *
    FROM yb_local_tablets();

CREATE VIEW yb_wait_event_desc AS
    SELECT *
    FROM yb_wait_event_desc();

CREATE VIEW yb_query_diagnostics_status AS
    SELECT *
    FROM yb_get_query_diagnostics_status();

CREATE VIEW yb_servers_metrics AS
    SELECT *
    FROM yb_servers_metrics();

CREATE VIEW yb_tablet_metadata AS
    SELECT
        t.tablet_id,
        -- Stable PG table oid (pg_class.oid) reported by the master; unlike the
        -- relfilenode, it survives table rewrites. NULL for non-YSQL tables
        -- (e.g. the 'transactions' table) and colocation parents.
        t.oid,
        t.namespace    AS db_name,
        t.object_name  AS relname,
        t.start_hash_code,
        t.end_hash_code,
        t.leader,
        t.replicas,
        t.start_range,
        t.end_range,
        t.tablet_attrs,
        t.tablet_state
    FROM
        yb_get_tablet_metadata() t
    WHERE
        -- Condition 1: Include the system 'transactions' table.
        (t.namespace = 'system' AND t.object_name = 'transactions')
    OR
        -- Condition 2: Include YSQL tables.
        (t.type = 'YSQL');

CREATE VIEW yb_pg_stat_plans AS
    SELECT *
    FROM yb_pg_stat_plans_get_all_entries() AS stat_plans(dbid oid, userid oid, queryid BIGINT, 
													planid bigint, first_used TIMESTAMPTZ, 
	                                     			last_used TIMESTAMPTZ, hints text, calls bigint, 
													 avg_exec_time double precision, 
                                        			 max_exec_time double precision, max_exec_time_params text, 
													 avg_est_cost double precision, plan text); 

CREATE VIEW yb_pg_stat_plans_insights AS 
	WITH cte AS (SELECT dbid, userid, queryid, planid, first_used, last_used, hints, avg_exec_time, avg_est_cost, 
	             min(avg_exec_time) OVER (PARTITION BY dbid, userid, queryid) min_avg_exec_time, 
				 min(avg_est_cost) OVER (PARTITION BY dbid, userid, queryid) min_avg_est_cost FROM yb_pg_stat_plans) 
	SELECT dbid, userid, queryid, planid, first_used, last_used, hints, avg_exec_time, avg_est_cost, 
	       min_avg_exec_time, min_avg_est_cost, CASE WHEN (avg_exec_time = min_avg_exec_time AND 
		   min_avg_est_cost != avg_est_cost) OR (avg_exec_time != min_avg_exec_time AND 
		   min_avg_est_cost = avg_est_cost) THEN 'Yes' ELSE 'No' END AS plan_require_evaluation, 
		   CASE WHEN avg_exec_time = min_avg_exec_time THEN 'Yes' ELSE 'No' END AS plan_min_exec_time 
		FROM cte ORDER BY queryid, planid, last_used;

CREATE VIEW yb_online_schema_change AS
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
        m.origin,
        m.cutover_mode,
        m.query
    FROM pg_catalog.pg_yb_online_schema_change m,
         -- The master reports an active job's copy. The function is strict, so a
         -- NULL id skips the call for a job that has ended.
         LATERAL pg_catalog.yb_online_schema_change_progress(
             CASE WHEN m.status = 'active' THEN m.job_id END) p
    -- Active jobs first: a failed copy keeps its trigger on the source until it is aborted.
    ORDER BY m.status <> 'active', m.created_at DESC, m.job_id DESC;

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
COMMENT ON COLUMN yb_online_schema_change.origin IS 'Who started the job: manual (the SQL lifecycle functions) or alter_table (ALTER TABLE ... CONCURRENTLY).';
COMMENT ON COLUMN yb_online_schema_change.cutover_mode IS 'What finish does: rename (the target takes the source''s name) or swap (the source keeps its OID and takes the target''s storage).';
COMMENT ON COLUMN yb_online_schema_change.query IS 'Text of the statement that started the job: the yb_begin_mirror_table call or the ALTER TABLE ... CONCURRENTLY statement, which a swap re-runs on the source. NULL if unknown.';

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
COMMENT ON COLUMN pg_catalog.pg_yb_online_schema_change.query IS 'Text of the statement that started the job: the yb_begin_mirror_table call or the ALTER TABLE ... CONCURRENTLY statement, which a swap re-runs on the source. NULL if unknown.';
