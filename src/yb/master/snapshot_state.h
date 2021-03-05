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

#ifndef YB_MASTER_SNAPSHOT_STATE_H
#define YB_MASTER_SNAPSHOT_STATE_H

#include "yb/common/hybrid_time.h"
#include "yb/common/snapshot.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/master/state_with_tablets.h"

#include "yb/tserver/tserver_fwd.h"

namespace yb {
namespace master {

YB_STRONGLY_TYPED_BOOL(ForClient);

struct TabletSnapshotOperation {
  TabletId tablet_id;
  TxnSnapshotId snapshot_id;
  SysSnapshotEntryPB::State state;
  HybridTime snapshot_hybrid_time;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(tablet_id, snapshot_id, state, snapshot_hybrid_time);
  }
};

using TabletSnapshotOperations = std::vector<TabletSnapshotOperation>;

class SnapshotState : public StateWithTablets {
 public:
  SnapshotState(
      SnapshotCoordinatorContext* context, const TxnSnapshotId& id,
      const tserver::TabletSnapshotOpRequestPB& request);

  SnapshotState(
      SnapshotCoordinatorContext* context, const TxnSnapshotId& id,
      const SysSnapshotEntryPB& entry);

  const TxnSnapshotId& id() const {
    return id_;
  }

  HybridTime snapshot_hybrid_time() const {
    return snapshot_hybrid_time_;
  }

  const SnapshotScheduleId& schedule_id() const {
    return schedule_id_;
  }

  int version() const {
    return version_;
  }

  std::string ToString() const;
  CHECKED_STATUS ToPB(SnapshotInfoPB* out);
  CHECKED_STATUS ToEntryPB(SysSnapshotEntryPB* out, ForClient for_client);
  CHECKED_STATUS StoreToWriteBatch(docdb::KeyValueWriteBatchPB* out);
  CHECKED_STATUS CheckCanDelete();
  void PrepareOperations(TabletSnapshotOperations* out);
  void SetVersion(int value);
  bool NeedCleanup() const;
  bool ShouldUpdate(const SnapshotState& other) const;

 private:
  bool IsTerminalFailure(const Status& status) override;

  TxnSnapshotId id_;
  HybridTime snapshot_hybrid_time_;
  SysRowEntries entries_;
  // When snapshot is taken as a part of snapshot schedule schedule_id_ will contain this
  // schedule id. Otherwise it will be nil.
  SnapshotScheduleId schedule_id_;
  int version_;
};

Result<docdb::KeyBytes> EncodedSnapshotKey(
    const TxnSnapshotId& id, SnapshotCoordinatorContext* context);

} // namespace master
} // namespace yb

#endif  // YB_MASTER_SNAPSHOT_STATE_H
