// Copyright (c) YugabyteDB, Inc.
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

#pragma once

#include "yb/cdc/cdc_service.h"

namespace yb::cdc {

Result<bool> IsBootstrapRequiredForTablet(
    tablet::TabletPeerPtr tablet_peer, const OpId& min_op_id, const CoarseTimePoint& deadline);

class XClusterProducerBootstrap {
 public:
  XClusterProducerBootstrap(
      CDCServiceImpl* cdc_service, const BootstrapProducerRequestPB& req,
      BootstrapProducerResponsePB* resp, CDCCreationState* creation_state,
      CDCServiceContext* cdc_service_context, CDCStateTable* cdc_state_table)
      : cdc_service_(cdc_service),
        req_(req),
        resp_(resp),
        creation_state_(creation_state),
        cdc_service_context_(cdc_service_context),
        cdc_state_table_(cdc_state_table) {}

  // RunBootstrapProducer runs tablet operations in parallel & batching to reduce overall RPC count.
  // Steps:
  // 1. Create CDC Streams for each Table under Bootstrap
  // 2. Create a mapping of servers to list of tablet peers for these Tables
  // 3. Async per server, get the Latest OpID on each tablet leader.
  // 4. Async per server, Set WAL Retention on each tablet peer.
  // Optimization:
  // If we have a local peer, we can try to get the checkpoint from that peer to potentially reduce
  // rpcs. If it is behind we will end up with a more conservative checkpoint, but that is ok.
  Status RunBootstrapProducer();

 private:
  Status CreateAllBootstrapStreams();

  Status ConstructServerToTabletsMapping();

  Status GetLatestOpIdsFromLocalPeers();

  Status FetchLatestOpIdsFromRemotePeers();

  Status VerifyTabletOpIds();

  Status SetLogRetentionForLocalTabletPeers();

  Status SetLogRetentionForRemoteTabletPeers();

  Status UpdateCdcStateTableWithCheckpoints();

  void PrepareResponse();

  Result<bool> IsBootstrapRequired();

  CDCServiceImpl* cdc_service_;
  const BootstrapProducerRequestPB& req_;
  BootstrapProducerResponsePB* resp_;
  CDCCreationState* creation_state_;
  CDCServiceContext* cdc_service_context_;
  CDCStateTable* cdc_state_table_;

  using BootstrapTabletPair = std::pair<xrepl::StreamId, TabletId>;
  using ServerToCDCServiceMap = std::unordered_map<std::string, std::shared_ptr<CDCServiceProxy>>;
  using ServerToBootstrapTabletPairMap =
      std::unordered_map<std::string, std::vector<BootstrapTabletPair>>;
  using BTPHash = boost::hash<BootstrapTabletPair>;

  std::vector<std::pair<xrepl::StreamId, TableId>> bootstrap_ids_and_tables_;
  std::unordered_map<BootstrapTabletPair, yb::OpId, BTPHash> tablet_op_ids_;
  ServerToCDCServiceMap server_to_proxy_;
  ServerToBootstrapTabletPairMap server_to_remote_tablets_;
  std::unordered_set<BootstrapTabletPair, BTPHash> local_tablets_;
  std::unordered_set<BootstrapTabletPair, BTPHash> remote_tablets_to_fetch_opids_for_;
  std::vector<TabletId> local_leader_tablets_;
  std::unordered_map<std::string, std::vector<TabletId>> server_to_remote_leader_tablets_;
  HybridTime bootstrap_time_ = HybridTime::kMin;
};

}  // namespace yb::cdc
