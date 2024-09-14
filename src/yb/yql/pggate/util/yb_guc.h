// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License.  You may obtain a copy
// of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
// License for the specific language governing permissions and limitations under
// the License.
//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {

struct varlena;

#endif

/*
 * Guc variable to log the protobuf string for every outgoing (DocDB) read/write request.
 * See the "YB Debug utils" section in pg_yb_utils.h (as well as guc.c) for more information.
 */
extern bool yb_debug_log_docdb_requests;

/*
 * Toggles whether formatting functions exporting system catalog information
 * include DocDB metadata (such as tablet split information).
 */
extern bool yb_format_funcs_include_yb_metadata;

/*
 * Guc variable to enable the use of regular transactions for operating on system catalog tables
 * in case a DDL transaction has not been started.
 */
extern bool yb_non_ddl_txn_for_sys_tables_allowed;

/*
 * Toggles whether to force use of global transaction status table.
 */
extern bool yb_force_global_transaction;

/*
 * Guc that toggles whether strict inequalities are pushed down.
 */
extern bool yb_pushdown_strict_inequality;

/*
 * Guc that toggles whether IS NOT NULL is pushed down.
 */
extern bool yb_pushdown_is_not_null;

/*
 * Guc that toggles the pg_locks view on/off.
 */
extern bool yb_enable_pg_locks;

/*
 * Guc variable to suppress non-Postgres logs from appearing in Postgres log file.
 */
extern bool suppress_nonpg_logs;

/*
 * Guc variable to control the max session batch size before flushing.
 */
extern int ysql_session_max_batch_size;

/*
 * Guc variable to control the max number of in-flight operations from YSQL to tablet server.
 */
extern int ysql_max_in_flight_ops;

/*
 * Guc variable to enable binary restore from a binary backup of YSQL tables. When doing binary
 * restore, we copy the docdb SST files of those tables from the source database and reuse them
 * for a newly created target database to restore those tables.
 */
extern bool yb_binary_restore;

/*
 * Guc variable for ignoring requests to set pg_class oids when yb_binary_restore is set.
 *
 * If true then calls to pg_catalog.binary_upgrade_set_next_{heap|index}_pg_class_oid will have no
 * effect.
 */
extern bool yb_ignore_pg_class_oids;

/*
 * Set to true only for runs with EXPLAIN ANALYZE
 */
extern bool yb_run_with_explain_analyze;

/*
 * GUC variable that enables batching RPCs of generated for IN queries
 * on hash keys issued to the same tablets.
 */
extern bool yb_enable_hash_batch_in;

/*
 * GUC variable that enables using the default value for existing rows after
 * an ADD COLUMN ... DEFAULT operation.
 */
extern bool yb_enable_add_column_missing_default;

/*
 * Guc variable that enables replication commands.
 */
extern bool yb_enable_replication_commands;

/*
 * Guc variable that enables replication slot consumption.
 */
extern bool yb_enable_replication_slot_consumption;

/*
 * GUC variable that enables ALTER TABLE rewrite operations.
 */
extern bool yb_enable_alter_table_rewrite;

/*
 * Guc variable that enables replica identity command in Alter Table Query
 */
extern bool yb_enable_replica_identity;

/*
 * xcluster consistency level
 */
#define XCLUSTER_CONSISTENCY_TABLET 0
#define XCLUSTER_CONSISTENCY_DATABASE 1

/*
 * Enables atomic and ordered reads of data in xCluster replicated databases. This may add a delay
 * to the visibility of all data in the database.
 */
extern int yb_xcluster_consistency_level;

extern uint64_t yb_read_time;
extern bool yb_is_read_time_ht;
/*
 * Allows for customizing the number of rows to be prefetched.
 */
extern int yb_fetch_row_limit;
extern int yb_fetch_size_limit;

/*
 * GUC flag: Minimum transaction age, in ms, to report when using yb_lock_status().
 */
extern int yb_locks_min_txn_age;

/*
 * GUC flag: Maximum number of transactions to return results for in yb_lock_status().
 */
extern int yb_locks_max_transactions;

/*
 * GUC flag: Maximum number of locks to return per transaction per tablet in yb_lock_status().
 */
extern int yb_locks_txn_locks_per_tablet;

/*
 * GUC flag: Time in milliseconds for which Walsender waits before fetching the next batch of
 * changes from the CDC service in case the last received response was non-empty.
 */
extern int yb_walsender_poll_sleep_duration_nonempty_ms;

/*
 * GUC flag: Specifies the maximum number of changes kept in memory per transaction in reorder
 * buffer, which is used in streaming changes via logical replication. After that changes are
 * spooled to disk.
 */
extern int yb_reorderbuffer_max_changes_in_memory;

/*
 * GUC flag:  Time in milliseconds for which Walsender waits before fetching the next batch of
 * changes from the CDC service in case the last received response was empty. The response can be
 * empty in case there are no DMLs happening in the system.
 */
extern int yb_walsender_poll_sleep_duration_empty_ms;

extern int yb_read_after_commit_visibility;

#ifdef __cplusplus
} // extern "C"
#endif
