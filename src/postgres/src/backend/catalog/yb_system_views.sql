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
