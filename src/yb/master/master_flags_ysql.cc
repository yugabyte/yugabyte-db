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
//

// Master gflags for YSQL: DDL handling and verification, catalog versions, backends, object locks
// and major upgrades. The Rust master reads them by name, or accepts them without honoring them yet
// (see master_flags_baseline.txt). Their names, types, defaults, help text, tags and AutoFlag
// classes are part of the yb-master command line and of auto_flags.json: change them only as you
// would change any released flag.

#include "yb/util/flag_validators.h"
#include "yb/util/flags.h"

DEFINE_RUNTIME_uint64(master_ysql_operation_lease_ttl_ms, 30 * 1000,
                      "The lifetime of ysql operation lease extensions. The ysql operation lease "
                      "allows tservers to host pg sessions and serve reads and writes to user data "
                      "through the YSQL API.");
TAG_FLAG(master_ysql_operation_lease_ttl_ms, advanced);

DEFINE_RUNTIME_uint64(ysql_operation_lease_ttl_client_buffer_ms, 2 * 1000,
                      "The difference between the duration masters and tservers use for ysql "
                      "operation lease TTLs. This is non-zero to account for clock skew and give "
                      "tservers time to clean up their existing pg sessions before the master "
                      "leader ignores them for exclusive table lock requests.");
TAG_FLAG(ysql_operation_lease_ttl_client_buffer_ms, advanced);

DEFINE_NON_RUNTIME_uint64(object_lock_cleanup_interval_ms, 5000,
                          "The interval between runs of the background cleanup task for "
                          "table-level locks held by unresponsive TServers.");

DEFINE_test_flag(bool, skip_launch_release_request, false,
    "If true, skip launching the release request after persisting it to in progress requests.");

DEFINE_test_flag(bool, allow_unknown_txn_release_request, false,
    "If true, do not error out if a release request comes in for an unknown transaction.");

DEFINE_test_flag(bool, pause_obj_lock_acquire_before_relaunch_check, false,
    "If true, pause after an acquire request is acked by all tservers, before checking whether "
    "the fanout must be relaunched for lease changes.");

DEFINE_RUNTIME_int32(wait_for_ysql_backends_catalog_version_master_tserver_rpc_timeout_ms,
    60 * 1000, // 60s
    "WaitForYsqlBackendsCatalogVersion master-to-tserver RPC timeout.");
TAG_FLAG(wait_for_ysql_backends_catalog_version_master_tserver_rpc_timeout_ms, advanced);

DEFINE_RUNTIME_uint32(ysql_backends_catalog_version_job_expiration_sec, 120, // 2m
    "Expiration time in seconds for a YSQL BackendsCatalogVersionJob. Recommended to set several"
    " times higher than tserver wait_for_ysql_backends_catalog_version_client_master_rpc_timeout_ms"
    " flag.");
TAG_FLAG(ysql_backends_catalog_version_job_expiration_sec, advanced);

DEFINE_test_flag(bool, assert_no_future_catalog_version, false,
    "Asserts that the clients are never requesting a catalog version which is higher "
    "than what is seen at the master. Used to assert that there are no stale master reads.");

DEFINE_test_flag(bool, block_wait_for_ysql_backends_catalog_version, false, // runtime-settable
    "If true, enable toggleable busy-wait at the beginning of WaitForYsqlBackendsCatalogVersion.");

DEFINE_test_flag(bool, wait_for_ysql_backends_catalog_version_take_leader_lock, true,
    "Take leader lock in WaitForYsqlBackendsCatalogVersion.");

DEFINE_test_flag(bool, ysql_backends_catalog_version_disable_abort_all_jobs, false,
    "Make YsqlBackendsManager::AbortAllJobs a no-op.");

DEFINE_test_flag(bool, skip_wait_for_ysql_backends_catalog_version, false,
    "Set WaitForYsqlBackendsCatalogVersion to immediately return success.");

DEFINE_RUNTIME_bool(retry_if_ddl_txn_verification_pending, true,
    "Whether to retry a transaction if it fails verification.");

DEFINE_RUNTIME_int32(wait_for_ysql_ddl_verification_timeout_ms, 200,
    "Timeout in milliseconds to wait for YSQL DDL transaction verification to finish");

DEFINE_test_flag(bool, disable_ysql_ddl_txn_verification, false,
    "Simulates a condition where the background process that checks whether the YSQL transaction "
    "was a success or a failure is indefinitely delayed");

