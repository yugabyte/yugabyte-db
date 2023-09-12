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

#include "yb/client/client.h"
#include "yb/client/client-test-util.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"
#include "yb/integration-tests/xcluster/xcluster_ysql_test_base.h"
#include "yb/master/master.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/integration-tests/xcluster/xcluster_test_base.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/backoff_waiter.h"
#include "yb/client/snapshot_test_util.h"
#include "yb/util/file_util.h"

using std::string;
using namespace std::chrono_literals;

namespace yb {
using OK = Status::OK;

const int kMasterCount = 3;
const int kTServerCount = 3;
const int kTabletCount = 3;
const string kTableName = "test_table";
constexpr auto kNumRecordsPerBatch = 10;

YB_DEFINE_ENUM(ReplicationDirection, (ProducerToConsumer)(ConsumerToProducer))

class XClusterDRTest : public XClusterYsqlTestBase {
  typedef XClusterYsqlTestBase super;

 public:
  void SetUp() override {
    YB_SKIP_TEST_IN_TSAN();

    super::SetUp();
    MiniClusterOptions opts;
    opts.num_masters = kMasterCount;
    opts.num_tablet_servers = kTServerCount;
    ASSERT_OK(InitClusters(opts));

    auto producer_cluster_future = std::async(std::launch::async, [&] {
      auto table_name = ASSERT_RESULT(CreateYsqlTable(&producer_cluster_, namespace_name));
      ASSERT_OK(producer_client()->OpenTable(table_name, &producer_table_));
    });

    producer_cluster_future.get();

    producer_snapshot_util_.SetProxy(&producer_client()->proxy_cache());
    producer_snapshot_util_.SetCluster(producer_cluster());
    consumer_snapshot_util_.SetProxy(&consumer_client()->proxy_cache());
    consumer_snapshot_util_.SetCluster(consumer_cluster());

    SetReplicationDirection(ReplicationDirection::ProducerToConsumer);
  }

  Result<client::YBTableName> CreateYsqlTable(Cluster* cluster, const std::string& namespace_name) {
    return super::CreateYsqlTable(
        cluster, namespace_name, {} /* schema_name */, kTableName, {} /*tablegroup_name*/,
        kTabletCount);
  }

  Status DropYsqlTable(Cluster* cluster, const std::string& namespace_name) {
    return super::DropYsqlTable(cluster, namespace_name, "" /* schema_name */, kTableName);
  }

  void WriteBatchOnSource() {
    WriteWorkload(
        (*source_table_)->name(), written_rows_count_, written_rows_count_ + kNumRecordsPerBatch,
        source_cluster_);
    written_rows_count_ += kNumRecordsPerBatch;
  }

  Status ValidateBatchCountOnTarget(int expected_num_batches) {
    return ValidateRows(
        (*target_table_)->name(), expected_num_batches * kNumRecordsPerBatch, target_cluster_);
  }

  Status WaitForTargetRowsToMatchSource() {
    auto& table_name = (*target_table_)->name();
    RETURN_NOT_OK(WaitForRowCount(table_name, written_rows_count_, target_cluster_));

    return ValidateRows(table_name, written_rows_count_, target_cluster_);
  }

  void SetReplicationDirection(ReplicationDirection replication_direction) {
    if (replication_direction == ReplicationDirection::ProducerToConsumer) {
      source_tables_for_bootstrap_ = {producer_table_};
      source_table_ = &producer_table_;
      source_cluster_ = &producer_cluster_;
      source_snapshot_util_ = &producer_snapshot_util_;
      target_table_ = &consumer_table_;
      target_cluster_ = &consumer_cluster_;
      target_snapshot_util_ = &consumer_snapshot_util_;
    } else {
      source_tables_for_bootstrap_ = {consumer_table_};
      source_table_ = &consumer_table_;
      source_cluster_ = &consumer_cluster_;
      source_snapshot_util_ = &consumer_snapshot_util_;
      target_table_ = &producer_table_;
      target_cluster_ = &producer_cluster_;
      target_snapshot_util_ = &producer_snapshot_util_;
    }
    source_client_ = source_cluster_->client_.get();
    target_client_ = target_cluster_->client_.get();
  }

