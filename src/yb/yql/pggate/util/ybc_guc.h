// Copyright (c) YugabyteDB, Inc.
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

#include <stdbool.h>  // Needed for bool in C.
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
 * Toggles whether to force use of tablespace-local locality instead of region locality.
 */
extern bool yb_force_tablespace_locality;

/*
 * Specify oid for tablespace to use for tablespace-local locality. Automatic selection is used
 * if 0 (default value).
 */
extern uint32_t yb_force_tablespace_locality_oid;

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
 * GUC that toggles whether pg_locks correctly formats and integrates advisory locks info.
 */
extern bool yb_pg_locks_integrate_advisory_locks;

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
 * Guc variable to ignore requests to set relfilenode ids when yb_binary_restore is set.
 *
 * If true then calls to pg_catalog.binary_upgrade_set_next_{heap|index}_relfilenode will have no
 * effect.
 */
extern bool yb_ignore_relfilenode_ids;

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
 * GUC variable that enables pg_export_snapshot and SET TRANSACTION SNAPSHOT.
 */
extern bool yb_enable_pg_export_snapshot;

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
 * Guc variable that allows lsn types to be specified while creating replication slot
 */
extern bool yb_allow_replication_slot_lsn_types;

/*
 * Guc variable that allows ordering mode to be specified while creating replication slot
 */
extern bool yb_allow_replication_slot_ordering_modes;

/*
 * GUC variable that specifies default replica identity for tables at the time of creation.
 */
extern char* yb_default_replica_identity;

/*
 * GUC variable that enable replication slot consumption of consistent changes from a hash range
 * of table.
 */
extern bool yb_enable_consistent_replication_from_hash_range;

/*
 * GUC variable that enables streaming tables without primary key to CDCSDK logical replication
 * streams.
 */
extern bool yb_cdcsdk_stream_tables_without_primary_key;

extern bool enable_object_locking_infra;

extern bool yb_enable_ddl_savepoint_infra;

/*
 * Refer YBCIsLegacyModeForCatalogOps() for details.
 */
extern bool yb_fallback_to_legacy_catalog_read_time;

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

/*
 * Allows user to query a databases as of the point in time.
 * yb_read_time can be expressed in the following 2 ways -
 *  - UNIX timestamp in microsecond (default unit)
 *  - as a uint64 representation of HybridTime with unit "ht"
 * Zero value means reading data as of current time.
 */
extern uint64_t yb_read_time;
extern bool yb_is_read_time_ht;
extern bool yb_disable_catalog_version_check;

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
 * GUC flag:  Time in milliseconds for which Walsender waits before fetching the next batch of
 * changes from the CDC service in case the last received response was empty. The response can be
 * empty in case there are no DMLs happening in the system.
 */
extern int yb_walsender_poll_sleep_duration_empty_ms;

/*
 * GUC flag: Specifies the maximum number of changes kept in memory per transaction in reorder
 * buffer, which is used in streaming changes via logical replication. After that changes are
 * spooled to disk.
 */
extern int yb_reorderbuffer_max_changes_in_memory;

/*
 * Allows for customizing the maximum size of a batch of explicit row lock operations.
 */
extern int yb_explicit_row_locking_batch_size;

/*
 * Ease transition to YSQL by reducing read restart errors for new apps.
 *
 * This option doesn't affect SERIALIZABLE isolation level since
 * SERIALIZABLE can't face read restart errors anyway. Also, does not affect
 * fast path writes.
 *
 * See the help text for yb_read_after_commit_visibility GUC for more
 * information.
 *
 * XXX: This GUC is meant as a workaround only by relaxing the
 * read-after-commit-visibility guarantee. Ideally, user should
 * (a) Fix their apps to handle read restart errors
 * (b) Or use accurate clocks provided by time_source=clockbound
 */
typedef enum {
  YB_STRICT_READ_AFTER_COMMIT_VISIBILITY = 0,
  YB_RELAXED_READ_AFTER_COMMIT_VISIBILITY = 1,
  YB_DEFERRED_READ_AFTER_COMMIT_VISIBILITY = 2,
} YbcReadAfterCommitVisibilityEnum;

/* GUC for the enum above. */
extern int yb_read_after_commit_visibility;

extern bool yb_allow_block_based_sampling_algorithm;

extern bool yb_allow_separate_requests_for_sampling_stages;

extern bool yb_refresh_matview_in_place;

extern bool yb_disable_auto_analyze;

extern int yb_major_version_upgrade_compatibility;

extern bool yb_upgrade_to_pg15_completed;

extern bool yb_debug_log_catcache_events;

extern bool yb_debug_log_snapshot_mgmt;

extern bool yb_debug_log_snapshot_mgmt_stack_trace;

extern bool yb_extension_upgrade;

extern bool yb_mixed_mode_expression_pushdown;

extern bool yb_mixed_mode_saop_pushdown;

extern bool yb_use_internal_auto_analyze_service_conn;

extern bool yb_ddl_transaction_block_enabled;

extern bool yb_disable_ddl_transaction_block_for_read_committed;

extern bool yb_allow_dockey_bounds;

extern bool yb_ignore_read_time_in_walsender;

extern bool yb_disable_pg_snapshot_mgmt_in_repeatable_read;

// Should be in sync with YsqlSamplingAlgorithm protobuf.
typedef enum {
  YB_SAMPLING_ALGORITHM_FULL_TABLE_SCAN = 0,
  YB_SAMPLING_ALGORITHM_BLOCK_BASED_SAMPLING = 1,
} YbcSamplingAlgorithmEnum;

extern int32_t yb_sampling_algorithm;

extern int yb_fk_references_cache_limit;

extern bool yb_xcluster_target_ddl_bypass;

#ifdef __cplusplus
} // extern "C"
#endif
