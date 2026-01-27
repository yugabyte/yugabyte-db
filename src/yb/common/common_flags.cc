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

#include "yb/common/common_flags.h"
#include "yb/util/flags.h"
#include "yb/util/flag_validators.h"
#include "yb/util/size_literals.h"

using namespace yb::size_literals;

// Note that this is used by the client or master only, not by tserver.
DEFINE_RUNTIME_int32(yb_num_shards_per_tserver, kAutoDetectNumShardsPerTServer,
    "The default number of shards per table per tablet server when a table is created. If the "
    "value is -1, the system automatically determines an appropriate value based on the number of "
    "CPU cores; it is determined to 1 if enable_automatic_tablet_splitting is set to true.");

DEFINE_RUNTIME_int32(ysql_num_shards_per_tserver, kAutoDetectNumShardsPerTServer,
    "The default number of shards per YSQL table per tablet server when a table is created. If the "
    "value is -1, the system automatically determines an appropriate value based on the number of "
    "CPU cores; it is determined to 1 if enable_automatic_tablet_splitting is set to true.");

DEFINE_UNKNOWN_bool(ysql_disable_index_backfill, false,
    "A kill switch to disable multi-stage backfill for YSQL indexes.");
TAG_FLAG(ysql_disable_index_backfill, hidden);
TAG_FLAG(ysql_disable_index_backfill, advanced);

DEPRECATE_FLAG(bool, enable_pg_savepoints, "04_2024");

DEFINE_RUNTIME_AUTO_bool(enable_automatic_tablet_splitting, kExternal, false, true,
    "If false, disables automatic tablet splitting driven from the yb-master side, and in this "
    "case the value of tserver's ysql_num_tablets is recommended to be set to -1.");

DEFINE_UNKNOWN_bool(log_ysql_catalog_versions, false,
    "Log YSQL catalog events. For debugging purposes.");
TAG_FLAG(log_ysql_catalog_versions, hidden);

DEPRECATE_FLAG(bool, disable_hybrid_scan, "11_2022");

DEFINE_NON_RUNTIME_bool(enable_wait_queues, true,
    "If true, enable wait queues that help provide Wait-on-Conflict behavior during conflict "
    "resolution whenever required. Enabling this flag enables deadlock detection as well.");
TAG_FLAG(enable_wait_queues, advanced);

DEPRECATE_FLAG(bool, enable_deadlock_detection, "09_2023");

DEFINE_NON_RUNTIME_bool(disable_deadlock_detection, false,
    "If true, disables deadlock detection. This can be used in conjunction with enable_wait_queues "
    "in case it is desirable to disable deadlock detection with wait queues enabled. This should "
    "only be done if the db operator can guarantee that deadlocks will be fully avoided by the "
    "app layer, and is not recommended for most use cases.");
TAG_FLAG(disable_deadlock_detection, advanced);
TAG_FLAG(disable_deadlock_detection, hidden);

DEFINE_RUNTIME_PG_FLAG(bool, yb_ddl_rollback_enabled, true,
    "If true, upon failure of a YSQL DDL transaction that affects the DocDB syscatalog, the "
    "YB-Master will rollback the changes made to the DocDB syscatalog.");

DEFINE_NON_RUNTIME_bool(ysql_enable_db_catalog_version_mode, true,
    "Enable the per database catalog version mode, a DDL statement that only "
    "affects the current database will only increment catalog version for "
    "the current database.");
TAG_FLAG(ysql_enable_db_catalog_version_mode, advanced);
TAG_FLAG(ysql_enable_db_catalog_version_mode, hidden);

DEFINE_test_flag(bool, ysql_enable_db_logical_client_version_mode, true,
    "Enable the per database logical client version mode, a DDL statement that only "
    "affects the current database will only increment catalog version for "
    "the current database.");

DEFINE_RUNTIME_uint32(wait_for_ysql_backends_catalog_version_client_master_rpc_margin_ms, 5000,
    "For a WaitForYsqlBackendsCatalogVersion client-to-master RPC, the amount of time to reserve"
    " out of the RPC timeout to respond back to client. If margin is zero, client will determine"
    " timeout without receiving response from master. Margin should be set high enough to cover"
    " processing and RPC time for the response. It should be lower than"
    " wait_for_ysql_backends_catalog_version_client_master_rpc_timeout_ms.");
