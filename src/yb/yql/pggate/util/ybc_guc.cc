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

#include "yb/yql/pggate/util/ybc_guc.h"

bool yb_debug_log_docdb_requests = false;

bool yb_enable_hash_batch_in = true;

bool yb_non_ddl_txn_for_sys_tables_allowed = false;

bool yb_format_funcs_include_yb_metadata = false;

bool yb_force_global_transaction = false;

bool yb_force_tablespace_locality = false;

uint32_t yb_force_tablespace_locality_oid = 0;

bool suppress_nonpg_logs = false;

bool yb_binary_restore = false;

bool yb_ignore_pg_class_oids = true;

bool yb_ignore_relfilenode_ids = true;

bool yb_pushdown_strict_inequality = true;

bool yb_pushdown_is_not_null = true;

bool yb_enable_pg_locks = true;

bool yb_pg_locks_integrate_advisory_locks = true;

bool yb_run_with_explain_analyze = false;

bool yb_enable_add_column_missing_default = true;

bool yb_enable_replication_commands = true;

bool yb_enable_pg_export_snapshot = true;

bool yb_enable_replication_slot_consumption = true;

bool yb_allow_replication_slot_lsn_types = true;

bool yb_allow_replication_slot_ordering_modes = true;

bool yb_enable_alter_table_rewrite = true;

bool yb_enable_replica_identity = true;

bool yb_enable_consistent_replication_from_hash_range = false;

bool yb_cdcsdk_stream_tables_without_primary_key = false;

// If this is set in the user's session to a positive value, it will supersede the gflag
// ysql_session_max_batch_size.
int ysql_session_max_batch_size = 0;

int ysql_max_in_flight_ops = 0;

int yb_xcluster_consistency_level = XCLUSTER_CONSISTENCY_DATABASE;

int yb_fetch_row_limit = 0;

int yb_fetch_size_limit = 0;

int yb_locks_min_txn_age = 1000;

int yb_locks_max_transactions = 16;

int yb_locks_txn_locks_per_tablet = 200;

int yb_walsender_poll_sleep_duration_nonempty_ms = 1;

int yb_walsender_poll_sleep_duration_empty_ms = 10;

int yb_reorderbuffer_max_changes_in_memory = 4096;

int yb_explicit_row_locking_batch_size = 1;

uint64_t yb_read_time = 0;
bool yb_is_read_time_ht = false;
bool yb_disable_catalog_version_check = false;

int yb_read_after_commit_visibility = 0;

bool yb_allow_block_based_sampling_algorithm = true;

bool yb_allow_separate_requests_for_sampling_stages = true;

// TODO(#24089): Once code duplication between yb_guc and ybc_util is removed, we should be able
// to use YB_SAMPLING_ALGORITHM_BLOCK_BASED_SAMPLING instead of 1 and do it in one place.
int32_t yb_sampling_algorithm = 1 /* YB_SAMPLING_ALGORITHM_BLOCK_BASED_SAMPLING */;

bool yb_refresh_matview_in_place = false;

// When set to a non-zero value to indicate the yb universe will have PostgreSQL running different
// versions.
// This should be set during the Upgrade and Rollback Phases of the YSQL major upgrade. This is set
// before the YSQL major upgrade starts, and can be reset as soon as the upgrade reaches the
// Monitoring phase.
// Use YBCPgYsqlMajorVersionUpgradeInProgress in order to know if the YSQL major upgrade is in
// progress.
int yb_major_version_upgrade_compatibility = 0;

// Indicates that the upgrade to the PostgreSQL version 15 has been completed.
// Use YBCPgYsqlMajorVersionUpgradeInProgress in order to know if the YSQL major upgrade is in
// progress.
bool yb_upgrade_to_pg15_completed = true;

bool yb_disable_auto_analyze = false;

bool yb_extension_upgrade = false;

bool yb_mixed_mode_expression_pushdown = true;

bool yb_debug_log_catcache_events = false;

bool yb_debug_log_snapshot_mgmt = false;

bool yb_debug_log_snapshot_mgmt_stack_trace = false;

bool yb_mixed_mode_saop_pushdown = false;

// Internal GUC to help a backend identify that the connection is from the Auto-Analyze service.
bool yb_use_internal_auto_analyze_service_conn = false;

bool yb_ddl_transaction_block_enabled = kEnableDdlTransactionBlocks;

bool yb_disable_ddl_transaction_block_for_read_committed = false;

int yb_fk_references_cache_limit = 65535;

bool yb_allow_dockey_bounds = true;

bool yb_xcluster_target_ddl_bypass = false;

bool yb_ignore_read_time_in_walsender = false;

bool yb_disable_pg_snapshot_mgmt_in_repeatable_read = false;

bool enable_object_locking_infra = true;

bool yb_enable_concurrent_ddl = false;

bool yb_enable_ddl_savepoint_infra = true;