DEFINE_test_flag(int32, ysql_max_random_delay_before_ddl_verification_usecs, 0,
                  "Maximum #usecs to randomly sleep before verifying a YSQL DDL transaction");

DEFINE_test_flag(bool, pause_ddl_rollback, false, "Pause DDL rollback");

DEFINE_test_flag(bool, hang_on_ddl_verification_progress, false,
    "Used in tests to simulate a hang while checking ddl verification progress.");

DEFINE_test_flag(double, ysql_fail_probability_of_catalog_writes_by_ddl_verification, 0.0,
    "Inject random failure in sys catalog writes made by ddl transaction verification");

DEFINE_test_flag(double, ysql_ddl_rollback_failure_probability, 0.0,
    "Inject random failure of ddl rollback operations");

DEFINE_test_flag(double, ysql_ddl_verification_failure_probability, 0.0,
    "Inject random failure of ddl verification operations");

DEFINE_test_flag(bool, disable_release_object_locks_on_ddl_verification, false,
    "When set, skip release object lock rpcs to tservers triggered at the end of DDL verification, "
    "that release object locks acquired by the DDL.");

// TODO (mbautin, 2019-12): switch the default to true after updating all external callers
// (yb-ctl, YugaWare) and unit tests.
DEFINE_RUNTIME_bool(master_auto_run_initdb, false,
    "Automatically run initdb on master leader initialization");

DEFINE_NON_RUNTIME_uint32(num_advisory_locks_tablets, 3,
    "Number of advisory lock tablets. Should be set before universe creation");
DEFINE_validator(num_advisory_locks_tablets, FLAG_GT_VALUE_VALIDATOR(0));

DEFINE_NON_RUNTIME_int32(ysql_tablespace_info_refresh_secs, 30,
    "Frequency at which the table to tablespace information will be updated in master "
    "from pg catalog tables. A value of -1 disables the refresh task.");

DEFINE_RUNTIME_int32(ysql_ddl_post_processing_failed_verification_retry_secs, 300,
    "Frequency in seconds at which the master leader will re-trigger DDL verification for "
    "YSQL DDL transactions in kDdlPostProcessingFailed state. A value of -1 disables this "
    "background task.");

DEFINE_RUNTIME_int32(pg_catalog_versions_cache_retry_ms, 200,
    "Retry delay in ms for the catalog version cache refresh background task when "
    "a refresh or submit failure occurs. Should be less than heartbeat_interval_ms/2.");
TAG_FLAG(pg_catalog_versions_cache_retry_ms, advanced);

DEFINE_test_flag(bool, simulate_catalog_version_refresh_failure, false,
    "When true, RefreshPgCatalogVersionCachePeriodically skips updating the cache, "
    "simulating a persistent refresh failure.");

DEFINE_test_flag(bool, log_catalog_version_cache_events, false,
    "Log cache hit/miss and refresh events for GetYsqlDBCatalogVersion.");

DEFINE_test_flag(int32, master_min_live_tservers_before_initdb, 0,
    "If positive, the master waits for at least this many tablet servers to "
    "register before launching initdb in RunNewClusterGlobalInitDB. Used by "
    "MiniYBCluster to avoid a startup race in which the postgres bootstrap "
    "inside initdb calls CreateTable on the master before any tablet servers "
    "are registered, which causes the bootstrap to fail with "
    "\"Not enough live tablet servers ...\" and the master to LOG(FATAL) in "
    "SetInitDbDone. Production code path is unchanged at the default value 0.");

DEFINE_test_flag(string, fail_ysql_catalog_upgrade_state_transition_from, "",
    "When set fail the transition to the provided state");

DEFINE_test_flag(bool, ysql_fail_cleanup_previous_version_catalog, false,
    "Fail the cleanup of the previous version ysql catalog");

DEFINE_UNKNOWN_int32(ysql_transaction_bg_task_wait_ms, 200,
  "Amount of time the catalog manager background task thread waits "
  "between runs");

DEFINE_test_flag(bool, skip_transaction_verification, false,
    "Test only flag to keep the txn metadata in SysTablesEntryPB and skip"
    " transaction verification on the master");

DEFINE_test_flag(double, ysql_ddl_transaction_verification_failure_probability, 0,
    "Inject random failure in checking transaction status for DDL transactions");

DEFINE_RUNTIME_int32(ddl_requester_liveness_check_interval_secs, 10,
    "Interval in seconds between liveness checks for a background DDL operation's initiating "
    "transaction. If the transaction is detected as aborted, the background operation is stopped.");
