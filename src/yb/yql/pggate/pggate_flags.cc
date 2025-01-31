//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

// This file contains flag definitions that should be known to master, tserver, and pggate
// (linked into postgres).

#include "yb/util/flags.h"
#include "yb/yql/pggate/pggate_flags.h"

DEPRECATE_FLAG(int32, pgsql_rpc_keepalive_time_ms, "02_2024");

DEFINE_UNKNOWN_int32(pggate_rpc_timeout_secs, 60,
             "Timeout for RPCs from pggate to YB cluster");

DEFINE_UNKNOWN_int32(pggate_ybclient_reactor_threads, 2,
             "The number of reactor threads to be used for processing ybclient "
             "requests originating in the PostgreSQL proxy server");

DEFINE_UNKNOWN_string(pggate_master_addresses, "",
              "Addresses of the master servers to which the PostgreSQL proxy server connects.");

DEFINE_UNKNOWN_int32(pggate_tserver_shm_fd, -1,
              "File descriptor of the local tablet server's shared memory.");

DEPRECATE_FLAG(bool, TEST_pggate_ignore_tserver_shm, "02_2024");

DEFINE_UNKNOWN_int32(ysql_request_limit, 1024,
             "Maximum number of requests to be sent at once");

DEPRECATE_FLAG(uint64, ysql_prefetch_limit, "04_2023");

DEPRECATE_FLAG(double, ysql_backward_prefetch_scale_factor, "11_2022");

DEFINE_UNKNOWN_uint64(ysql_session_max_batch_size, 3072,
              "Use session variable ysql_session_max_batch_size instead. "
              "Maximum batch size for buffered writes between PostgreSQL server and YugaByte DocDB "
              "services");

DEFINE_UNKNOWN_bool(ysql_non_txn_copy, false,
            "Execute COPY inserts non-transactionally.");

DEFINE_test_flag(bool, ysql_disable_transparent_cache_refresh_retry, false,
    "Never transparently retry commands that fail with cache version mismatch error");

DEFINE_test_flag(int64, inject_delay_between_prepare_ybctid_execute_batch_ybctid_ms, 0,
    "Inject delay between creation and dispatch of RPC ops for testing");

// TODO(dmitry): Next flag is used for testing purpose to simulate tablet splitting.
// It is better to rewrite tests and use real tablet splitting instead of the emulation.
// Flag should be removed after this (#13079)
DEFINE_test_flag(bool, index_read_multiple_partitions, false,
      "Test flag used to simulate tablet spliting by joining tables' partitions.");

DEFINE_UNKNOWN_int32(ysql_output_buffer_size, 262144,
             "Size of postgres-level output buffer, in bytes. "
             "While fetched data resides within this buffer and hasn't been flushed to client yet, "
             "we're free to transparently restart operation in case of restart read error.");

DEPRECATE_FLAG(bool, ysql_enable_update_batching, "10_2022");

DEFINE_UNKNOWN_bool(ysql_suppress_unsupported_error, false,
            "Suppress ERROR on use of unsupported SQL statement and use WARNING instead");

DEFINE_NON_RUNTIME_bool(ysql_suppress_unsafe_alter_notice, false,
    "Suppress NOTICE on use of unsafe ALTER statements");

DEFINE_UNKNOWN_int32(ysql_sequence_cache_minval, 100,
             "Set how many sequence numbers to be preallocated in cache.");

DEFINE_RUNTIME_string(ysql_sequence_cache_method, "connection",
    "Where sequence values are cached for both existing and new sequences. Valid values are "
    "\"connection\" and \"server\"");

DEFINE_RUNTIME_string(ysql_conn_mgr_sequence_support_mode, "pooled_without_curval_lastval",
    "Sequence support mode when connection manager is enabled. When set to "
    "'pooled_without_curval_lastval', currval() and lastval() functions are not supported. "
    "When set to 'pooled_with_curval_lastval', currval() and lastval() functions are supported. "
    "In these both settings, the monotonic order of sequence is not guaranteed if the "
    "'ysql_sequence_cache_method' is set to 'connection'. To support monotonic order also set "
    "this flag to 'session'");

