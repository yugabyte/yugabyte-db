// Copyright (c) YugabyteDB, Inc.
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

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const bool*     log_ysql_catalog_versions;
  const bool*     ysql_catalog_preload_additional_tables;
  const bool*     ysql_disable_index_backfill;
  const bool*     ysql_disable_server_file_access;
  const bool*     ysql_enable_reindex;
  const int32_t*  ysql_num_databases_reserved_in_db_catalog_version_mode;
  const int32_t*  ysql_output_buffer_size;
  const int32_t*  ysql_output_flush_size;
  const int32_t*  ysql_sequence_cache_minval;
  const uint64_t* ysql_session_max_batch_size;
  const bool*     ysql_sleep_before_retry_on_txn_conflict;
  const bool*     ysql_colocate_database_by_default;
  const bool*     ysql_enable_read_request_caching;
  const bool*     ysql_enable_profile;
  const bool*     ysql_disable_global_impact_ddl_statements;
  const bool*     ysql_minimal_catalog_caches_preload;
  const bool*     ysql_enable_colocated_tables_with_tablespaces;
  const bool*     ysql_enable_create_database_oid_collision_retry;
  const char*     ysql_catalog_preload_additional_table_list;
  const bool*     ysql_use_relcache_file;
  const bool*     ysql_use_optimized_relcache_update;
  const bool*     ysql_enable_pg_per_database_oid_allocator;
  const bool*     ysql_enable_db_catalog_version_mode;
  const bool*     TEST_hide_details_for_pg_regress;
  const bool*     TEST_generate_ybrowid_sequentially;
  const bool*     ysql_use_fast_backward_scan;
  const char*     TEST_ysql_conn_mgr_dowarmup_all_pools_mode;
  const bool*     TEST_ysql_enable_db_logical_client_version_mode;
  const bool*     ysql_conn_mgr_superuser_sticky;
  const bool*     TEST_ysql_log_perdb_allocated_new_objectid;
  const bool*     ysql_conn_mgr_version_matching;
  const bool*     ysql_conn_mgr_version_matching_connect_higher_version;
  const bool*     ysql_block_dangerous_roles;
  const char*     ysql_sequence_cache_method;
  const char*     ysql_conn_mgr_sequence_support_mode;
  const int32_t*  ysql_conn_mgr_max_query_size;
  const int32_t*  ysql_conn_mgr_wait_timeout_ms;
  const bool*     ysql_enable_neghit_full_inheritscache;
  const bool*     enable_object_locking_for_table_locks;
  const bool*     ysql_yb_enable_ddl_savepoint_support;
  const uint32_t* ysql_max_invalidation_message_queue_size;
  const uint32_t* ysql_max_replication_slots;
  const uint32_t* yb_max_recursion_depth;
  const uint32_t* ysql_conn_mgr_stats_interval;
  const bool*     ysql_enable_read_request_cache_for_connection_auth;
  // TODO(arpan): Currently, the following flag is marked UNKOWN. If it turns out to be runtime
  // updatable, we would need to move it out of here and perhaps declare a GUC instead.
  const int32_t*  timestamp_history_retention_interval_sec;
  const bool*     ysql_enable_scram_channel_binding;
  const uint32_t* TEST_ysql_conn_mgr_auth_delay_ms;
  const bool*     ysql_enable_relcache_init_optimization;
  const char *    placement_cloud;
  const char *    placement_region;
  const char *    placement_zone;
  const bool*     TEST_ysql_bypass_auto_analyze_auth_check;
  const int64_t*  TEST_delay_after_table_analyze_ms;
  const bool*     TEST_ysql_yb_enable_listen_notify;
  const bool*     TEST_enable_obj_tuple_locks;
} YbcPgGFlagsAccessor;

const YbcPgGFlagsAccessor* YBCGetGFlags();

#ifdef __cplusplus
}  // extern "C"
#endif
