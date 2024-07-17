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

CREATE OR REPLACE FUNCTION
  yb_is_database_colocated(check_legacy boolean DEFAULT false)
RETURNS boolean
LANGUAGE INTERNAL
STRICT STABLE PARALLEL SAFE
AS 'yb_is_database_colocated';

CREATE OR REPLACE FUNCTION
  yb_reset_analyze_statistics(table_oid oid)
RETURNS void AS
$$
    UPDATE pg_class c SET reltuples = -1
    WHERE
        relkind IN ('r', 'p', 'm', 'i')
        AND reltuples >= 0
        AND NOT EXISTS (
            SELECT 0 FROM pg_namespace ns
            WHERE ns.oid = relnamespace
                  AND nspname IN ('pg_toast', 'pg_toast_temp')
        )
        AND (table_oid IS NULL
             OR c.oid = table_oid
             OR c.oid IN (SELECT indexrelid FROM pg_index WHERE indrelid = table_oid))
        AND ((SELECT rolsuper FROM pg_roles r WHERE rolname = session_user)
             OR (NOT relisshared
                 AND (relowner = session_user::regrole
                      OR ((SELECT datdba
                           FROM pg_database d
                           WHERE datname = current_database())
                          = session_user::regrole)
                     )
                 )
        );
    DELETE
    FROM pg_statistic
    WHERE
        EXISTS (
            SELECT 0 FROM pg_class c
            WHERE c.oid = starelid
                  AND (table_oid IS NULL OR c.oid = table_oid)
                  AND ((SELECT rolsuper FROM pg_roles r
                        WHERE rolname = session_user)
                       OR (NOT relisshared
                           AND (relowner = session_user::regrole
                                OR ((SELECT datdba
                                     FROM pg_database d
                                     WHERE datname = current_database())
                                    = session_user::regrole)
                               )
                          )
                      )
        );
    DELETE FROM pg_statistic_ext_data
    WHERE
        EXISTS (
            SELECT 0 FROM pg_class c
            JOIN pg_statistic_ext e ON c.oid = e.stxrelid
            WHERE e.oid = stxoid
                  AND (table_oid IS NULL OR c.oid = table_oid)
                  AND ((SELECT rolsuper FROM pg_roles r
                        WHERE rolname = session_user)
                       OR (NOT relisshared
                           AND (relowner = session_user::regrole
                                OR ((SELECT datdba
                                     FROM pg_database d
                                     WHERE datname = current_database())
                                    = session_user::regrole)
                               )
                          )
                      )
        );
    UPDATE pg_yb_catalog_version SET current_version = current_version + 1
    WHERE db_oid = (SELECT oid FROM pg_database
                    WHERE datname = current_database());
$$
LANGUAGE SQL
CALLED ON NULL INPUT
VOLATILE
SECURITY DEFINER
SET yb_non_ddl_txn_for_sys_tables_allowed = ON;

CREATE OR REPLACE FUNCTION
  yb_query_diagnostics(query_id int8, diagnostics_interval_sec int8 DEFAULT 300,
                       explain_sample_rate int8 DEFAULT 1, explain_analyze bool DEFAULT false,
                       explain_dist bool DEFAULT false, explain_debug bool DEFAULT false,
                       bind_var_query_min_duration_ms int8 DEFAULT 10)
RETURNS text
LANGUAGE INTERNAL
VOLATILE STRICT PARALLEL SAFE
AS 'yb_query_diagnostics';

--
-- Grant and revoke statements on YB objects.
--
REVOKE EXECUTE ON FUNCTION yb_increment_all_db_catalog_versions(boolean) FROM public;
GRANT EXECUTE ON FUNCTION yb_increment_all_db_catalog_versions(boolean) TO yb_db_admin;
REVOKE EXECUTE ON FUNCTION yb_fix_catalog_version_table(boolean) FROM public;
REVOKE EXECUTE ON FUNCTION yb_query_diagnostics(int8,int8,int8,boolean,boolean,boolean,int8) FROM public;
GRANT EXECUTE ON FUNCTION yb_query_diagnostics(int8,int8,int8,boolean,boolean,boolean,int8) TO yb_db_admin;
