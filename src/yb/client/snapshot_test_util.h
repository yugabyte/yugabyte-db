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

#ifndef YB_CLIENT_SNAPSHOT_TEST_UTIL_H
#define YB_CLIENT_SNAPSHOT_TEST_UTIL_H

#include "yb/client/txn-test-base.h"
#include "yb/common/snapshot.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/master/master_backup.proxy.h"
#include "yb/rpc/proxy.h"
#include "yb/util/net/net_fwd.h"
#include "yb/util/net/net_util.h"
#include "yb/util/tsan_util.h"

using namespace std::literals;

namespace yb {
namespace client {

using Snapshots = google::protobuf::RepeatedPtrField<master::SnapshotInfoPB>;
constexpr auto kWaitTimeout = std::chrono::seconds(15);

using Schedules = google::protobuf::RepeatedPtrField<master::SnapshotScheduleInfoPB>;
using ImportedSnapshotData = google::protobuf::RepeatedPtrField<
    master::ImportSnapshotMetaResponsePB::TableMetaPB>;
constexpr auto kSnapshotInterval = 10s * kTimeMultiplier;
constexpr auto kSnapshotRetention = 20h;

YB_STRONGLY_TYPED_BOOL(WaitSnapshot);
YB_STRONGLY_TYPED_BOOL(ListDeleted);
YB_STRONGLY_TYPED_BOOL(PrepareForBackup);

class SnapshotTestUtil {
 public:
  SnapshotTestUtil() = default;
  ~SnapshotTestUtil() = default;

  Result<std::unique_ptr<YBClient>> InitWithCluster(MiniClusterBase* cluster) {
    SetCluster(cluster);
    auto result = VERIFY_RESULT(cluster->CreateClient());
    SetProxy(&result->proxy_cache());
    return result;
  }

  void SetProxy(rpc::ProxyCache* proxy_cache) {
    proxy_cache_ = proxy_cache;
  }
  void SetCluster(MiniClusterBase* cluster) { cluster_ = cluster; }

  Result<master::MasterBackupProxy> MakeBackupServiceProxy() {
    return master::MasterBackupProxy(
        proxy_cache_, VERIFY_RESULT(cluster_->GetLeaderMasterBoundRpcAddr()));
  }

  Result<master::SysSnapshotEntryPB::State> SnapshotState(const TxnSnapshotId& snapshot_id);

  Result<bool> IsSnapshotDone(const TxnSnapshotId& snapshot_id);
  Result<Snapshots> ListSnapshots(
      const TxnSnapshotId& snapshot_id = TxnSnapshotId::Nil(),
      ListDeleted list_deleted = ListDeleted::kTrue,
      PrepareForBackup prepare_for_backup = PrepareForBackup::kFalse);
  Status VerifySnapshot(
      const TxnSnapshotId& snapshot_id, master::SysSnapshotEntryPB::State state,
      size_t expected_num_tablets, size_t expected_num_namespaces = 1,
      size_t expected_num_tables = 1);
  Status WaitSnapshotInState(
      const TxnSnapshotId& snapshot_id, master::SysSnapshotEntryPB::State state,
      MonoDelta duration = kWaitTimeout);
  Status WaitSnapshotDone(
      const TxnSnapshotId& snapshot_id, MonoDelta duration = kWaitTimeout);

  Result<TxnSnapshotRestorationId> StartRestoration(
      const TxnSnapshotId& snapshot_id, HybridTime restore_at = HybridTime());
  Result<bool> IsRestorationDone(const TxnSnapshotRestorationId& restoration_id);
  Status RestoreSnapshot(
      const TxnSnapshotId& snapshot_id, HybridTime restore_at = HybridTime());
  Result<TxnSnapshotId> StartSnapshot(const YBTableName& table_name);
  // Set for_import to true if this snapshots is imported from another DB.
  Result<TxnSnapshotId> StartSnapshot(const TableId& table_id, bool imported = false);
  Result<TxnSnapshotId> StartSnapshot(const TableHandle& table);
  Result<TxnSnapshotId> CreateSnapshot(const TableId& table_id, bool imported = false);
  Result<TxnSnapshotId> CreateSnapshot(const TableHandle& table);
  Status DeleteSnapshot(const TxnSnapshotId& snapshot_id);
  Status WaitAllSnapshotsDeleted();

  Result<ImportedSnapshotData> StartImportSnapshot(const master::SnapshotInfoPB& snapshot);
  Status WaitAllSnapshotsCleaned();

  Result<SnapshotScheduleId> CreateSchedule(
      const TableHandle& table, YQLDatabase db_type, const std::string& db_name,
      MonoDelta interval = kSnapshotInterval, MonoDelta retention = kSnapshotRetention);
  Result<SnapshotScheduleId> CreateSchedule(
      const TableHandle& table, YQLDatabase db_type, const std::string& db_name,
      WaitSnapshot wait_snapshot, MonoDelta interval = kSnapshotInterval,
      MonoDelta retention = kSnapshotRetention);
  Result<SnapshotScheduleId> CreateSchedule(
      const YBTablePtr table, YQLDatabase db_type, const std::string& db_name,
      WaitSnapshot wait_snapshot, MonoDelta interval = kSnapshotInterval,
      MonoDelta retention = kSnapshotRetention);

  Result<Schedules> ListSchedules(const SnapshotScheduleId& id = SnapshotScheduleId::Nil());

  Result<TxnSnapshotId> PickSuitableSnapshot(
      const SnapshotScheduleId& schedule_id, HybridTime hybrid_time);

  Status WaitScheduleSnapshot(
      const SnapshotScheduleId& schedule_id, HybridTime min_hybrid_time);

  Status WaitScheduleSnapshot(
      const SnapshotScheduleId& schedule_id, int max_snapshots = 1,
      HybridTime min_hybrid_time = HybridTime::kMin);

 private:
  template <class F>
  Result<TxnSnapshotId> DoStartSnapshot(const F& fill_tables);

  rpc::ProxyCache* proxy_cache_;
  MiniClusterBase* cluster_;
};

} // namespace client
} // namespace yb

#endif  // YB_CLIENT_SNAPSHOT_TEST_UTIL_H
