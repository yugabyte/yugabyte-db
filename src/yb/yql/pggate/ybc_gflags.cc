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

#include "yb/yql/pggate/ybc_gflags.h"

#include <string>

#include "yb/common/common_flags.h"

#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

DEFINE_UNKNOWN_bool(ysql_enable_reindex, false,
    "Enable REINDEX INDEX statement.");
TAG_FLAG(ysql_enable_reindex, advanced);
TAG_FLAG(ysql_enable_reindex, hidden);

DEFINE_UNKNOWN_bool(ysql_disable_server_file_access, false,
    "If true, disables read, write, and execute of local server files. "
    "File access can be re-enabled if set to false.");

DEFINE_NON_RUNTIME_bool(ysql_enable_profile, false, "Enable PROFILE feature.");

DEFINE_test_flag(string, ysql_conn_mgr_dowarmup_all_pools_mode, "none",
    "Enable precreation of server connections in every pool in Ysql Connection Manager and "
    "choose the mode of attachment of idle server connections to clients to serve their queries. "
    "ysql_conn_mgr_dowarmup is responsible for creating server connections only in "
    "yugabyte (user), yugabyte (database) pool during the initialization of connection "
    "manager process. This flag will create max(ysql_conn_mgr_min_conns_per_db, "
    "3) number of server connections in any pool whenever there is a requirement to create the "
    "first backend process in that particular pool.");

DEFINE_test_flag(uint32, ysql_conn_mgr_auth_delay_ms, 0,
    "Add a delay in od_auth_backend to simulate stalls during authentication with connection "
    " manager .");

DEFINE_NON_RUNTIME_bool(ysql_conn_mgr_superuser_sticky, true,
    "If enabled, make superuser connections sticky in Ysql Connection Manager.");

DEFINE_NON_RUNTIME_int32(ysql_conn_mgr_max_query_size, 4096,
    "Maximum size of the query which connection manager can process in the deploy phase or while"
    "forwarding the client query");

DEFINE_NON_RUNTIME_int32(ysql_conn_mgr_wait_timeout_ms, 10000,
    "ysql_conn_mgr_wait_timeout_ms denotes the waiting time in ms, before getting timeout while "
    "sending/receiving the packets at the socket in ysql connection manager. It is seen"
    " asan builds requires large wait timeout than other builds");

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_stats_interval, 1,
    "Interval (in secs) at which the stats for Ysql Connection Manager will be updated.");

// This gflag should be deprecated but kept to avoid breaking some customer
// clusters using it. Use ysql_catalog_preload_additional_table_list if possible.
DEFINE_NON_RUNTIME_bool(ysql_catalog_preload_additional_tables, false,
    "If true, YB catalog preloads a default set of tables upon connection "
    "creation and cache refresh: pg_am,pg_amproc,pg_cast,pg_cast,pg_inherits,"
    "pg_policy,pg_proc,pg_tablespace,pg_trigger.");

DEFINE_NON_RUNTIME_string(ysql_catalog_preload_additional_table_list, "",
    "A list of catalog tables that YSQL preloads additionally upon "
    "connection start-up and cache refreshes. Catalog table names must start with pg_."
    "Invalid catalog names are ignored. Comma separated. Example: pg_range,pg_proc."
    "If both ysql_catalog_preload_additional_tables and "
    "ysql_catalog_preload_additional_table_list are set, we take a union of "
    "both the default list and the user-specified list.");

DEFINE_NON_RUNTIME_bool(ysql_disable_global_impact_ddl_statements, false,
    "If true, disable global impact ddl statements in per database catalog "
    "version mode.");

DEFINE_NON_RUNTIME_bool(
    ysql_minimal_catalog_caches_preload, false,
    "Fill postgres' caches with system items only");

DEFINE_RUNTIME_PREVIEW_bool(
    ysql_conn_mgr_version_matching, false,
    "If true, does selection of transactional backends based on logical client version");

DEFINE_RUNTIME_PREVIEW_bool(
    ysql_conn_mgr_version_matching_connect_higher_version, true,
    "If ysql_conn_mgr_version_matching is enabled is enabled, then connect to higher version "
    "server if this flag is set to true");

DEFINE_NON_RUNTIME_bool(ysql_block_dangerous_roles, false,
    "Block roles that can potentially be used to escalate to superuser privileges. Intended to be "
    "used with superuser login disabled, such as in YBM. When true, this assumes those blocked "
    "roles are not already in use.");

DEFINE_RUNTIME_PREVIEW_bool(
    ysql_enable_pg_export_snapshot, false,
    "Enables the support for synchronizing snapshots across transactions, using pg_export_snapshot "
    "and SET TRANSACTION SNAPSHOT");

