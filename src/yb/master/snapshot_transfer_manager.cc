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
#include "yb/master/snapshot_transfer_manager.h"
#include "yb/master/ts_descriptor.h"
#include "yb/client/client.h"
#include "yb/util/trace.h"

DEFINE_test_flag(
    bool, xcluster_fail_snapshot_transfer, false,
    "In the SetupReplicationWithBootstrap flow, test failure to transfer snapshot on consumer.");

namespace yb {
namespace master {

Status SnapshotTransferManager::TransferSnapshot(
    const TxnSnapshotId& old_snapshot_id, const TxnSnapshotId& new_snapshot_id,
    const std::vector<TableMetaPB>& tables, const LeaderEpoch& epoch) {
  for (const auto& table : tables) {
    // Get the producer replica locations for all tablets in the table. Making the call here so we
    // only make O(table) rpc requests to the producer universe instead of O(tablets).
    const auto& producer_table_id = table.table_ids().old_id();
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> producer_tablets;
    RETURN_NOT_OK_PREPEND(
        producer_client_->GetTabletsFromTableId(
            producer_table_id, /* max_tablets = */ 0, &producer_tablets),
        Format("Unable to retrieve producer tablet locations for table $0", producer_table_id));

    // For each tablet, get the TSInfoPB of the tablet leader replica.
    for (const auto& tablet : producer_tablets) {
      const auto& producer_tablet_id = tablet.tablet_id();
      for (const auto& replica : tablet.replicas()) {
        if (replica.role() == PeerRole::LEADER) {
          producer_tablet_ts_info_[producer_tablet_id] = replica.ts_info();
          break;
        }
      }
    }

    for (const auto& tablet_id_pair : table.tablets_ids()) {
      // For each old tablet ID generated in the import snapshot response, ensure that we've
      // retrieved the leader replica TSInfoPB in the previous step.
      const auto& producer_tablet_id = tablet_id_pair.old_id();
      SCHECK(
          producer_tablet_ts_info_.contains(producer_tablet_id), IllegalState,
          Format("Did not retrieve tablet $0's TSInfo", producer_tablet_id));


      // For each consumer tablet, we need to find its TabletInfo. The contents of the object
      // will help us construct a RetrySpecificTSRpcTask later on so that we can dispatch
      // requests to specific TServers for each tablet.
      const auto& consumer_tablet_id = tablet_id_pair.new_id();
      auto consumer_tablet = VERIFY_RESULT(catalog_manager_->GetTabletInfo(consumer_tablet_id));
      consumer_tablet_info_[consumer_tablet_id] = consumer_tablet;
      {
        auto l = consumer_tablet->LockForRead();

        auto locs = consumer_tablet->GetReplicaLocations();
        for (const auto& replica : *locs) {
          const auto& ts_uuid = replica.second.ts_desc->permanent_uuid();
          ts_tablet_map_[ts_uuid].push_back(consumer_tablet_id);
        }
      }
      consumer_producer_tablet_id_map_[consumer_tablet_id] = producer_tablet_id;
    }
  }

  for (const auto& [ts_uuid, tablet_ids] : ts_tablet_map_) {
    for (const auto& tablet_id : tablet_ids) {
      RETURN_NOT_OK_PREPEND(
          SendSnapshotTransferRequest(old_snapshot_id, new_snapshot_id, ts_uuid, tablet_id, epoch),
          Format(
              "Failed to send snapshot transfer request for tablet $0 on tserver $1", tablet_id,
              ts_uuid));

      std::lock_guard lock(mutex_);
      transfer_info_.transferring_++;
    }
  }

  LOG(INFO) << "Successfully started snapshot transfer task";

  return result_.get_future().get();
}

void SnapshotTransferManager::HandleSnapshotTransferResponse(
    const TabletServerId& ts_uuid, const TabletId& tablet_id, const Status& status) {
  LOG(INFO) << Format(
      "Handing snapshot transfer response for tablet $0 on TS $1 with status $2", tablet_id,
      ts_uuid, status);

  std::lock_guard lock(mutex_);
  transfer_info_.transferring_--;

  if (!status.ok()) {
    transfer_info_.failed_++;
  } else {
    transfer_info_.succeeded_++;
  }

  if (transfer_info_.transferring_ == 0) {
    if (PREDICT_FALSE(FLAGS_TEST_xcluster_fail_snapshot_transfer)) {
      result_.set_value(STATUS(Aborted, "Test failure"));
      return;
    }

    if (transfer_info_.failed_ == 0) {
      LOG(INFO) << "Successfully completed snapshot transfer task";
      result_.set_value(Status::OK());
    } else {
      LOG(WARNING) << "Failed snapshot transfer task";
      result_.set_value(status);
    }
  }

  VLOG(1) << Format(
      "Transferring $0; Succeeded $1; Failed $2", transfer_info_.transferring_,
      transfer_info_.succeeded_, transfer_info_.failed_);
}

Status SnapshotTransferManager::SendSnapshotTransferRequest(
    const TxnSnapshotId& old_snapshot_id, const TxnSnapshotId& new_snapshot_id,
    const TabletServerId& ts_uuid, const TabletId& tablet_id, const LeaderEpoch& epoch) {
  const auto& table = consumer_tablet_info_[tablet_id]->table();
  const auto& producer_tablet_id = consumer_producer_tablet_id_map_[tablet_id];
  const auto& producer_ts_info = producer_tablet_ts_info_[producer_tablet_id];

  auto task = std::make_shared<AsyncSnapshotTransferTask>(
      master_, catalog_manager_->AsyncTaskPool(), this, ts_uuid, table, tablet_id,
      producer_tablet_id, producer_ts_info, old_snapshot_id, new_snapshot_id, epoch);
  table->AddTask(task);
  return catalog_manager_->ScheduleTask(task);
}

}  // namespace master
}  // namespace yb
