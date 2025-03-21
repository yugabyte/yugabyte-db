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
