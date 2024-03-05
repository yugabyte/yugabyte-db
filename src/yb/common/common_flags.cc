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

#include "yb/common/common_flags.h"
#include "yb/util/flags.h"
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

DEFINE_NON_RUNTIME_bool(
    enable_pg_savepoints, true,
    "Set to false to disable savepoints in YugaByte PostgreSQL API.");
TAG_FLAG(enable_pg_savepoints, stable);
TAG_FLAG(enable_pg_savepoints, advanced);

DEFINE_RUNTIME_AUTO_bool(enable_automatic_tablet_splitting, kLocalPersisted, false, true,
    "If false, disables automatic tablet splitting driven from the yb-master side.");

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

DEFINE_RUNTIME_PG_PREVIEW_FLAG(bool, ddl_rollback_enabled, false,
    "If true, upon failure of a YSQL DDL transaction that affects the DocDB syscatalog, the "
    "YB-Master will rollback the changes made to the DocDB syscatalog.");

DEFINE_NON_RUNTIME_PREVIEW_bool(ysql_enable_db_catalog_version_mode, false,
    "Enable the per database catalog version mode, a DDL statement that only "
    "affects the current database will only increment catalog version for "
    "the current database.");

DEFINE_RUNTIME_uint32(wait_for_ysql_backends_catalog_version_client_master_rpc_margin_ms, 5000,
    "For a WaitForYsqlBackendsCatalogVersion client-to-master RPC, the amount of time to reserve"
    " out of the RPC timeout to respond back to client. If margin is zero, client will determine"
    " timeout without receiving response from master. Margin should be set high enough to cover"
    " processing and RPC time for the response. It should be lower than"
    " wait_for_ysql_backends_catalog_version_client_master_rpc_timeout_ms.");
TAG_FLAG(wait_for_ysql_backends_catalog_version_client_master_rpc_margin_ms, advanced);

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

namespace {

constexpr const auto kMinRpcThrottleThresholdBytes = 16;

void RpcThrottleThresholdBytesValidator() {
  if (FLAGS_rpc_throttle_threshold_bytes <= 0) {
    return;
  }

  if (FLAGS_rpc_throttle_threshold_bytes < kMinRpcThrottleThresholdBytes) {
    LOG(FATAL) << "Flag validation failed. rpc_throttle_threshold_bytes (value: "
               << FLAGS_rpc_throttle_threshold_bytes << ") must be at least "
               << kMinRpcThrottleThresholdBytes;
  }

  if (yb::std_util::cmp_greater_equal(
          FLAGS_rpc_throttle_threshold_bytes, FLAGS_consensus_max_batch_size_bytes)) {
    LOG(FATAL) << "Flag validation failed. rpc_throttle_threshold_bytes (value: "
               << FLAGS_rpc_throttle_threshold_bytes
               << ") must be less than consensus_max_batch_size_bytes "
               << "(value: " << FLAGS_consensus_max_batch_size_bytes << ")";
  }
}

}  // namespace

// Normally we would have used DEFINE_validator. But this validation depends on the value of another
// flag (consensus_max_batch_size_bytes). On process startup flag validations are run as each flag
// gets parsed from the command line parameter. So this would impose a restriction on the user to
// pass the flags in a particular obscure order via command line. YBA has no guarantees on the order
// it uses as well. So, instead we use a Callback with LOG(FATAL) since at startup Callbacks are run
// after all the flags have been parsed.
REGISTER_CALLBACK(rpc_throttle_threshold_bytes, "RpcThrottleThresholdBytesValidator",
    &RpcThrottleThresholdBytesValidator);

DEFINE_RUNTIME_AUTO_bool(enable_xcluster_auto_flag_validation, kLocalPersisted, false, true,
    "Enables validation of AutoFlags between the xcluster universes");

namespace yb {

void InitCommonFlags() {
  // Note! Autoflags are in non-promoted state (are set to the initial value) during execution of
  // this function. Be very careful in manipulations with such flags.
}

} // namespace yb
