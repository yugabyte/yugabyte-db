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

#include "yb/master/restoration_state.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/snapshot_coordinator_context.h"
#include "yb/master/snapshot_state.h"

#include "yb/tserver/tserver_error.h"

namespace yb {
namespace master {

RestorationState::RestorationState(
    SnapshotCoordinatorContext* context, const TxnSnapshotRestorationId& restoration_id,
    SnapshotState* snapshot)
    : StateWithTablets(context, SysSnapshotEntryPB::RESTORING),
      restoration_id_(restoration_id), snapshot_id_(snapshot->id()) {
  InitTabletIds(snapshot->TabletIdsInState(SysSnapshotEntryPB::COMPLETE));
}

CHECKED_STATUS RestorationState::ToPB(RestorationInfoPB* out) {
  out->set_id(restoration_id_.data(), restoration_id_.size());
  auto& entry = *out->mutable_entry();
  entry.set_snapshot_id(snapshot_id_.data(), snapshot_id_.size());

  entry.set_state(VERIFY_RESULT(AggregatedState()));

  if (complete_time_) {
    entry.set_complete_time_ht(complete_time_.ToUint64());
  }

  TabletsToPB(entry.mutable_tablet_restorations());

  return Status::OK();
}

TabletInfos RestorationState::PrepareOperations() {
  std::vector<TabletId> tablet_ids;
  DoPrepareOperations([&tablet_ids](const TabletData& data) {
    tablet_ids.push_back(data.id);
  });
  return context().GetTabletInfos(tablet_ids);
}

bool RestorationState::IsTerminalFailure(const Status& status) {
  return status.IsAborted() ||
         tserver::TabletServerError(status) == tserver::TabletServerErrorPB::INVALID_SNAPSHOT;
}

} // namespace master
} // namespace yb
