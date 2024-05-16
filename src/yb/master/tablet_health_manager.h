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

#include <condition_variable>
#include <unordered_map>

#include "yb/master/catalog_manager_if.h"
#include "yb/tserver/tserver.pb.h"
#include "yb/util/status.h"

namespace yb {
namespace master {

class Master;
class CatalogManager;
class TableInfo;

using ReplicaCountMap = std::unordered_map<TabletId, size_t>;
using ServerUuidSet = std::unordered_set<std::string>;

// This class may outlive the driver (e.g. if the driver exits because of the timeout).
class AreNodesSafeToTakeDownCallbackHandler {
 public:
  static std::shared_ptr<AreNodesSafeToTakeDownCallbackHandler> MakeHandler(
    int64_t follower_lag_bound_ms,
    const std::unordered_map<TabletServerId, std::vector<TabletId>>& tserver_to_tablet,
    const std::vector<consensus::RaftPeerPB>& masters_to_contact,
    ReplicaCountMap required_replicas);

  // Callback for the async tasks we schedule.
  template <typename T>
  void ReportHealthCheck(const T& resp, const std::string& uuid);

  Result<ReplicaCountMap> WaitForResponses(CoarseTimePoint deadline);

 private:
  const int64_t follower_lag_bound_ms_;
  ReplicaCountMap required_replicas_ GUARDED_BY(mutex_);
  ServerUuidSet outstanding_rpcs_ GUARDED_BY(mutex_);
  std::mutex mutex_;
  std::condition_variable_any cv_;

  AreNodesSafeToTakeDownCallbackHandler(int64_t follower_lag_bound_ms,
                                        ReplicaCountMap required_replicas,
                                        ServerUuidSet outstanding_rpcs) :
    follower_lag_bound_ms_(follower_lag_bound_ms),
    required_replicas_(std::move(required_replicas)),
    outstanding_rpcs_(std::move(outstanding_rpcs)) {}

  bool DoneProcessing() REQUIRES_SHARED(mutex_);

  void ProcessHealthCheck(
      const tserver::CheckTserverTabletHealthResponsePB& tserver_resp)
      REQUIRES(mutex_);
  void ProcessHealthCheck(
      const master::CheckMasterTabletHealthResponsePB& master_resp)
      REQUIRES(mutex_);
  void ProcessHealthyReplica(const TabletId& tablet_id) REQUIRES(mutex_);
};

class AreNodesSafeToTakeDownDriver {
 public:
  AreNodesSafeToTakeDownDriver(
      const AreNodesSafeToTakeDownRequestPB& req,
      Master* master, CatalogManagerIf* catalog_manager) :
      req_(req), master_(master), catalog_manager_(catalog_manager) {}

  Status StartCallAndWait(CoarseTimePoint deadline);

 private:
  const AreNodesSafeToTakeDownRequestPB& req_;
  Master* master_;
  CatalogManagerIf* catalog_manager_;

  Result<std::vector<consensus::RaftPeerPB>> FindMastersToContact(
      ReplicaCountMap* required_replicas);

  Result<std::unordered_map<TabletServerId, std::vector<TabletId>>> FindTserversToContact(
      ReplicaCountMap* required_replicas);

  Status ScheduleTServerTasks(
      std::unordered_map<TabletServerId, std::vector<TabletId>>&& tservers,
      std::shared_ptr<AreNodesSafeToTakeDownCallbackHandler> cb_handler);

  Status ScheduleMasterTasks(
      std::vector<consensus::RaftPeerPB>&& masters,
      std::shared_ptr<AreNodesSafeToTakeDownCallbackHandler> cb_handler);
};

class TabletHealthManager {
 public:
  explicit TabletHealthManager(Master* master, CatalogManagerIf* catalog_manager)
      : master_(DCHECK_NOTNULL(master)),
        catalog_manager_(DCHECK_NOTNULL(catalog_manager)) {}

  Status AreNodesSafeToTakeDown(
      const AreNodesSafeToTakeDownRequestPB* req, AreNodesSafeToTakeDownResponsePB* resp,
      rpc::RpcContext* rpc);

  Status CheckMasterTabletHealth(
      const CheckMasterTabletHealthRequestPB* req, CheckMasterTabletHealthResponsePB* resp);

  Status GetMasterHeartbeatDelays(
      const GetMasterHeartbeatDelaysRequestPB* req, GetMasterHeartbeatDelaysResponsePB* resp);

 private:
  Master* master_;
  CatalogManagerIf* catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(TabletHealthManager);
};

} // namespace master
} // namespace yb
