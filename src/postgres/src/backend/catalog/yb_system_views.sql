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

-- YB_TODO(Arpan) Need to use this new definition for pg_locks without breaking initdb
--
-- CREATE VIEW pg_locks AS
-- SELECT l.locktype,
--        l.database,
--        l.relation,
--        null::int                  AS page,
--        null::smallint             AS tuple,
--        null::text                 AS virtualxid,
--        null::xid                  AS transactionid,
--        null::oid                  AS classid,
--        null::oid                  AS objid,
--        null::smallint             AS objsubid,
--        null::text                 AS virtualtransaction,
--        l.pid,
--        array_to_string(mode, ',') AS mode,
--        l.granted,
--        l.fastpath,
--        l.waitstart,
--        l.waitend,
--        jsonb_build_object('node', l.node,
--                           'transactionid', l.transaction_id,
--                           'subtransaction_id', l.subtransaction_id,
--                           'is_explicit', l.is_explicit,
--                           'tablet_id', l.tablet_id,
--                           'blocked_by', l.blocked_by,
--                           'keyrangedetails', jsonb_build_object(
--                                   'cols', to_jsonb(l.hash_cols || l.range_cols),
--                                   'attnum', l.attnum,
--                                   'column_id', l.column_id,
--                                   'multiple_rows_locked', l.multiple_rows_locked
--                               )
--            )                      AS ybdetails
-- FROM yb_lock_status(null, null) AS l;

CREATE OR REPLACE FUNCTION
  yb_is_database_colocated(check_legacy boolean DEFAULT false)
RETURNS boolean
LANGUAGE INTERNAL
STRICT STABLE PARALLEL SAFE
AS 'yb_is_database_colocated';

--
-- Grant and revoke statements on YB objects.
--
REVOKE EXECUTE ON FUNCTION yb_increment_all_db_catalog_versions(boolean) FROM public;
GRANT EXECUTE ON FUNCTION yb_increment_all_db_catalog_versions(boolean) TO yb_db_admin;
REVOKE EXECUTE ON FUNCTION yb_fix_catalog_version_table(boolean) FROM public;
