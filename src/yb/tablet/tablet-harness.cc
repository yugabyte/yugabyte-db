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

#include "yb/tablet/tablet-harness.h"

#include "yb/qlexpr/index.h"
#include "yb/dockv/partition.h"

#include "yb/consensus/log_anchor_registry.h"

#include "yb/server/logical_clock.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"

#include "yb/util/result.h"

using std::vector;

namespace yb {
namespace tablet {

std::pair<dockv::PartitionSchema, dockv::Partition> CreateDefaultPartition(const Schema& schema) {
  // Create a default partition schema.
  dockv::PartitionSchema partition_schema;
  CHECK_OK(dockv::PartitionSchema::FromPB(PartitionSchemaPB(), schema, &partition_schema));

  // Create the tablet partitions.
  vector<dockv::Partition> partitions;
  CHECK_OK(partition_schema.CreatePartitions({}, schema, &partitions));
  CHECK_EQ(1, partitions.size());
  return std::make_pair(partition_schema, partitions[0]);
}

Status TabletHarness::Create(bool first_time) {
  std::pair<dockv::PartitionSchema, dockv::Partition> partition(CreateDefaultPartition(schema_));

  // Build the Tablet
  fs_manager_.reset(new FsManager(options_.env, options_.root_dir, "tserver_test"));
  if (first_time) {
    RETURN_NOT_OK(fs_manager_->CreateInitialFileSystemLayout());
  }
  RETURN_NOT_OK(fs_manager_->CheckAndOpenFileSystemRoots());

  auto table_info = std::make_shared<TableInfo>(
      "test-tablet", Primary::kTrue, "YBTableTest", "test", "YBTableTest", options_.table_type,
      schema_, qlexpr::IndexMap(), boost::none, 0 /* schema_version */, partition.first,
      "" /* pg_table_id */, tablet::SkipTableTombstoneCheck::kFalse);
  auto metadata = VERIFY_RESULT(RaftGroupMetadata::TEST_LoadOrCreate(RaftGroupMetadataData {
    .fs_manager = fs_manager_.get(),
    .table_info = table_info,
    .raft_group_id = options_.tablet_id,
    .partition = partition.second,
    .tablet_data_state = TABLET_DATA_READY,
    .snapshot_schedules = {},
    .hosted_services = {},
  }));
  if (options_.enable_metrics) {
    metrics_registry_.reset(new MetricRegistry());
  }

  clock_ = server::LogicalClock::CreateStartingAt(HybridTime::kInitial);
  tablet_.reset(new Tablet(MakeTabletInitData(metadata)));
  return Status::OK();
}

Status TabletHarness::Open() {
  RETURN_NOT_OK(tablet_->Open());
  tablet_->MarkFinishedBootstrapping();
  return tablet_->EnableCompactions(/* blocking_rocksdb_shutdown_start_ops_pause = */ nullptr);
}

Result<TabletPtr> TabletHarness::OpenTablet(const TabletId& tablet_id) {
  auto metadata = VERIFY_RESULT(RaftGroupMetadata::Load(fs_manager_.get(), tablet_id));
  std::shared_ptr<Tablet> tablet(new Tablet(MakeTabletInitData(metadata)));
  RETURN_NOT_OK(tablet->Open());
  tablet->MarkFinishedBootstrapping();
  RETURN_NOT_OK(
      tablet->EnableCompactions(/* blocking_rocksdb_shutdown_start_ops_pause = */ nullptr));
  return tablet;
}

TabletInitData TabletHarness::MakeTabletInitData(const RaftGroupMetadataPtr& metadata) {
  return TabletInitData {
      .metadata = metadata,
      .client_future = std::shared_future<client::YBClient*>(),
      .clock = clock_,
      .parent_mem_tracker = std::shared_ptr<MemTracker>(),
      .block_based_table_mem_tracker = std::shared_ptr<MemTracker>(),
      .metric_registry = metrics_registry_.get(),
      .log_anchor_registry = new log::LogAnchorRegistry(),
      .tablet_options = TabletOptions(),
      .log_prefix_suffix = std::string(),
      .transaction_participant_context = nullptr,
      .local_tablet_filter = client::LocalTabletFilter(),
      .transaction_coordinator_context = nullptr,
      .txns_enabled = TransactionsEnabled::kFalse,
      .is_sys_catalog = IsSysCatalogTablet::kFalse,
      .snapshot_coordinator = nullptr,
      .tablet_splitter = nullptr,
      .allowed_history_cutoff_provider = {},
      .transaction_manager_provider = nullptr,
      .full_compaction_pool = nullptr,
      .admin_triggered_compaction_pool = nullptr,
      .post_split_compaction_added = nullptr,
      .metadata_cache = nullptr,
    };
}

} // namespace tablet
} // namespace yb