  // Setup replication with bootstrap, set STANDBY role and wait for safe time.
  Status SetupReplication(std::vector<xrepl::StreamId> bootstrap_ids) {
    RETURN_NOT_OK(SetupUniverseReplication(
        source_cluster_->mini_cluster_.get(), target_cluster_->mini_cluster_.get(), target_client_,
        kReplicationGroupId, source_tables_for_bootstrap_, bootstrap_ids,
        {LeaderOnly::kTrue, Transactional::kTrue}));

    master::GetUniverseReplicationResponsePB resp;
    RETURN_NOT_OK(VerifyUniverseReplication(
        target_cluster_->mini_cluster_.get(), target_client_, kReplicationGroupId, &resp));

    RETURN_NOT_OK(ChangeXClusterRole(cdc::XClusterRole::STANDBY, target_cluster_));

    auto namespace_id = VERIFY_RESULT(GetNamespaceId(target_client_));
    return WaitForValidSafeTimeOnAllTServers(namespace_id, target_cluster_);
  }

  Result<std::pair<std::vector<xrepl::StreamId>, TxnSnapshotId>>
  BootstrapAndSnapshotSourceCluster() {
    auto bootstrap_ids =
        VERIFY_RESULT(BootstrapCluster(source_tables_for_bootstrap_, source_cluster_));
    auto snapshot_id = VERIFY_RESULT(source_snapshot_util_->CreateSnapshot((*source_table_)->id()));

    return std::pair(bootstrap_ids, snapshot_id);
  }

  Status RestoreSnapshotOnTarget(const TxnSnapshotId& snapshot_id) {
    // Drop and recreate tables on target cluster.
    if (target_table_->get()) {
      RETURN_NOT_OK(DropYsqlTable(target_cluster_, namespace_name));
    }
    auto table_name = VERIFY_RESULT(CreateYsqlTable(target_cluster_, namespace_name));
    RETURN_NOT_OK(target_client_->OpenTable(table_name, target_table_));

    auto source_snapshot = VERIFY_RESULT(source_snapshot_util_->ListSnapshots(
        snapshot_id, client::ListDeleted::kFalse, client::PrepareForBackup::kTrue));
    SCHECK_EQ(source_snapshot.size(), 1, IllegalState, "Expected 1 snapshot");

    auto metas = VERIFY_RESULT(target_snapshot_util_->StartImportSnapshot(source_snapshot[0]));
    SCHECK_EQ(metas.size(), 1, IllegalState, "Expected 1 snapshot metadata");
    auto& snapshot_meta = metas[0];

    auto target_snapshot_id = VERIFY_RESULT(
        target_snapshot_util_->CreateSnapshot((*target_table_)->id(), true /* imported */));

    LOG(INFO) << "Snapshot metadata: " << metas[0].DebugString();
    LOG(INFO) << "Target snapshot_id: " << target_snapshot_id;

    const string source_table_dir_name = Format("table-$0", snapshot_meta.table_ids().old_id());
    const string target_table_dir_name = Format("table-$0", snapshot_meta.table_ids().new_id());
    for (auto& tablet_pairs : snapshot_meta.tablets_ids()) {
      auto* source_tservers = source_cluster_->mini_cluster_->mini_tablet_server(0);
      SCHECK(
          source_tservers->fs_manager().LookupTablet(tablet_pairs.old_id()), IllegalState,
          "Tablet not found");
      const string source_tablet_snapshot_dir_name =
          Format("tablet-$0.snapshots", tablet_pairs.old_id());
      const string source_path = JoinPathSegments(
          source_tservers->fs_manager().GetDataRootDirs()[0], FsManager::kRocksDBDirName,
          source_table_dir_name, source_tablet_snapshot_dir_name, snapshot_id.ToString());

      const string target_tablet_snapshot_dir_name =
          Format("tablet-$0.snapshots", tablet_pairs.new_id());

      for (auto& target_tservers : target_cluster_->mini_cluster_->mini_tablet_servers()) {
        SCHECK(
            target_tservers->fs_manager().LookupTablet(tablet_pairs.new_id()), IllegalState,
            "Tablet not found");
        const string target_path = JoinPathSegments(
            target_tservers->fs_manager().GetDataRootDirs()[0], FsManager::kRocksDBDirName,
            target_table_dir_name, target_tablet_snapshot_dir_name, target_snapshot_id.ToString());

        LOG(INFO) << "Copying snapshot from " << source_path << " to " << target_path;

        RETURN_NOT_OK(CopyDirectory(
            target_tservers->fs_manager().env(), source_path, target_path, UseHardLinks::kFalse,
            CreateIfMissing::kTrue, RecursiveCopy::kTrue));
      }
    }

    RETURN_NOT_OK(target_snapshot_util_->RestoreSnapshot(target_snapshot_id));

    // Reopen the table after restore.
    return target_client_->OpenTable((*target_table_)->name(), target_table_);
  }