TAG_FLAG(wait_for_ysql_backends_catalog_version_client_master_rpc_margin_ms, advanced);

// TODO(#13369): use this flag in tserver.
DEFINE_NON_RUNTIME_uint32(master_ts_ysql_catalog_lease_ms, 10000, // 10s
    "Lease period between master and tserver that guarantees YSQL system catalog is not stale."
    " Must be higher than --heartbeat_interval_ms, preferably many times higher.");
TAG_FLAG(master_ts_ysql_catalog_lease_ms, advanced);
TAG_FLAG(master_ts_ysql_catalog_lease_ms, hidden);

DEFINE_NON_RUNTIME_bool(
    ysql_enable_colocated_tables_with_tablespaces, false,
    "Enable creation of colocated tables with a specified placement policy via a tablespace."
    "If true, creating a colocated table  will colocate the table on an implicit "
    "tablegroup that is determined by the tablespace it uses. We turn the feature off by default.");

// We expect that consensus_max_batch_size_bytes + 1_KB would be less than rpc_max_message_size.
// Otherwise such batch would be rejected by RPC layer.
DEFINE_RUNTIME_uint64(consensus_max_batch_size_bytes, 4_MB,
    "The maximum per-tablet RPC batch size when updating peers. The sum of "
    "consensus_max_batch_size_bytes and 1KB should be less than rpc_max_message_size");
TAG_FLAG(consensus_max_batch_size_bytes, advanced);

DEFINE_UNKNOWN_int64(rpc_throttle_threshold_bytes, 1_MB,
    "Throttle inbound RPC calls larger than specified size on hitting mem tracker soft limit. "
    "Throttling is disabled if negative value is specified. The value must be at least 16 and less "
    "than the strictly enforced consensus_max_batch_size_bytes.");

DEFINE_NON_RUNTIME_bool(ysql_enable_pg_per_database_oid_allocator, true,
    "If true, enable per-database PG new object identifier allocator.");
TAG_FLAG(ysql_enable_pg_per_database_oid_allocator, advanced);
TAG_FLAG(ysql_enable_pg_per_database_oid_allocator, hidden);

DEFINE_RUNTIME_int32(
    ysql_clone_pg_schema_rpc_timeout_ms, 10 * 60 * 1000,  // 10 min.
    "Timeout used by the master when attempting to clone PG Schema objects using an async task to "
    "tserver");
TAG_FLAG(ysql_clone_pg_schema_rpc_timeout_ms, advanced);

DEFINE_RUNTIME_AUTO_bool(
    yb_enable_cdc_consistent_snapshot_streams, kLocalPersisted, false, true,
    "Enable support for CDC Consistent Snapshot Streams");

DEFINE_RUNTIME_AUTO_PG_FLAG(
    bool, yb_enable_replication_slot_consumption, kLocalPersisted, false, true,
    "Enable consumption of changes via replication slots."
    "Requires yb_enable_replication_commands to be true.");

DEFINE_RUNTIME_PG_PREVIEW_FLAG(bool, yb_enable_consistent_replication_from_hash_range, false,
    "Enable consumption of consistent changes via replication slots from "
    "a hash range of a table.");

DEFINE_NON_RUNTIME_PREVIEW_bool(ysql_yb_enable_implicit_dynamic_tables_logical_replication, false,
    "When set to true, modifications to publication will be reflected implicitly. "
    "This replaces the previous mechanism of periodic publication refresh with PG "
    "like semantics for dynamic tables");

DEFINE_NON_RUNTIME_bool(TEST_hide_details_for_pg_regress, false,
    "For pg_regress tests, alter error messages that contain unstable items such as ybctid, oids, "
    "and catalog version numbers to hide such details or omit the message entirely.");
TAG_FLAG(TEST_hide_details_for_pg_regress, hidden);

