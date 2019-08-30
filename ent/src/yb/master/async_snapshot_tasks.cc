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

#include "yb/master/async_snapshot_tasks.h"

#include "yb/common/wire_protocol.h"

#include "yb/master/master.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/catalog_manager.h"

#include "yb/rpc/messenger.h"

#include "yb/tserver/backup.proxy.h"

#include "yb/util/flag_tags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"

namespace yb {
namespace master {

using std::string;
using tserver::TabletServerErrorPB;

////////////////////////////////////////////////////////////
// AsyncTabletSnapshotOp
////////////////////////////////////////////////////////////

AsyncTabletSnapshotOp::AsyncTabletSnapshotOp(Master *master,
                                             ThreadPool* callback_pool,
                                             const scoped_refptr<TabletInfo>& tablet,
                                             const string& snapshot_id,
                                             tserver::TabletSnapshotOpRequestPB::Operation op)
  : enterprise::RetryingTSRpcTask(master,
                                  callback_pool,
                                  new PickLeaderReplica(tablet),
                                  tablet->table().get()),
    tablet_(tablet),
    snapshot_id_(snapshot_id),
    operation_(op) {
}

string AsyncTabletSnapshotOp::description() const {
  return Format("$0 Tablet Snapshot Operation $1 RPC", *tablet_, operation_);
}

TabletId AsyncTabletSnapshotOp::tablet_id() const {
  return tablet_->tablet_id();
}

TabletServerId AsyncTabletSnapshotOp::permanent_uuid() const {
  return target_ts_desc_ != nullptr ? target_ts_desc_->permanent_uuid() : "";
}

void AsyncTabletSnapshotOp::HandleResponse(int attempt) {
  server::UpdateClock(resp_, master_->clock());

  if (resp_.has_error()) {
    Status status = StatusFromPB(resp_.error().status());

    // Do not retry on a fatal error.
    switch (resp_.error().code()) {
      case TabletServerErrorPB::TABLET_NOT_FOUND:
        LOG(WARNING) << "TS " << permanent_uuid() << ": snapshot failed for tablet "
                     << tablet_->ToString() << " no further retry: " << status.ToString();
        TransitionToTerminalState(MonitoredTaskState::kRunning, MonitoredTaskState::kComplete);
        break;
      default:
        LOG(WARNING) << "TS " << permanent_uuid() << ": snapshot failed for tablet "
                     << tablet_->ToString() << ": " << status.ToString();
    }
  } else {
    TransitionToTerminalState(MonitoredTaskState::kRunning, MonitoredTaskState::kComplete);
    VLOG(1) << "TS " << permanent_uuid() << ": snapshot complete on tablet "
            << tablet_->ToString();
  }

  if (state() == MonitoredTaskState::kComplete) {
    bool handled = false;
    switch (operation_) {
      case tserver::TabletSnapshotOpRequestPB::CREATE: {
        handled = true;
        // TODO: this class should not know CatalogManager API,
        //       remove circular dependency between classes.
        master_->catalog_manager()->HandleCreateTabletSnapshotResponse(
            tablet_.get(), resp_.has_error());
        break;
      }
      case tserver::TabletSnapshotOpRequestPB::RESTORE: {
        handled = true;
        // TODO: this class should not know CatalogManager API,
        //       remove circular dependency between classes.
        master_->catalog_manager()->HandleRestoreTabletSnapshotResponse(
            tablet_.get(), resp_.has_error());
        break;
      }
      case tserver::TabletSnapshotOpRequestPB::DELETE: {
        handled = true;
        // TODO: this class should not know CatalogManager API,
        //       remove circular dependency between classes.
        master_->catalog_manager()->HandleDeleteTabletSnapshotResponse(
            snapshot_id_, tablet_.get(), resp_.has_error());
        break;
      }
      case tserver::TabletSnapshotOpRequestPB::UNKNOWN: break; // Not handled.
    }

    if (!handled) {
      FATAL_INVALID_ENUM_VALUE(tserver::TabletSnapshotOpRequestPB::Operation, operation_);
    }
  } else {
    VLOG(1) << "TabletSnapshotOp task is not completed";
  }
}

bool AsyncTabletSnapshotOp::SendRequest(int attempt) {
  tserver::TabletSnapshotOpRequestPB req;
  req.set_dest_uuid(permanent_uuid());
  req.set_tablet_id(tablet_->tablet_id());
  req.set_snapshot_id(snapshot_id_);
  req.set_operation(operation_);
  req.set_propagated_hybrid_time(master_->clock()->Now().ToUint64());

  ts_backup_proxy_->TabletSnapshotOpAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG(1) << "Send tablet snapshot request " << operation_ << " to " << permanent_uuid()
          << " (attempt " << attempt << "):\n"
          << req.DebugString();
  return true;
}

} // namespace master
} // namespace yb
