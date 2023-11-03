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

#pragma once

#include "yb/common/hybrid_time.h"
#include "yb/common/snapshot.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/master/state_with_tablets.h"

#include "yb/util/async_task_util.h"
#include "yb/util/flags.h"
#include "yb/util/tostring.h"

DECLARE_int64(max_concurrent_restoration_rpcs);
DECLARE_int64(max_concurrent_restoration_rpcs_per_tserver);

namespace yb {
namespace master {

YB_DEFINE_ENUM(RestorePhase, (kInitial)(kPostSysCatalogLoad));
YB_STRONGLY_TYPED_BOOL(IsSysCatalogRestored);

struct TabletRestoreOperation {
  TabletId tablet_id;
  TxnSnapshotRestorationId restoration_id;
  TxnSnapshotId snapshot_id;
  HybridTime restore_at;
  bool sys_catalog_restore_needed;
  bool is_tablet_part_of_snapshot;
  std::optional<int64_t> db_oid;
  SnapshotScheduleId schedule_id;
};

using TabletRestoreOperations = std::vector<TabletRestoreOperation>;

class RestorationState : public StateWithTablets {
 public:
  RestorationState(
      SnapshotCoordinatorContext* context, const TxnSnapshotRestorationId& restoration_id,
      SnapshotState* snapshot, HybridTime restore_at, IsSysCatalogRestored is_sys_catalog_restored,
      uint64_t throttle_limit = std::numeric_limits<int>::max());

  RestorationState(
      SnapshotCoordinatorContext* context, const TxnSnapshotRestorationId& restoration_id,
      const SysRestorationEntryPB& entry);

  const TxnSnapshotRestorationId& restoration_id() const {
    return restoration_id_;
  }

  const TxnSnapshotId& snapshot_id() const {
    return snapshot_id_;
  }

  HybridTime complete_time() const {
    return complete_time_;
  }

  const SnapshotScheduleId& schedule_id() const {
    return schedule_id_;
  }

  HybridTime restore_at() const {
    return restore_at_;
  }

  int64_t version() const {
    return version_;
  }

  void set_complete_time(HybridTime value) {
    complete_time_ = value;
  }

  void SetSysCatalogRestored(IsSysCatalogRestored is_sys_catalog_restored) {
    is_sys_catalog_restored_ = is_sys_catalog_restored;
  }

  void SetLeaderTerm(int64_t leader_term) {
    leader_term_ = leader_term;
  }

  int64_t GetLeaderTerm() {
    return leader_term_;
  }

  IsSysCatalogRestored IsSysCatalogRestorationDone() {
    return is_sys_catalog_restored_;
  }

  AsyncTaskThrottler& Throttler() {
    return throttler_;
  }

  const std::unordered_map<std::string, SysRowEntryType>& MasterMetadata() {
    return master_metadata_;
  }

  std::unordered_map<std::string, SysRowEntryType>* MutableMasterMetadata() {
    return &master_metadata_;
  }

  bool ShouldUpdate(const RestorationState& other) const {
    // Backward compatibility mode
    int64_t other_version = other.version() == 0 ? version() + 1 : other.version();
    // If we have several updates for single restore, they are loaded in chronological order.
    // So latest update should be picked.
    return version() < other_version;
  }

  std::string ToString() const {
    return YB_CLASS_TO_STRING(restoration_id, snapshot_id);
  }

  Status ToPB(RestorationInfoPB* out);

  void PrepareOperations(
      TabletRestoreOperations* operations, const std::unordered_set<TabletId>& snapshot_tablets,
      std::optional<int64_t> db_oid);

  Status Abort();

  Status StoreToWriteBatch(docdb::KeyValueWriteBatchPB* write_batch);

  Status StoreToKeyValuePair(docdb::KeyValuePairPB* pair);

 private:
  std::optional<SysSnapshotEntryPB::State> GetTerminalStateForStatus(const Status& status) override;
  Status ToEntryPB(ForClient for_client, SysRestorationEntryPB* out);

  SysSnapshotEntryPB::State MigrateInitialStateIfNeeded(SysSnapshotEntryPB::State initial_state);

  const TxnSnapshotRestorationId restoration_id_;
  TxnSnapshotId snapshot_id_ = TxnSnapshotId::Nil();
  SnapshotScheduleId schedule_id_ = SnapshotScheduleId::Nil();
  HybridTime complete_time_;
  IsSysCatalogRestored is_sys_catalog_restored_;
  HybridTime restore_at_;
  AsyncTaskThrottler throttler_;
  int64_t version_;
  int64_t leader_term_ = -1; // Set only after the leader has loaded sys catalog.
  std::unordered_map<std::string, SysRowEntryType> master_metadata_;
};

} // namespace master
} // namespace yb
