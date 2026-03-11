/*
 * YugabyteDB System Functions
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
 * src/backend/catalog/yb_system_functions.sql
 *
 */

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
  yb_is_database_colocated(check_legacy boolean DEFAULT false)
RETURNS boolean
LANGUAGE INTERNAL
STRICT STABLE PARALLEL SAFE
AS 'yb_is_database_colocated';

CREATE OR REPLACE FUNCTION
  yb_query_diagnostics(query_id int8, diagnostics_interval_sec int8 DEFAULT 300,
                       explain_sample_rate int8 DEFAULT 1, explain_analyze bool DEFAULT false,
                       explain_dist bool DEFAULT false, explain_debug bool DEFAULT false,
                       bind_var_query_min_duration_ms int8 DEFAULT 10)
RETURNS text
LANGUAGE INTERNAL
VOLATILE STRICT PARALLEL SAFE
AS 'yb_query_diagnostics';

CREATE OR REPLACE FUNCTION
  yb_cancel_query_diagnostics(query_id int8)
RETURNS void
LANGUAGE INTERNAL
VOLATILE STRICT PARALLEL SAFE
AS 'yb_cancel_query_diagnostics';

CREATE OR REPLACE FUNCTION
  yb_index_check(indexrelid oid, single_snapshot_mode bool DEFAULT false)
RETURNS void
LANGUAGE INTERNAL
VOLATILE PARALLEL SAFE
AS 'yb_index_check';

CREATE OR REPLACE FUNCTION
  yb_compute_row_ybctid(relid oid, key_atts record, ybidxbasectid bytea DEFAULT NULL)
RETURNS bytea
LANGUAGE INTERNAL
IMMUTABLE PARALLEL SAFE
AS 'yb_compute_row_ybctid';

CREATE OR REPLACE FUNCTION
  yb_active_session_history(start_time TIMESTAMPTZ DEFAULT NULL,
                            end_time TIMESTAMPTZ DEFAULT NULL,
                            OUT sample_time TIMESTAMPTZ,
                            OUT root_request_id UUID,
                            OUT rpc_request_id INT8,
                            OUT wait_event_component TEXT,
                            OUT wait_event_class TEXT,
                            OUT wait_event TEXT,
                            OUT top_level_node_id UUID,
                            OUT query_id INT8,
                            OUT pid INT4,
                            OUT client_node_ip TEXT,
                            OUT wait_event_aux TEXT,
                            OUT sample_weight FLOAT4,
                            OUT wait_event_type TEXT,
                            OUT ysql_dbid OID,
                            OUT wait_event_code INT8,
                            OUT pss_mem_bytes INT8,
                            OUT ysql_userid OID)
RETURNS SETOF RECORD
LANGUAGE INTERNAL
VOLATILE ROWS 100000 PARALLEL RESTRICTED
AS 'yb_active_session_history';

--
-- Grant and revoke statements on YB objects.
--
REVOKE EXECUTE ON FUNCTION yb_increment_all_db_catalog_versions(boolean) FROM public;
GRANT EXECUTE ON FUNCTION yb_increment_all_db_catalog_versions(boolean) TO yb_db_admin;
REVOKE EXECUTE ON FUNCTION yb_fix_catalog_version_table(boolean) FROM public;
REVOKE EXECUTE ON FUNCTION yb_query_diagnostics(int8,int8,int8,boolean,boolean,boolean,int8) FROM public;
GRANT EXECUTE ON FUNCTION yb_query_diagnostics(int8,int8,int8,boolean,boolean,boolean,int8) TO yb_db_admin;
REVOKE EXECUTE ON FUNCTION yb_cancel_query_diagnostics(int8) FROM public;
GRANT EXECUTE ON FUNCTION yb_cancel_query_diagnostics(int8) TO yb_db_admin;
REVOKE EXECUTE ON FUNCTION yb_increment_db_catalog_version_with_inval_messages(oid,boolean,bytea,int4)
  FROM public;
GRANT EXECUTE ON FUNCTION yb_increment_db_catalog_version_with_inval_messages(oid,boolean,bytea,int4)
  TO yb_db_admin;
REVOKE EXECUTE ON FUNCTION yb_increment_all_db_catalog_versions_with_inval_messages(oid,boolean,bytea,int4)
  FROM public;
GRANT EXECUTE ON FUNCTION yb_increment_all_db_catalog_versions_with_inval_messages(oid,boolean,bytea,int4)
  TO yb_db_admin;