DEFINE_NON_RUNTIME_uint32(max_replication_slots, 10,
     "Controls the maximum number of replication slots that are allowed to exist on a cluster.");

// The following flags related to the cloud, region and availability zone that an instance is
// started in. These are passed in from whatever provisioning mechanics start the servers. They
// are used for generic placement policies on table creation and tablet load balancing, to
// either constrain data to a certain location (table A should only live in aws.us-west2.a), or to
// define the required level of fault tolerance expected (table B should have N replicas, across
// two regions of AWS and one of GCE).
//
// These are currently for use in a cloud-based deployment, but could be retrofitted to work for
// an on-premise deployment as well, with datacenter, cluster and rack levels, for example.
DEFINE_NON_RUNTIME_string(placement_cloud, "cloud1",
    "The cloud in which this instance is started.");
DEFINE_NON_RUNTIME_string(placement_region, "datacenter1",
    "The cloud region in which this instance is started.");
DEFINE_NON_RUNTIME_string(placement_zone, "rack1",
    "The cloud availability zone in which this instance is started.");

DEFINE_test_flag(bool, check_catalog_version_overflow, false,
    "Check whether received catalog version is unreasonably too big");

DEFINE_RUNTIME_PG_FLAG(bool, yb_enable_invalidation_messages, true,
    "True to enable invalidation messages");

// Keep in sync with the same definition in ybc_guc.h
#ifdef NDEBUG
constexpr bool kEnableDdlTransactionBlocks = true;
#else
constexpr bool kEnableDdlTransactionBlocks = false;
#endif
DEFINE_NON_RUNTIME_PG_FLAG(bool, yb_ddl_transaction_block_enabled, kEnableDdlTransactionBlocks,
    "If true, DDL operations in YSQL will execute within the active transaction"
    "block instead of their separate transactions. Ensure DDL atomicity is "
    "enabled via ysql_yb_enable_ddl_atomicity_infra and ysql_yb_ddl_rollback_enabled flags.");

DEFINE_NON_RUNTIME_PG_FLAG(bool, yb_disable_ddl_transaction_block_for_read_committed, false,
    "If true, DDL operations in READ COMMITTED mode will be executed in a separate DDL transaction "
    "instead of the as part of the enclosing transaction block even if "
    "ysql_yb_ddl_transaction_block_enabled is true. In other words, for Read Committed, fall back "
    "to the mode when ysql_yb_ddl_transaction_block_enabled is false.");

DEFINE_RUNTIME_AUTO_PG_FLAG(
    bool, yb_enable_ddl_savepoint_infra, kLocalPersisted, false, true,
    "Auto flag that controls whether DDL savepoint support can be safely enabled "
    "during upgrade. Both this flag and ysql_yb_enable_ddl_savepoint_support "
    "must be true to enable the feature.");
DEFINE_NON_RUNTIME_PREVIEW_bool(ysql_yb_enable_ddl_savepoint_support, false,
    "If true, support for savepoints for DDL statements within a transaction block will be "
    "enabled. This flag only takes effect if ysql_yb_ddl_transaction_block_enabled is set to "
    "true.");
DEFINE_validator(ysql_yb_enable_ddl_savepoint_support,
    FLAG_REQUIRES_FLAG_VALIDATOR(ysql_yb_ddl_transaction_block_enabled));

// Wait-queues: Enabling FLAGS_refresh_waiter_timeout_ms is necessary for maintaining up-to-date
// blocking transaction(s) information at the transaction coordinator/deadlock detector. Else, with
// the current implementation, it could result in true deadlocks not being detected.
//
// For instance, refer issue https://github.com/yugabyte/yugabyte-db/issues/16286
//
// Additionally, enabling this flag serves as a fallback mechanism for deadlock detection as it
// helps maintain updated blocker(s) info at the deadlock detector. Since the feature of supporting
// transaction promotion for geo-partitioned workloads in use of wait-queues and deadlock detection
// is relatively new, it is advisable that we have the flag enabled for now. The value can be
// increased once the feature hardens and the above referred issue is resolved.
DEFINE_RUNTIME_uint64(refresh_waiter_timeout_ms, 30000,
    "The maximum amount of time a waiter transaction waits in the wait-queue "
    "before its callback is invoked. On invocation, the waiter transaction "
    "re-runs conflicts resolution and might enter the wait-queue again with "
    "updated blocker(s) information. Setting the value to 0 disables "
    "automatically re-running conflict resolution due to timeout. It follows "
    "that the waiter callback would only be invoked when a blocker txn commits/ "
    "aborts/gets promoted.");
