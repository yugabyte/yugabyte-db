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

#include "yb/master/async_rpc_tasks.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/snapshot_transfer_manager.h"

namespace yb {

namespace master {

// Send the request to the specified Tablet Server.
// Keeps retrying until we get an "ok" response.
class AsyncSnapshotTransferTask : public RetrySpecificTSRpcTask {
 public:
  AsyncSnapshotTransferTask(
      Master* master, ThreadPool* callback_pool, SnapshotTransferManager* task_manager,
      const TabletServerId& ts_uuid, const scoped_refptr<TableInfo>& table,
      const TabletId& consumer_tablet_id, const TabletId& producer_tablet_id,
      const TSInfoPB& producer_ts_info, const TxnSnapshotId& old_snapshot_id,
      const TxnSnapshotId& new_snapshot_id, LeaderEpoch epoch);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kFlushTablets;
  }

  std::string type_name() const override { return "Snapshot transfer"; }

  std::string description() const override;

 private:
  TabletId tablet_id() const override { return consumer_tablet_id_; }
  TabletServerId permanent_uuid() const;

  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

  SnapshotTransferManager* task_manager_;
  const TabletId consumer_tablet_id_;
  const TabletId producer_tablet_id_;
  const TSInfoPB producer_ts_info_;
  const TxnSnapshotId old_snapshot_id_;
  const TxnSnapshotId new_snapshot_id_;
  tserver::StartRemoteSnapshotTransferResponsePB resp_;
};

}  // namespace master
}  // namespace yb
