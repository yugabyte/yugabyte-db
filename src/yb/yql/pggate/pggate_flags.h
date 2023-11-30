// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#pragma once

#include "yb/util/flags.h"

DECLARE_int32(pgsql_rpc_keepalive_time_ms);
DECLARE_int32(pggate_rpc_timeout_secs);
DECLARE_int32(pggate_ybclient_reactor_threads);
DECLARE_string(pggate_master_addresses);
DECLARE_int32(pggate_tserver_shm_fd);
DECLARE_int32(ysql_request_limit);
DECLARE_uint64(ysql_prefetch_limit);
DECLARE_double(ysql_backward_prefetch_scale_factor);
DECLARE_uint64(ysql_session_max_batch_size);
DECLARE_bool(ysql_non_txn_copy);
DECLARE_int32(ysql_max_read_restart_attempts);
DECLARE_bool(TEST_ysql_disable_transparent_cache_refresh_retry);
DECLARE_int64(TEST_inject_delay_between_prepare_ybctid_execute_batch_ybctid_ms);
DECLARE_bool(TEST_index_read_multiple_partitions);
DECLARE_int32(ysql_output_buffer_size);
DECLARE_int32(ysql_select_parallelism);
DECLARE_int32(ysql_sequence_cache_minval);
DECLARE_int32(ysql_num_databases_reserved_in_db_catalog_version_mode);

DECLARE_bool(ysql_suppress_unsupported_error);
DECLARE_bool(ysql_suppress_unsafe_alter_notice);

DECLARE_bool(ysql_beta_features);
DECLARE_bool(ysql_beta_feature_tablegroup);
DECLARE_bool(ysql_colocate_database_by_default);
DECLARE_bool(ysql_beta_feature_tablespace_alteration);
DECLARE_bool(ysql_serializable_isolation_for_ddl_txn);
DECLARE_int32(ysql_max_write_restart_attempts);
DECLARE_bool(ysql_sleep_before_retry_on_txn_conflict);
DECLARE_bool(ysql_disable_portal_run_context);
DECLARE_bool(TEST_yb_lwlock_crash_after_acquire_pg_stat_statements_reset);
DECLARE_bool(TEST_yb_lwlock_error_after_acquire_pg_stat_statements_reset);
DECLARE_bool(TEST_yb_test_fail_matview_refresh_after_creation);
DECLARE_bool(ysql_enable_read_request_caching);
DECLARE_bool(ysql_enable_create_database_oid_collision_retry);
