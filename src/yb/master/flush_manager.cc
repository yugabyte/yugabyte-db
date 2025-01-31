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

#include "yb/master/flush_manager.h"

#include "yb/util/logging.h"

#include "yb/master/async_flush_tablets_task.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_admin.pb.h"
#include "yb/master/master_error.h"
#include "yb/master/master_util.h"
#include "yb/master/ts_descriptor.h"

#include "yb/util/status_log.h"
#include "yb/util/trace.h"

namespace yb {
namespace master {

using std::map;
using std::vector;

Status FlushManager::FlushTables(const FlushTablesRequestPB* req,
                                 FlushTablesResponsePB* resp,
                                 rpc::RpcContext* rpc,
                                 const LeaderEpoch& epoch) {
  LOG(INFO) << "Servicing FlushTables request: " << req->ShortDebugString();

  // Check request.
  if (req->tables_size() == 0) {
    const Status s = STATUS(IllegalState, "Empty table list in flush tables request",
                            req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
    return SetupError(resp->mutable_error(), s);
  }

  // Create a new flush request UUID.
  const FlushRequestId flush_id = catalog_manager_->GenerateId();

  const auto tables = VERIFY_RESULT(
      catalog_manager_->CollectTables(req->tables(), req->add_indexes()));

  // Per TS tablet lists for all provided tables.
  map<TabletServerId, vector<TabletId>> ts_tablet_map;
  scoped_refptr<TableInfo> table;

  for (const TableDescription& table_description : tables) {
    table = table_description.table_info;

    // Prepare per Tablet Server tablet lists.
    for (const auto& tablet : table_description.tablet_infos) {
      TRACE("Locking tablet");
      auto l = tablet->LockForRead();

      auto locs = tablet->GetReplicaLocations();
      for (const auto& [ts_uuid, _] : *locs) {
        ts_tablet_map[ts_uuid].push_back(tablet->id());
      }
    }
  }

  DCHECK_GT(ts_tablet_map.size(), 0);

  {
    std::lock_guard l(lock_);
    TRACE("Acquired flush manager lock");

    // Init Tablet Server id lists in memory storage.
    TSFlushingInfo& flush_info = flush_requests_[flush_id];
    flush_info.clear();

    for (const auto& ts : ts_tablet_map) {
      flush_info.ts_flushing_.insert(ts.first);
    }

    DCHECK_EQ(flush_info.ts_flushing_.size(), ts_tablet_map.size());
  }

  // Clean-up complete flushing requests.
  DeleteCompleteFlushRequests();

  DCHECK_ONLY_NOTNULL(table.get());

  const bool is_compaction = req->is_compaction();

  if (is_compaction) {
    for (const auto& table_description : tables) {
      RETURN_NOT_OK(catalog_manager_->UpdateLastFullCompactionRequestTime(
          table_description.table_info->id(), epoch));
    }
  }

  // Send FlushTablets requests to all Tablet Servers (one TS - one request).
  for (const auto& ts : ts_tablet_map) {
    // Using last table async task queue.
    SendFlushTabletsRequest(
        ts.first, table, ts.second, flush_id, is_compaction, req->regular_only(), epoch);
  }

  resp->set_flush_request_id(flush_id);

  LOG(INFO) << "Successfully started flushing request " << flush_id;

  return Status::OK();
}

Status FlushManager::IsFlushTablesDone(const IsFlushTablesDoneRequestPB* req,
                                       IsFlushTablesDoneResponsePB* resp) {
  LOG(INFO) << "Servicing IsFlushTablesDone request: " << req->ShortDebugString();

  std::lock_guard l(lock_);
  TRACE("Acquired flush manager lock");

  // Check flush request id.
  const FlushRequestMap::const_iterator it = flush_requests_.find(req->flush_request_id());

  if (it == flush_requests_.end()) {
    const Status s = STATUS(NotFound, "The flush request was not found", req->flush_request_id(),
                            MasterError(MasterErrorPB::INVALID_REQUEST));
    return SetupError(resp->mutable_error(), s);
  }

  const TSFlushingInfo& flush_info = it->second;
  resp->set_done(flush_info.ts_flushing_.empty());
  resp->set_success(flush_info.ts_failed_.empty());

  VLOG(1) << "IsFlushTablesDone request: " << req->flush_request_id()
          << " Done: " << resp->done() << " Success " << resp->success();
  return Status::OK();
}

void FlushManager::SendFlushTabletsRequest(const TabletServerId& ts_uuid,
                                           const scoped_refptr<TableInfo>& table,
                                           const vector<TabletId>& tablet_ids,
                                           const FlushRequestId& flush_id,
                                           const bool is_compaction,
                                           const bool regular_only,
                                           const LeaderEpoch& epoch) {
  auto call = std::make_shared<AsyncFlushTablets>(
      master_, catalog_manager_->AsyncTaskPool(), ts_uuid, table, tablet_ids, flush_id,
      is_compaction, regular_only, epoch);
  table->AddTask(call);
  WARN_NOT_OK(catalog_manager_->ScheduleTask(call), "Failed to send flush tablets request");
}

void FlushManager::HandleFlushTabletsRpcFinish(const FlushRequestId& flush_id,
                                               const TabletServerId& ts_uuid,
                                               const Status& status) {
  LOG(INFO) << "Handling Flush Tablets RPC Finish for TS " << ts_uuid
            << ": flush request id: " << flush_id << ", status: " << status;

  std::lock_guard l(lock_);
  TRACE("Acquired flush manager lock");
  UpdateFlushRequestsUnlocked(flush_id, ts_uuid, status);
}

void FlushManager::HandleFlushTabletsResponse(const FlushRequestId& flush_id,
                                              const TabletServerId& ts_uuid,
                                              const Status& status) {
  LOG(INFO) << "Handling Flush Tablets Response from TS " << ts_uuid
            << ": flush request id: " << flush_id << ", status: " << status;

  std::lock_guard l(lock_);
  TRACE("Acquired flush manager lock");
  UpdateFlushRequestsUnlocked(flush_id, ts_uuid, status);
}

void FlushManager::UpdateFlushRequestsUnlocked(const FlushRequestId& flush_id,
                                               const TabletServerId& ts_uuid,
                                               const Status& status) {
  // Check current flush request id.
  const FlushRequestMap::iterator it = flush_requests_.find(flush_id);

  if (it == flush_requests_.end()) {
    LOG(WARNING) << "Old flush request id is in the flush tablets response: " << flush_id;
    return;
  }

  TSFlushingInfo& flush_info = it->second;

  if (flush_info.ts_flushing_.erase(ts_uuid) > 0) {
    (status.IsOk() ? flush_info.ts_succeed_ : flush_info.ts_failed_).insert(ts_uuid);

    // Finish this flush request operation.
    if (flush_info.ts_flushing_.empty()) {
      if (flush_info.ts_failed_.empty()) {
        LOG(INFO) << "Successfully complete flush table request: " << flush_id;
      } else {
        LOG(WARNING) << "Failed flush table request: " << flush_id;
      }
    }
  }

  VLOG(1) << "Flush table request: " << flush_id
          << ". Flushing " << flush_info.ts_flushing_.size()
          << "; Succeed " << flush_info.ts_succeed_.size()
          << "; Failed " << flush_info.ts_failed_.size() << " TServers";
}

void FlushManager::DeleteCompleteFlushRequests() {
  std::lock_guard l(lock_);
  TRACE("Acquired flush manager lock");

  // Clean-up complete flushing requests.
  for (FlushRequestMap::iterator it = flush_requests_.begin(); it != flush_requests_.end();) {
    if (it->second.ts_flushing_.empty()) {
      it = flush_requests_.erase(it);
    } else {
      ++it;
    }
  }
}

} // namespace master
} // namespace yb
