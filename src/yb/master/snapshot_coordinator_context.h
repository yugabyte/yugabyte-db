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

#ifndef YB_MASTER_SNAPSHOT_COORDINATOR_CONTEXT_H
#define YB_MASTER_SNAPSHOT_COORDINATOR_CONTEXT_H

#include "yb/common/common_fwd.h"
#include "yb/common/entity_ids.h"
#include "yb/common/snapshot.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/master/master_fwd.h"
#include "yb/master/master.pb.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/server/server_fwd.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/tserver/tserver_fwd.h"

#include "yb/util/result.h"

namespace yb {
namespace master {

using TabletSnapshotOperationCallback =
    std::function<void(Result<const tserver::TabletSnapshotOpResponsePB&>)>;

// Context class for MasterSnapshotCoordinator.
class SnapshotCoordinatorContext {
 public:
  // Return tablet infos for specified tablet ids.
  // The returned vector is always of the same length as the input vector,
  // with null entries returned for unknown tablet ids.
  virtual TabletInfos GetTabletInfos(const std::vector<TabletId>& id) = 0;

  virtual void SendCreateTabletSnapshotRequest(
      const scoped_refptr<TabletInfo>& tablet, const std::string& snapshot_id,
      const SnapshotScheduleId& schedule_id, HybridTime snapshot_hybrid_time,
      TabletSnapshotOperationCallback callback) = 0;

  virtual void SendRestoreTabletSnapshotRequest(
      const scoped_refptr<TabletInfo>& tablet, const std::string& snapshot_id,
      HybridTime restore_at, TabletSnapshotOperationCallback callback) = 0;

  virtual void SendDeleteTabletSnapshotRequest(
      const scoped_refptr<TabletInfo>& tablet, const std::string& snapshot_id,
      TabletSnapshotOperationCallback callback) = 0;

  virtual Result<SysRowEntries> CollectEntries(
      const google::protobuf::RepeatedPtrField<TableIdentifierPB>& tables,
      bool add_indexes,
      bool include_parent_colocated_table) = 0;

  virtual const Schema& schema() = 0;

  virtual void Submit(std::unique_ptr<tablet::Operation> operation) = 0;

  virtual rpc::Scheduler& Scheduler() = 0;

  virtual bool IsLeader() = 0;

  virtual server::Clock* Clock() = 0;

  virtual ~SnapshotCoordinatorContext() = default;
};

Result<docdb::KeyBytes> EncodedKey(
    SysRowEntry::Type type, const Slice& id, SnapshotCoordinatorContext* context);

} // namespace master
} // namespace yb

#endif  // YB_MASTER_SNAPSHOT_COORDINATOR_CONTEXT_H