TAG_FLAG(refresh_waiter_timeout_ms, advanced);
TAG_FLAG(refresh_waiter_timeout_ms, hidden);

#ifdef NDEBUG
constexpr bool kEnableObjectLockingForTableLocks = kEnableDdlTransactionBlocks;
#else
constexpr bool kEnableObjectLockingForTableLocks = false;
#endif
DEFINE_NON_RUNTIME_bool(enable_object_locking_for_table_locks,
    kEnableObjectLockingForTableLocks,
    "This flag enables the object lock APIs provided by tservers and masters - "
    "AcquireObject(Global)Lock, ReleaseObject(Global)Lock. These APIs are used to "
    "implement pg table locks.");
DEFINE_RUNTIME_AUTO_PG_FLAG(
    bool, enable_object_locking_infra, kLocalPersisted, false, true,
    "Auto flag that controls whether table-level object locking can be safely enabled "
    "during upgrade. Both this flag and enable_object_locking_for_table_locks "
    "must be true to enable the feature.");
DEFINE_validator(enable_object_locking_for_table_locks,
    FLAG_REQUIRES_FLAG_VALIDATOR(ysql_enable_db_catalog_version_mode),
    FLAG_REQUIRES_FLAG_VALIDATOR(ysql_yb_ddl_transaction_block_enabled),
    FLAG_REQUIRES_NONZERO_FLAG_VALIDATOR(refresh_waiter_timeout_ms));
DEFINE_validator(ysql_enable_db_catalog_version_mode,
    FLAG_REQUIRED_BY_FLAG_VALIDATOR(enable_object_locking_for_table_locks));
DEFINE_validator(ysql_yb_ddl_transaction_block_enabled,
    FLAG_DELAYED_COND_VALIDATOR(
        (!_value || FLAGS_ysql_yb_ddl_rollback_enabled),
        "ysql_yb_ddl_rollback_enabled must be enabled"),
    FLAG_REQUIRED_BY_FLAG_VALIDATOR(enable_object_locking_for_table_locks));
DEFINE_validator(refresh_waiter_timeout_ms,
    FLAG_REQUIRED_NONZERO_BY_FLAG_VALIDATOR(enable_object_locking_for_table_locks));

DEFINE_RUNTIME_PG_PREVIEW_FLAG(bool, yb_cdcsdk_stream_tables_without_primary_key, false,
    "When set to true, allows streaming of tables without primary keys for CDCSDK logical "
    "replication streams.");

namespace {

constexpr const auto kMinRpcThrottleThresholdBytes = 16;

bool RpcThrottleThresholdBytesValidator(const char* flag_name, int64 value) {
  if (value <= 0) {
    return true;
  }

  if (value < kMinRpcThrottleThresholdBytes) {
    LOG_FLAG_VALIDATION_ERROR(flag_name, value)
        << "Must be at least " << kMinRpcThrottleThresholdBytes;
    return false;
  }

  // This validation depends on the value of other flag(s): consensus_max_batch_size_bytes.
  DELAY_FLAG_VALIDATION_ON_STARTUP(flag_name);

  if (std::cmp_greater_equal(value, FLAGS_consensus_max_batch_size_bytes)) {
    LOG_FLAG_VALIDATION_ERROR(flag_name, value)
        << "Must be less than consensus_max_batch_size_bytes "
        << "(value: " << FLAGS_consensus_max_batch_size_bytes << ")";
    return false;
  }

  return true;
}

}  // namespace

DEFINE_validator(rpc_throttle_threshold_bytes, &RpcThrottleThresholdBytesValidator);

