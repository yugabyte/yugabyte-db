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

#include "yb/master/catalog_manager.h"

#include "yb/util/status.h"

namespace yb::master {

// This class collects the implementation of some of the rpcs in the MasterCluster service.
class MasterClusterHandler {
 public:
  explicit MasterClusterHandler(CatalogManager* catalog_manager, TSManager* ts_manager);
  // ============================================================================
  // Methods invoked by RPC macro machinery. These implement RPCs.
  // ============================================================================
  Status GetClusterConfig(GetMasterClusterConfigResponsePB* resp);

  Status SetClusterConfig(
      const ChangeMasterClusterConfigRequestPB* req, ChangeMasterClusterConfigResponsePB* resp,
      rpc::RpcContext* rpc, const LeaderEpoch& epoch);

  Status GetLoadMoveCompletionPercent(GetLoadMovePercentResponsePB* resp);

  Status GetLeaderBlacklistCompletionPercent(GetLoadMovePercentResponsePB* resp);

  Status AreLeadersOnPreferredOnly(
      const AreLeadersOnPreferredOnlyRequestPB* req, AreLeadersOnPreferredOnlyResponsePB* resp);

  Status SetPreferredZones(
      const SetPreferredZonesRequestPB* req, SetPreferredZonesResponsePB* resp,
      rpc::RpcContext* rpc, const LeaderEpoch& epoch);

  // ============================================================================
  // Helper methods called by the methods which implement RPCs.
  // ============================================================================

  Status GetLoadMoveCompletionPercent(GetLoadMovePercentResponsePB* resp, bool blacklist_leader);

 private:
  CatalogManager* catalog_manager_;
  TSManager* ts_manager_;
};
}  // namespace yb::master
