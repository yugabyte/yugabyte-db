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

#include "yb/yql/pggate/util/yb_guc.h"

bool yb_debug_log_docdb_requests = false;

bool yb_enable_hash_batch_in = true;

bool yb_non_ddl_txn_for_sys_tables_allowed = false;

bool yb_format_funcs_include_yb_metadata = false;

bool yb_force_global_transaction = false;

bool suppress_nonpg_logs = false;

bool yb_binary_restore = false;

bool yb_pushdown_strict_inequality = true;

bool yb_pushdown_is_not_null = true;

bool yb_enable_pg_locks = true;

bool yb_run_with_explain_analyze = false;

bool yb_enable_add_column_missing_default = true;

bool yb_enable_replication_commands = false;

bool yb_enable_replication_slot_consumption = false;

bool yb_enable_alter_table_rewrite = true;

bool yb_enable_replica_identity = false;

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

int yb_walsender_poll_sleep_duration_empty_ms = 1 * 1000;

int yb_explicit_row_locking_batch_size = 1;

uint64_t yb_read_time = 0;
bool yb_is_read_time_ht = false;

int yb_read_after_commit_visibility = 0;
