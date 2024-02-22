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

#include "yb/master/tablet_health_manager.h"

#include <algorithm>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "yb/common/common_net.pb.h"
#include "yb/common/entity_ids_types.h"
#include "yb/common/wire_protocol.h"
#include "yb/common/wire_protocol.pb.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/metadata.pb.h"
#include "yb/consensus/raft_consensus.h"

#include "yb/master/async_rpc_tasks.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager-internal.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master.h"
#include "yb/master/master_admin.pb.h"
#include "yb/master/master_fwd.h"
#include "yb/master/sys_catalog_constants.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"

#include "yb/rpc/rpc_context.h"

#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/tserver.pb.h"

#include "yb/util/flags/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/unique_lock.h"

using std::future;
using std::optional;
using std::pair;
using std::string;
using std::unordered_map;
using std::unordered_set;
using std::vector;

using namespace std::chrono_literals;

namespace yb {
namespace master {

void AreNodesSafeToTakeDownCallbackHandler::ProcessHealthyReplica(const TabletId& tablet_id) {
  auto it = required_replicas_.find(tablet_id);
  if (it == required_replicas_.end()) {
    // This tablet already had enough replicas and was erased by a previous call.
    return;
  }

  --(it->second);
  VLOG(1) << Format("Decremented required replicas for tablet $0 to $1", tablet_id, it->second);

  // We have enough replicas, erase this tablet from the map.
  if (it->second == 0) {
    required_replicas_.erase(it);
  }
}

bool AreNodesSafeToTakeDownCallbackHandler::DoneProcessing() {
  // Wake up the main thread if we have processed all RPCs or all tablets have been removed from the
  // map (i.e., all tablets have enough replicas).
  return outstanding_rpcs_.size() == 0 || required_replicas_.size() == 0;
}

template <typename T>
void AreNodesSafeToTakeDownCallbackHandler::ReportHealthCheck(
    const T& server_resp, const string& uuid) {
  std::lock_guard l(mutex_);

  if (DoneProcessing()) {
    VLOG_WITH_FUNC(1) << "Done processing. Ignoring health check report from peer " << uuid;
    return;
  }

  auto num_erased = outstanding_rpcs_.erase(uuid);
  if (num_erased != 1) {
    VLOG_WITH_FUNC(1) << "Ignoring duplicate health check report from peer " << uuid;
    return;
  }

  if (server_resp.has_error()) {
    // Do not return early here; we want to check DoneProcessing below and wake up the driver if all
    // requests have returned.
    LOG_WITH_FUNC(INFO) << "Got error from server " << uuid << ": "
        << StatusFromPB(server_resp.error().status());
  } else {
    VLOG_WITH_FUNC(1) << Format("Processing health check report from $0. $1 requests outstanding.",
        uuid, outstanding_rpcs_.size());
    ProcessHealthCheck(server_resp);
  }

  if (DoneProcessing()) {
    cv_.notify_one();
  }
}

// Explicitly instantiate the templates, otherwise we get shlib-no-undefined errors when trying
// to call this from the RPC tasks.
template void AreNodesSafeToTakeDownCallbackHandler::ReportHealthCheck(
    const tserver::CheckTserverTabletHealthResponsePB& resp, const string& uuid);
template void AreNodesSafeToTakeDownCallbackHandler::ReportHealthCheck(
    const master::CheckMasterTabletHealthResponsePB& resp, const string& uuid);

void AreNodesSafeToTakeDownCallbackHandler::ProcessHealthCheck(
    const tserver::CheckTserverTabletHealthResponsePB& tserver_resp) {
  for (auto& tablet : tserver_resp.tablet_healths()) {
    if (tablet.role() == LEADER ||
        (tablet.role() == FOLLOWER && tablet.follower_lag_ms() < follower_lag_bound_ms_)) {
      ProcessHealthyReplica(tablet.tablet_id());
    }
  }
}

void AreNodesSafeToTakeDownCallbackHandler::ProcessHealthCheck(
    const master::CheckMasterTabletHealthResponsePB& master_resp) {
  if (master_resp.role() == LEADER ||
      (master_resp.role() == FOLLOWER && master_resp.follower_lag_ms() < follower_lag_bound_ms_)) {
    ProcessHealthyReplica(kSysCatalogTabletId);
  }
}

std::shared_ptr<AreNodesSafeToTakeDownCallbackHandler>
AreNodesSafeToTakeDownCallbackHandler::MakeHandler(
    int64_t follower_lag_bound_ms,
    const std::unordered_map<TabletServerId, std::vector<TabletId>>& tserver_to_tablets,
    const std::vector<consensus::RaftPeerPB>& masters_to_contact,
    ReplicaCountMap required_replicas) {
  ServerUuidSet outstanding_rpcs;
  for (const auto& peer : masters_to_contact) {
    outstanding_rpcs.insert(peer.permanent_uuid());
  }
  for (const auto& [tserver_uuid, _] : tserver_to_tablets) {
    outstanding_rpcs.insert(tserver_uuid);
  }

  // Cannot use std::make_shared here since the class constructor is private (to force the use of
  // MakeHandler).
  auto* handler_ptr = new AreNodesSafeToTakeDownCallbackHandler(
      follower_lag_bound_ms, std::move(required_replicas), std::move(outstanding_rpcs));
  return std::shared_ptr<AreNodesSafeToTakeDownCallbackHandler>(handler_ptr);
}

Result<ReplicaCountMap> AreNodesSafeToTakeDownCallbackHandler::WaitForResponses(
    CoarseTimePoint deadline) {
  UniqueLock lock(mutex_);
  bool finished_processing = cv_.wait_until(
      lock, deadline, std::bind(&AreNodesSafeToTakeDownCallbackHandler::DoneProcessing, this));
  if (!finished_processing) {
    return STATUS_FORMAT(TimedOut, "Timed out waiting for responses");
  }
  return std::move(required_replicas_);
}

Result<unordered_map<TabletServerId, vector<TabletId>>>
AreNodesSafeToTakeDownDriver::FindTserversToContact(ReplicaCountMap* required_replicas) {
  unordered_map<TabletServerId, vector<TabletId>> tablets_to_check;

  unordered_set<TabletServerId> tservers_to_take_down;
  for (auto& tserver : req_.tserver_uuids()) {
    tservers_to_take_down.insert(tserver);
  }

  // Iterate through all tablets to get the list of tservers we need to check for each tablet.
  for (auto& table : catalog_manager_->GetTables(
      GetTablesMode::kRunning, PrimaryTablesOnly::kTrue)) {
    // Min replicas required for tablets of this table. This is set when the first tablet we need
    // to check is encountered.
    optional<size_t> min_replicas;

    for (auto& tablet : table->GetTablets()) {
      auto ts_uuid_to_replica_map = tablet->GetReplicaLocations();
      bool check_tablet_health = std::any_of(
          ts_uuid_to_replica_map->begin(),
          ts_uuid_to_replica_map->end(),
          [&](const auto& ts_uuid_and_replica) {
            return tservers_to_take_down.contains(ts_uuid_and_replica.first);
      });

      if (!check_tablet_health) {
        continue;
      }

      // Get table replication factor, if we have not already done so.
      if (!min_replicas.has_value()) {
        auto rf = VERIFY_RESULT(catalog_manager_->GetTableReplicationFactor(table));
        min_replicas = rf / 2 + 1;
      }
      (*required_replicas)[tablet->id()] = *min_replicas;
      VLOG(1) << Format("Tablet $0 from table $1 requires $2 replicas",
          tablet->id(), table->id(), *min_replicas);

      for (auto& [ts_uuid, _] : *ts_uuid_to_replica_map) {
        // Check for this tablet on all tservers that we are not taking down.
        if (!tservers_to_take_down.contains(ts_uuid)) {
          tablets_to_check[ts_uuid].push_back(tablet->tablet_id());
        }
      }
    }
  }

  return tablets_to_check;
}

Result<std::vector<consensus::RaftPeerPB>>
AreNodesSafeToTakeDownDriver::FindMastersToContact(ReplicaCountMap* required_replicas) {
  std::vector<consensus::RaftPeerPB> masters_to_contact;
  if (req_.master_uuids().empty()) {
    return masters_to_contact;
  }

  auto master_rf = VERIFY_RESULT(catalog_manager_->GetReplicationFactor());
  (*required_replicas)[kSysCatalogTabletId] = master_rf / 2 + 1;

  unordered_set<string> masters_to_take_down;
  for (auto& master : req_.master_uuids()) {
    masters_to_take_down.insert(master);
  }

  std::vector<consensus::RaftPeerPB> masters;
  RETURN_NOT_OK(master_->ListRaftConfigMasters(&masters));
  for (auto& peer : masters) {
    if (masters_to_take_down.contains(peer.permanent_uuid())) {
      continue;
    }
    masters_to_contact.push_back(std::move(peer));
  }
  return masters_to_contact;
}

Status AreNodesSafeToTakeDownDriver::StartCallAndWait(CoarseTimePoint deadline) {
  ReplicaCountMap required_replicas;

  const auto tservers_to_tablets = VERIFY_RESULT(FindTserversToContact(&required_replicas));
  const auto masters_to_contact = VERIFY_RESULT(FindMastersToContact(&required_replicas));

  auto cb_handler = AreNodesSafeToTakeDownCallbackHandler::MakeHandler(
      req_.follower_lag_bound_ms(), tservers_to_tablets, masters_to_contact,
      std::move(required_replicas));

  for (auto& [ts_uuid, tablets] : tservers_to_tablets) {
    auto call = std::make_shared<AsyncTserverTabletHealthTask>(
        master_, catalog_manager_->AsyncTaskPool(), ts_uuid, tablets, cb_handler);
    auto s = catalog_manager_->ScheduleTask(call);
    if (!s.ok()) {
      LOG(WARNING) << "Failed to schedule AsyncMasterTabletHealthTask: " << s;
      return s;
    }
  }

  for (auto& master : masters_to_contact) {
    auto call = std::make_shared<AsyncMasterTabletHealthTask>(
        master_, catalog_manager_->AsyncTaskPool(), master, cb_handler);
    auto s = catalog_manager_->ScheduleTask(call);
    if (!s.ok()) {
      LOG(WARNING) << "Failed to schedule AsyncMasterTabletHealthTask: " << s;
      return s;
    }
  }

  auto tablets_missing_replicas = VERIFY_RESULT(cb_handler->WaitForResponses(deadline));
  if (!tablets_missing_replicas.empty()) {
    auto& [tablet_id, missing_replicas] = *tablets_missing_replicas.begin();
    return STATUS_FORMAT(IllegalState,
        "$0 tablet(s) would be under-replicated. Example: tablet $1 would be under-replicated by "
        "$2 replicas", tablets_missing_replicas, tablet_id, missing_replicas);
  }

  return Status::OK();
}

Status TabletHealthManager::AreNodesSafeToTakeDown(
    const AreNodesSafeToTakeDownRequestPB* req, AreNodesSafeToTakeDownResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG(INFO) << "Processing AreNodesSafeToTakeDown call";
  AreNodesSafeToTakeDownDriver driver(*req, resp, master_, catalog_manager_);
  auto status = driver.StartCallAndWait(rpc->GetClientDeadline());
  if (!status.ok()) {
    return SetupError(resp->mutable_error(), MasterErrorPB::INTERNAL_ERROR, status);
  }
  return Status::OK();
}

Status TabletHealthManager::CheckMasterTabletHealth(
    const CheckMasterTabletHealthRequestPB *req, CheckMasterTabletHealthResponsePB *resp) {
  auto consensus_result = catalog_manager_->tablet_peer()->GetRaftConsensus();
  if (!consensus_result) {
    return STATUS_FORMAT(IllegalState, "Could not get sys catalog tablet consensus");
  }
  auto& consensus = *consensus_result;
  auto role = consensus->role();
  resp->set_role(role);
  if (role != PeerRole::LEADER) {
    resp->set_follower_lag_ms(consensus->follower_lag_ms());
  }
  return Status::OK();
}

Status TabletHealthManager::GetMasterHeartbeatDelays(
    const GetMasterHeartbeatDelaysRequestPB* req, GetMasterHeartbeatDelaysResponsePB* resp) {
  auto consensus_result = catalog_manager_->tablet_peer()->GetConsensus();
  if (!consensus_result) {
    return STATUS_FORMAT(IllegalState, "Could not get sys catalog tablet consensus");
  }
  auto& consensus = *consensus_result;
  auto now = MonoTime::Now();
  for (auto& last_communication_time : consensus->GetFollowerCommunicationTimes()) {
    auto* heartbeat_delay = resp->add_heartbeat_delay();
    heartbeat_delay->set_master_uuid(std::move(last_communication_time.peer_uuid));
    heartbeat_delay->set_last_heartbeat_delta_ms(
        now.GetDeltaSince(last_communication_time.last_successful_communication)
            .ToMilliseconds());
  }
  return Status::OK();
}

} // namespace master
} // namespace yb