DEFINE_NON_RUNTIME_bool(ysql_enable_neghit_full_inheritscache, true,
    "When set to true, a (fully) preloaded inherits cache returns negative cache hits"
    " right away without incurring a master lookup");

DEFINE_NON_RUNTIME_bool(ysql_enable_read_request_cache_for_connection_auth, false,
    "If true, use tserver response cache for authorization processing "
    "during connection setup. Only applicable when connection manager "
    "is used.");

DEFINE_NON_RUNTIME_bool(
    ysql_enable_scram_channel_binding, false,
    "Offer the option of SCRAM-SHA-256-PLUS (i.e. SCRAM with channel binding) as an SASL method if "
    "the server supports it in the SASL-Authentication message. This flag is disabled by default "
    "as connection manager does not support SCRAM with channel binding and enabling it would "
    "cause different behaviour vis-a-vis direct connections to postgres.");

DEFINE_NON_RUNTIME_bool(ysql_enable_relcache_init_optimization, true,
    "If applicable, new connections that need to rebuild relcache init file will not "
    "do it directly which can cause memory spike on such a new connection until it is "
    "disconnected. Instead an internal super user connection is made to perform the "
    "relcache init file rebuild.");

DEFINE_test_flag(bool, ysql_bypass_auto_analyze_auth_check, false,
    "Bypass the yb-tserver-key authentication method check when connecting using "
    "yb_auto_analyze backend type.");

DEFINE_test_flag(int64, delay_after_table_analyze_ms, 0,
    "Add this delay after each table is analyzed.");

DECLARE_bool(ysql_enable_colocated_tables_with_tablespaces);
DECLARE_bool(TEST_ysql_enable_db_logical_client_version_mode);
DECLARE_bool(TEST_ysql_yb_enable_ddl_savepoint_support);

DECLARE_bool(TEST_generate_ybrowid_sequentially);
DECLARE_bool(TEST_ysql_log_perdb_allocated_new_objectid);

DECLARE_bool(use_fast_backward_scan);
DECLARE_uint32(ysql_max_invalidation_message_queue_size);
DECLARE_uint32(max_replication_slots);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_bool(ysql_yb_enable_implicit_dynamic_tables_logical_replication);

namespace {

bool PreloadAdditionalCatalogListValidator(const char* flag_name, const std::string& flag_val) {
  for (const char& c : flag_val) {
    if (c != '_' && c != ',' && !islower(c)) {
      LOG_FLAG_VALIDATION_ERROR(flag_name, flag_val) << "Found invalid character '" << c << "'";
      return false;
    }
  }

  return true;
}

}  // namespace

DEFINE_validator(ysql_catalog_preload_additional_table_list, PreloadAdditionalCatalogListValidator);