DEFINE_RUNTIME_AUTO_bool(enable_xcluster_auto_flag_validation, kLocalPersisted, false, true,
    "Enables validation of AutoFlags between the xcluster universes");

// If the cluster is upgraded to a release where --ysql_yb_ddl_rollback_enabled is true by default,
// we do not want to have DDL transaction metadata to be stored persistently before the finalization
// phase of cluster upgrade completes. This is because bad things can happen if we have stored
// transaction metadata persistently and later rollback the upgrade. This auto flag is used for
// this purpose. We can only start to store DDL transaction metadata persistently when both
// --ysql_enable_ddl_atomicity_infra=true and --ysql_yb_ddl_rollback_enabled=true.
DEFINE_RUNTIME_AUTO_PG_FLAG(bool, yb_enable_ddl_atomicity_infra, kLocalPersisted, false, true,
    "Enables YSQL DDL atomicity");

DEFINE_NON_RUNTIME_string(certs_for_cdc_dir, "",
    "The parent directory of where all certificates for xCluster source universes will "
    "be stored, for when the source and target universes use different certificates. "
    "Place the certificates for each source universe in "
    "<certs_for_cdc_dir>/<source_cluster_uuid>/*.");

DEFINE_NON_RUNTIME_int32(cdc_read_rpc_timeout_ms, 30 * 1000,
    "Timeout used for CDC read rpc calls.  Reads normally occur cross-cluster.");
TAG_FLAG(cdc_read_rpc_timeout_ms, advanced);

// The flag is used both in DocRowwiseIterator and at PG side (needed for Cost Based Optimizer).
// But it is not tagged with kPg as it would be used both for YSQL and YCQL (refer to GH #22371).
DEFINE_NON_RUNTIME_bool(use_fast_backward_scan, true,
    "Use backward scan optimization to build a row in the reverse order for YSQL.");

DEFINE_RUNTIME_bool(ysql_enable_auto_analyze_service, false,
    "Enable the Auto Analyze service which automatically triggers ANALYZE to "
    "update table statistics for tables which have changed more than a "
    "configurable threshold.");
TAG_FLAG(ysql_enable_auto_analyze_service, experimental);

DEFINE_RUNTIME_AUTO_bool(cdcsdk_enable_dynamic_table_addition_with_table_cleanup,
    kLocalPersisted,
    false,
    true,
    "This flag needs to be true in order to support addition of dynamic tables "
    "along with removal of not of interest/expired tables from a CDCSDK "
    "stream.");
TAG_FLAG(cdcsdk_enable_dynamic_table_addition_with_table_cleanup, advanced);

DEFINE_RUNTIME_AUTO_PG_FLAG(bool, yb_update_optimization_infra, kLocalPersisted, false, true,
    "Enables optimizations of YSQL UPDATE queries. This includes "
    "(but not limited to) skipping redundant secondary index updates "
    "and redundant constraint checks.");

DEFINE_RUNTIME_PG_FLAG(bool, yb_skip_redundant_update_ops, true,
    "Enables the comparison of old and new values of columns specified in the "
    "SET clause of YSQL UPDATE queries to skip redundant secondary index "
    "updates and redundant constraint checks.");
TAG_FLAG(ysql_yb_skip_redundant_update_ops, advanced);

DEFINE_RUNTIME_bool(cdc_disable_sending_composite_values, true,
    "When this flag is set to true, cdc service will send null values for columns "
    "of composite types");

DEFINE_RUNTIME_int32(timestamp_history_retention_interval_sec, 900,
    "The time interval in seconds to retain DocDB history for. Point-in-time "
    "reads at a hybrid time further than this in the past might not be allowed "
    "after a compaction. Set this to be higher than the expected maximum duration "
    "of any single transaction in your application.");

DEFINE_test_flag(bool, ysql_yb_enable_listen_notify, false, "Enable YSQL LISTEN/NOTIFY.");

namespace yb {

void InitCommonFlags() {
  // Note! Autoflags are in non-promoted state (are set to the initial value) during execution of
  // this function. Be very careful in manipulations with such flags.
}

} // namespace yb
