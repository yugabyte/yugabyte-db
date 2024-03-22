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

// Flag for disabling runContext to Postgres's portal. Currently, each portal has two contexts.
// - PortalContext whose lifetime lasts for as long as the Portal object.
// - TmpContext whose lifetime lasts until one associated row of SELECT result set is sent out.
//
// We add one more context "ybRunContext".
// - Its lifetime will begin when PortalRun() is called to process a user statement request until
//   the end of the PortalRun() process.
// - A SELECT might be queried in small batches, and each batch is processed by one call to
//   PortalRun(). The "ybRunContext" is used for values that are private to one batch.
// - Use boolean experimental flag just in case introducing "ybRunContext" is a wrong idea.
DEFINE_UNKNOWN_bool(ysql_disable_portal_run_context, false, "Whether to use portal ybRunContext.");

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