namespace yb::pggate {

extern "C" {

const YbcPgGFlagsAccessor* YBCGetGFlags() {
  // clang-format off
  static YbcPgGFlagsAccessor accessor = {
      .log_ysql_catalog_versions                = &FLAGS_log_ysql_catalog_versions,
      .ysql_catalog_preload_additional_tables   = &FLAGS_ysql_catalog_preload_additional_tables,
      .ysql_disable_index_backfill              = &FLAGS_ysql_disable_index_backfill,
      .ysql_disable_server_file_access          = &FLAGS_ysql_disable_server_file_access,
      .ysql_enable_reindex                      = &FLAGS_ysql_enable_reindex,
      .ysql_num_databases_reserved_in_db_catalog_version_mode =
          &FLAGS_ysql_num_databases_reserved_in_db_catalog_version_mode,
      .ysql_output_buffer_size                  = &FLAGS_ysql_output_buffer_size,
      .ysql_output_flush_size                   = &FLAGS_ysql_output_flush_size,
      .ysql_sequence_cache_minval               = &FLAGS_ysql_sequence_cache_minval,
      .ysql_session_max_batch_size              = &FLAGS_ysql_session_max_batch_size,
      .ysql_sleep_before_retry_on_txn_conflict  = &FLAGS_ysql_sleep_before_retry_on_txn_conflict,
      .ysql_colocate_database_by_default        = &FLAGS_ysql_colocate_database_by_default,
      .ysql_enable_read_request_caching         = &FLAGS_ysql_enable_read_request_caching,
      .ysql_enable_profile                      = &FLAGS_ysql_enable_profile,
      .ysql_disable_global_impact_ddl_statements =
          &FLAGS_ysql_disable_global_impact_ddl_statements,
      .ysql_minimal_catalog_caches_preload      = &FLAGS_ysql_minimal_catalog_caches_preload,
      .ysql_enable_colocated_tables_with_tablespaces =
          &FLAGS_ysql_enable_colocated_tables_with_tablespaces,
      .ysql_enable_create_database_oid_collision_retry =
          &FLAGS_ysql_enable_create_database_oid_collision_retry,
      .ysql_catalog_preload_additional_table_list =
          FLAGS_ysql_catalog_preload_additional_table_list.c_str(),
      .ysql_use_relcache_file                   = &FLAGS_ysql_use_relcache_file,
      .ysql_use_optimized_relcache_update       = &FLAGS_ysql_use_optimized_relcache_update,
      .ysql_enable_pg_per_database_oid_allocator =
          &FLAGS_ysql_enable_pg_per_database_oid_allocator,
      .ysql_enable_db_catalog_version_mode =
          &FLAGS_ysql_enable_db_catalog_version_mode,
      .TEST_hide_details_for_pg_regress =
          &FLAGS_TEST_hide_details_for_pg_regress,
      .TEST_generate_ybrowid_sequentially =
          &FLAGS_TEST_generate_ybrowid_sequentially,
      .ysql_use_fast_backward_scan = &FLAGS_use_fast_backward_scan,
      .TEST_ysql_conn_mgr_dowarmup_all_pools_mode =
          FLAGS_TEST_ysql_conn_mgr_dowarmup_all_pools_mode.c_str(),
      .TEST_ysql_enable_db_logical_client_version_mode =
          &FLAGS_TEST_ysql_enable_db_logical_client_version_mode,
      .ysql_conn_mgr_superuser_sticky = &FLAGS_ysql_conn_mgr_superuser_sticky,
      .TEST_ysql_log_perdb_allocated_new_objectid =
          &FLAGS_TEST_ysql_log_perdb_allocated_new_objectid,
      .ysql_conn_mgr_version_matching = &FLAGS_ysql_conn_mgr_version_matching,
      .ysql_conn_mgr_version_matching_connect_higher_version =
          &FLAGS_ysql_conn_mgr_version_matching_connect_higher_version,
      .ysql_block_dangerous_roles = &FLAGS_ysql_block_dangerous_roles,
      .ysql_sequence_cache_method = FLAGS_ysql_sequence_cache_method.c_str(),
      .ysql_conn_mgr_sequence_support_mode = FLAGS_ysql_conn_mgr_sequence_support_mode.c_str(),
      .ysql_conn_mgr_max_query_size = &FLAGS_ysql_conn_mgr_max_query_size,
      .ysql_conn_mgr_wait_timeout_ms = &FLAGS_ysql_conn_mgr_wait_timeout_ms,
      .ysql_enable_pg_export_snapshot = &FLAGS_ysql_enable_pg_export_snapshot,
      .ysql_enable_neghit_full_inheritscache =
        &FLAGS_ysql_enable_neghit_full_inheritscache,
      .enable_object_locking_for_table_locks =
          &FLAGS_enable_object_locking_for_table_locks,
      .TEST_ysql_yb_enable_ddl_savepoint_support =
          &FLAGS_TEST_ysql_yb_enable_ddl_savepoint_support,
      .ysql_max_invalidation_message_queue_size =
          &FLAGS_ysql_max_invalidation_message_queue_size,
      .ysql_max_replication_slots = &FLAGS_max_replication_slots,
      .yb_max_recursion_depth = &FLAGS_yb_max_recursion_depth,
      .ysql_conn_mgr_stats_interval =
          &FLAGS_ysql_conn_mgr_stats_interval,
      .ysql_enable_read_request_cache_for_connection_auth =
          &FLAGS_ysql_enable_read_request_cache_for_connection_auth,
      .ysql_yb_enable_implicit_dynamic_tables_logical_replication =
          &FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication,
      .timestamp_history_retention_interval_sec =
          &FLAGS_timestamp_history_retention_interval_sec,
      .ysql_enable_scram_channel_binding = &FLAGS_ysql_enable_scram_channel_binding,
      .TEST_ysql_conn_mgr_auth_delay_ms = &FLAGS_TEST_ysql_conn_mgr_auth_delay_ms,
      .ysql_enable_relcache_init_optimization = &FLAGS_ysql_enable_relcache_init_optimization,
      .TEST_ysql_bypass_auto_analyze_auth_check = &FLAGS_TEST_ysql_bypass_auto_analyze_auth_check,
      .TEST_delay_after_table_analyze_ms = &FLAGS_TEST_delay_after_table_analyze_ms,
  };
  // clang-format on
  return &accessor;
}

}  // extern "C"

}  // namespace yb::pggate
