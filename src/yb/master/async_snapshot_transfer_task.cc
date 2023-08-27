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

#include "yb/master/async_snapshot_transfer_task.h"

#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.proxy.h"
#include "yb/tserver/tserver_service.proxy.h"

namespace yb {
namespace master {

using std::string;
using std::vector;
using tserver::TabletServerErrorPB;

////////////////////////////////////////////////////////////
// AsyncSnapshotTransferTask
////////////////////////////////////////////////////////////
AsyncSnapshotTransferTask::AsyncSnapshotTransferTask(
    Master* master, ThreadPool* callback_pool, SnapshotTransferManager* task_manager,
    const TabletServerId& ts_uuid, const scoped_refptr<TableInfo>& table,
    const TabletId& consumer_tablet_id, const TabletId& producer_tablet_id,
    const TSInfoPB& producer_ts_info, const TxnSnapshotId& old_snapshot_id,
    const TxnSnapshotId& new_snapshot_id, LeaderEpoch epoch)
    : RetrySpecificTSRpcTask(
          master, callback_pool, ts_uuid, table, std::move(epoch),
          /* async_task_throttler */ nullptr),
      task_manager_(task_manager),
      consumer_tablet_id_(consumer_tablet_id),
      producer_tablet_id_(producer_tablet_id),
      producer_ts_info_(producer_ts_info),
      old_snapshot_id_(old_snapshot_id),
      new_snapshot_id_(new_snapshot_id) {}

string AsyncSnapshotTransferTask::description() const {
  return Format("$0 Snapshot Transfer RPC", permanent_uuid());
}

TabletServerId AsyncSnapshotTransferTask::permanent_uuid() const { return permanent_uuid_; }

void AsyncSnapshotTransferTask::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    Status status = StatusFromPB(resp_.error().status());

    // Do not retry on a fatal error.
    switch (resp_.error().code()) {
      case TabletServerErrorPB::TABLET_NOT_FOUND:
        // Tablet splitting is disabled during this flow. Should never expect to see this as a
        // result of a split.
        LOG(WARNING) << Format(
            "TS $0: failed. No further retry: $1", permanent_uuid(), status.ToString());

        // We transition to complete state following the pattern of other async tasks like:
        // async_flush_tablets_task & async_snapshot_task. May need to reconsider changing to
        // TransitionToFailState & modify the logic below:
        // if (state() == server::MonitoredTask::kComplete) to account for that change
        TransitionToCompleteState();
        break;
      default:
        LOG(WARNING) << Format(
            "TS $0: failed. Snapshot transfer failed: $1", permanent_uuid(), status.ToString());
    }
  } else {
    TransitionToCompleteState();
    VLOG(1) << "TS " << permanent_uuid() << ": snapshot transfer complete";
  }

  if (state() == server::MonitoredTaskState::kComplete) {
    task_manager_->HandleSnapshotTransferResponse(
        permanent_uuid_, consumer_tablet_id_,
        resp_.has_error() ? StatusFromPB(resp_.error().status()) : Status::OK());
  } else {
    VLOG(1) << "SnapshotTransfer task is not completed";
  }
}

bool AsyncSnapshotTransferTask::SendRequest(int attempt) {
  tserver::StartRemoteSnapshotTransferRequestPB req;
  req.set_dest_uuid(permanent_uuid_);
  req.set_tablet_id(consumer_tablet_id_);
  req.set_snapshot_id(old_snapshot_id_.data(), old_snapshot_id_.size());
  req.set_new_snapshot_id(new_snapshot_id_.data(), new_snapshot_id_.size());
  req.set_source_peer_uuid(producer_tablet_id_);

  req.mutable_source_private_addr()->CopyFrom(producer_ts_info_.private_rpc_addresses());
  req.mutable_source_broadcast_addr()->CopyFrom(producer_ts_info_.broadcast_addresses());
  req.mutable_source_cloud_info()->CopyFrom(producer_ts_info_.cloud_info());
  ts_proxy_->StartRemoteSnapshotTransferAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG(1) << "Send snapshot transfer request to " << permanent_uuid_ << " (attempt " << attempt
          << "):\n"
          << req.DebugString();
  return true;
}

}  // namespace master
}  // namespace yb