 protected:
  client::YBTablePtr producer_table_, consumer_table_;
  client::SnapshotTestUtil producer_snapshot_util_, consumer_snapshot_util_;

  std::vector<std::shared_ptr<client::YBTable>> source_tables_for_bootstrap_;
  client::YBTablePtr *source_table_ = nullptr, *target_table_ = nullptr;
  Cluster *source_cluster_ = nullptr, *target_cluster_ = nullptr;
  YBClient *target_client_ = nullptr, *source_client_ = nullptr;
  client::SnapshotTestUtil *source_snapshot_util_ = nullptr, *target_snapshot_util_ = nullptr;
  int written_rows_count_ = 0;
};

TEST_F(XClusterDRTest, SetupWithBackupRestore) {
  WriteBatchOnSource();

  auto bootstrap_ids =
      ASSERT_RESULT(BootstrapCluster(source_tables_for_bootstrap_, source_cluster_));
  auto producer_snapshot_id =
      ASSERT_RESULT(producer_snapshot_util_.CreateSnapshot(producer_table_->id()));

  WriteBatchOnSource();

  ASSERT_OK(RestoreSnapshotOnTarget(producer_snapshot_id));

  // Only the first batch written before the snapshot should be visible.
  ASSERT_OK(ValidateBatchCountOnTarget(1));

  ASSERT_OK(SetupReplication(bootstrap_ids));

  // All rows should be visible.
  ASSERT_OK(WaitForTargetRowsToMatchSource());
}

TEST_F(XClusterDRTest, Failover) {
  // 1. Setup replication with bootstrap and restore.
  auto [bootstrap_ids, producer_snapshot_id] = ASSERT_RESULT(BootstrapAndSnapshotSourceCluster());

  ASSERT_OK(RestoreSnapshotOnTarget(producer_snapshot_id));
  ASSERT_OK(SetupReplication(bootstrap_ids));

  // 2. Validate replication.
  WriteBatchOnSource();
  ASSERT_OK(WaitForTargetRowsToMatchSource());

  // 3. Disable replication.
  ASSERT_OK(ToggleUniverseReplication(
      target_cluster_->mini_cluster_.get(), target_client_, kReplicationGroupId,
      false /* is_enabled */));

  // 4. Swap the source and target clusters.
  SetReplicationDirection(ReplicationDirection::ConsumerToProducer);

  // 5. Bootstrap the new source cluster and set to ACTIVE,
  auto [consumer_bootstrap_ids, consumer_snapshot_id] =
      ASSERT_RESULT(BootstrapAndSnapshotSourceCluster());

  ASSERT_OK(ChangeXClusterRole(cdc::XClusterRole::ACTIVE, source_cluster_));
  ASSERT_OK(WaitForRoleChangeToPropogateToAllTServers(cdc::XClusterRole::ACTIVE, source_cluster_));

  // 6. Write some more data on new source.
  WriteBatchOnSource();

  // 7. Recreate the target(old source) with the snapshot.
  ASSERT_OK(RestoreSnapshotOnTarget(consumer_snapshot_id));

  // 8. Verify only the first batch that was in the snapshot is visible.
  ASSERT_OK(ValidateBatchCountOnTarget(1));

  // 9. Setup replication and verify all rows are visible.
  ASSERT_OK(SetupReplication(consumer_bootstrap_ids));
  ASSERT_OK(WaitForTargetRowsToMatchSource());
}

}  // namespace yb