// Top-level flag to enable all YSQL beta features.
DEFINE_UNKNOWN_bool(ysql_beta_features, false,
            "Whether to enable all ysql beta features");

// Per-feature flags -- only relevant if ysql_beta_features is false.

DEFINE_UNKNOWN_bool(ysql_beta_feature_tablegroup, false,
            "Whether to enable the incomplete 'tablegroup' ysql beta feature");

TAG_FLAG(ysql_beta_feature_tablegroup, hidden);

DEFINE_UNKNOWN_bool(
    ysql_colocate_database_by_default, false, "Enable colocation by default on each database.");

DEFINE_UNKNOWN_bool(ysql_beta_feature_tablespace_alteration, false,
            "Whether to enable the incomplete 'tablespace_alteration' beta feature");

TAG_FLAG(ysql_beta_feature_tablespace_alteration, hidden);

DEFINE_UNKNOWN_bool(ysql_serializable_isolation_for_ddl_txn, false,
            "Whether to use serializable isolation for separate DDL-only transactions. "
            "By default, repeatable read isolation is used. "
            "This flag should go away once full transactional DDL is implemented.");

DEFINE_UNKNOWN_int32(ysql_select_parallelism, -1,
            "Number of read requests to issue in parallel to tablets of a table "
            "for SELECT.");

DEFINE_UNKNOWN_bool(ysql_sleep_before_retry_on_txn_conflict, true,
            "Whether to sleep before retrying the write on transaction conflicts.");

DEPRECATE_FLAG(int32, ysql_max_read_restart_attempts, "12_2023");

DEPRECATE_FLAG(int32, ysql_max_write_restart_attempts, "12_2023");

// This flag was used to disable ybRunContext, which was introduced by YB commit
// 15c68094b07004b5a844b0221e4b7514c4d7dc9a to plug a memory leak in portal. Later, upstream PG
// fixed the leak in commit f2004f19ed9c9228d3ea2b12379ccb4b9212641f. As a result, ybRunContext was
// left redundant, so D37419 removed it. See commit summary for details.
DEPRECATE_FLAG(bool, ysql_disable_portal_run_context, "08_2024");

#ifdef NDEBUG
constexpr bool kEnableReadCommitted = false;
#else
constexpr bool kEnableReadCommitted = true;
#endif
DEFINE_NON_RUNTIME_bool(
    yb_enable_read_committed_isolation, kEnableReadCommitted,
    "Defines how READ COMMITTED (which is our default SQL-layer isolation) and READ UNCOMMITTED "
    "are mapped internally. If false (default), both map to the stricter REPEATABLE READ "
    "implementation. If true, both use the new READ COMMITTED implementation instead.");

DEFINE_test_flag(bool, yb_lwlock_crash_after_acquire_pg_stat_statements_reset, false,
             "Issue sigkill for crash test after acquiring a LWLock in pg_stat_statements reset.");

DEFINE_UNKNOWN_int32(ysql_num_databases_reserved_in_db_catalog_version_mode, 10,
             "In per database catalog version mode, if the number of existing databases "
             "are within this number to the maximum number of databases allowed, then "
             "fail the create database statement.");
TAG_FLAG(ysql_num_databases_reserved_in_db_catalog_version_mode, advanced);
TAG_FLAG(ysql_num_databases_reserved_in_db_catalog_version_mode, hidden);

DEFINE_NON_RUNTIME_bool(ysql_enable_create_database_oid_collision_retry, true,
                        "Whether to retry YSQL CREATE DATABASE statement "
                        "if oid collision happens.");
TAG_FLAG(ysql_enable_create_database_oid_collision_retry, advanced);

DEFINE_NON_RUNTIME_bool(ysql_use_relcache_file, true, "Use relcache init file");

DEFINE_NON_RUNTIME_bool(ysql_use_optimized_relcache_update, true,
    "Use optimized relcache update during connection startup and cache refresh.");
