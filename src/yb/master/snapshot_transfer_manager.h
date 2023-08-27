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

#include <future>
#include <unordered_set>
#include <vector>

#include "yb/master/catalog_entity_info.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/master_admin.fwd.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_fwd.h"

#include "yb/util/flags.h"
#include "yb/util/status_fwd.h"
#include "yb/util/locks.h"

namespace yb {

namespace client {
class YBClient;
}  // namespace client

namespace master {

using TableMetaPB = ImportSnapshotMetaResponsePB::TableMetaPB;

class Master;
class CatalogManager;
class AsyncSnapshotTransferTask;
typedef std::shared_ptr<AsyncSnapshotTransferTask> AsyncSnapshotTransferTaskPtr;

// Handle SnapshotTransfer related operations. Currently, the class is solely used for xCluster
// native bootstrap and requires a YBClient* that's connected to the producer universe. Thus, a
// separate SnapshotTransferManager should be created for each native bootstrap and TransferSnapshot
// should only be called once during the lifespan of the instance.
class SnapshotTransferManager {
 public:
  explicit SnapshotTransferManager(
      Master* master, CatalogManagerIf* catalog_manager, client::YBClient* producer_client)
      : master_(DCHECK_NOTNULL(master)),
        catalog_manager_(DCHECK_NOTNULL(catalog_manager)),
        producer_client_(DCHECK_NOTNULL(producer_client)) {}

  // Performs a synchronous snapshot transfer. Returns once the tasks are all complete or if failed.
  Status TransferSnapshot(
      const TxnSnapshotId& old_snapshot_id, const TxnSnapshotId& new_snapshot_id,
      const std::vector<TableMetaPB>& tables, const LeaderEpoch& epoch);

  void HandleSnapshotTransferResponse(
      const TabletServerId& ts_uuid, const TabletId& tablet_id, const Status& status);

 private:
  // Start the background task to send the SnapshotTransfer RPC to the Tablet Server.
  Status SendSnapshotTransferRequest(
      const TxnSnapshotId& old_snapshot_id, const TxnSnapshotId& new_snapshot_id,
      const TabletServerId& ts_uuid, const TabletId& tablet_id, const LeaderEpoch& epoch);

  Master* master_;
  CatalogManagerIf* catalog_manager_;
  client::YBClient* producer_client_;

  std::promise<Status> result_;
  mutable rw_spinlock mutex_;
  struct SnapshotTransferInfo {
    int32_t transferring_ = 0;
    int32_t succeeded_ = 0;
    int32_t failed_ = 0;
  };
  SnapshotTransferInfo transfer_info_ GUARDED_BY(mutex_);

  // Values used for creating tasks.
  std::unordered_map<TabletId, TabletId> consumer_producer_tablet_id_map_;
  std::unordered_map<TabletId, TSInfoPB> producer_tablet_ts_info_;
  std::unordered_map<TabletId, TabletInfoPtr> consumer_tablet_info_;
  std::unordered_map<TabletServerId, std::vector<TabletId>> ts_tablet_map_;
};

}  // namespace master
}  // namespace yb
