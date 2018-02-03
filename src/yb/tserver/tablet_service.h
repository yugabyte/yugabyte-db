// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
#ifndef YB_TSERVER_TABLET_SERVICE_H_
#define YB_TSERVER_TABLET_SERVICE_H_

#include <memory>
#include <string>
#include <vector>

#include "yb/common/read_hybrid_time.h"
#include "yb/consensus/consensus.service.h"
#include "yb/gutil/ref_counted.h"

#include "yb/tablet/tablet_fwd.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/tablet_server_interface.h"
#include "yb/tserver/tserver_admin.service.h"
#include "yb/tserver/tserver_service.service.h"

namespace yb {
class Schema;
class Status;
class HybridTime;

namespace tserver {

class TabletPeerLookupIf;
class TabletServer;

class TabletServiceImpl : public TabletServerServiceIf {
 public:
  explicit TabletServiceImpl(TabletServerIf* server);

  void Write(const WriteRequestPB* req, WriteResponsePB* resp, rpc::RpcContext context) override;

  void Read(const ReadRequestPB* req, ReadResponsePB* resp, rpc::RpcContext context) override;

  void NoOp(const NoOpRequestPB* req, NoOpResponsePB* resp, rpc::RpcContext context) override;

  void ListTablets(const ListTabletsRequestPB* req,
                   ListTabletsResponsePB* resp,
                   rpc::RpcContext context) override;

  void ListTabletsForTabletServer(const ListTabletsForTabletServerRequestPB* req,
                                  ListTabletsForTabletServerResponsePB* resp,
                                  rpc::RpcContext context) override;

  void GetLogLocation(
      const GetLogLocationRequestPB* req,
      GetLogLocationResponsePB* resp,
      rpc::RpcContext context) override;

  void Checksum(const ChecksumRequestPB* req,
                ChecksumResponsePB* resp,
                rpc::RpcContext context) override;

  void ImportData(const ImportDataRequestPB* req,
                  ImportDataResponsePB* resp,
                  rpc::RpcContext context) override;

  void UpdateTransaction(const UpdateTransactionRequestPB* req,
                         UpdateTransactionResponsePB* resp,
                         rpc::RpcContext context) override;

  void GetTransactionStatus(const GetTransactionStatusRequestPB* req,
                            GetTransactionStatusResponsePB* resp,
                            rpc::RpcContext context) override;

  void AbortTransaction(const AbortTransactionRequestPB* req,
                        AbortTransactionResponsePB* resp,
                        rpc::RpcContext context) override;

  void Truncate(const TruncateRequestPB* req,
                TruncateResponsePB* resp,
                rpc::RpcContext context) override;

  void GetTabletStatus(const GetTabletStatusRequestPB* req,
                       GetTabletStatusResponsePB* resp,
                       rpc::RpcContext context) override;

  void Shutdown() override;

 private:
  // Check if the tablet peer is the leader and is in ready state for servicing IOs.
  CHECKED_STATUS CheckPeerIsLeaderAndReady(const tablet::TabletPeer& tablet_peer,
                                           TabletServerErrorPB::Code* error_code);

  CHECKED_STATUS CheckPeerIsLeader(const tablet::TabletPeer& tablet_peer,
                                   TabletServerErrorPB::Code* error_code);

  CHECKED_STATUS CheckPeerIsReady(const tablet::TabletPeer& tablet_peer,
                                  TabletServerErrorPB::Code* error_code);

  template <class Req, class Resp>
  bool DoGetTabletOrRespond(const Req* req, Resp* resp, rpc::RpcContext* context,
                            std::shared_ptr<tablet::AbstractTablet>* tablet);

  virtual bool GetTabletOrRespond(const ReadRequestPB* req,
                                  ReadResponsePB* resp,
                                  rpc::RpcContext* context,
                                  std::shared_ptr<tablet::AbstractTablet>* tablet);

  template<class Req, class Resp>
  bool PrepareModify(const Req& req,
                     Resp* resp,
                     rpc::RpcContext* context,
                     tablet::TabletPeerPtr* tablet_peer,
                     tablet::TabletPtr* tablet);

  // Read implementation. If restart is required returns restart time, in case of success
  // returns invalid ReadHybridTime. Otherwise returns error status.
  Result<ReadHybridTime> DoRead(tablet::AbstractTablet* tablet,
                                const ReadRequestPB* req,
                                ReadHybridTime read_time,
                                HybridTime safe_ht_to_read,
                                tablet::RequireLease require_lease,
                                HostPortPB* hostPortPB,
                                ReadResponsePB* resp,
                                rpc::RpcContext* context);

  TabletServerIf *const server_;
};

class TabletServiceAdminImpl : public TabletServerAdminServiceIf {
 public:
  explicit TabletServiceAdminImpl(TabletServer* server);
  virtual void CreateTablet(const CreateTabletRequestPB* req,
                            CreateTabletResponsePB* resp,
                            rpc::RpcContext context) override;

  virtual void DeleteTablet(const DeleteTabletRequestPB* req,
                            DeleteTabletResponsePB* resp,
                            rpc::RpcContext context) override;

  virtual void AlterSchema(const AlterSchemaRequestPB* req,
                           AlterSchemaResponsePB* resp,
                           rpc::RpcContext context) override;

  virtual void CopartitionTable(const CopartitionTableRequestPB* req,
                                CopartitionTableResponsePB* resp,
                                rpc::RpcContext context) override;

 private:
  TabletServer* server_;
};

class ConsensusServiceImpl : public consensus::ConsensusServiceIf {
 public:
  ConsensusServiceImpl(const scoped_refptr<MetricEntity>& metric_entity,
                       TabletPeerLookupIf* tablet_manager_);

  virtual ~ConsensusServiceImpl();

  virtual void UpdateConsensus(const consensus::ConsensusRequestPB *req,
                               consensus::ConsensusResponsePB *resp,
                               rpc::RpcContext context) override;

  virtual void RequestConsensusVote(const consensus::VoteRequestPB* req,
                                    consensus::VoteResponsePB* resp,
                                    rpc::RpcContext context) override;

  virtual void ChangeConfig(const consensus::ChangeConfigRequestPB* req,
                            consensus::ChangeConfigResponsePB* resp,
                            rpc::RpcContext context) override;

  virtual void GetNodeInstance(const consensus::GetNodeInstanceRequestPB* req,
                               consensus::GetNodeInstanceResponsePB* resp,
                               rpc::RpcContext context) override;

  virtual void RunLeaderElection(const consensus::RunLeaderElectionRequestPB* req,
                                 consensus::RunLeaderElectionResponsePB* resp,
                                 rpc::RpcContext context) override;

  virtual void LeaderElectionLost(const consensus::LeaderElectionLostRequestPB *req,
                                  consensus::LeaderElectionLostResponsePB *resp,
                                  ::yb::rpc::RpcContext context) override;

  virtual void LeaderStepDown(const consensus::LeaderStepDownRequestPB* req,
                              consensus::LeaderStepDownResponsePB* resp,
                              rpc::RpcContext context) override;

  virtual void GetLastOpId(const consensus::GetLastOpIdRequestPB *req,
                           consensus::GetLastOpIdResponsePB *resp,
                           rpc::RpcContext context) override;

  virtual void GetConsensusState(const consensus::GetConsensusStateRequestPB *req,
                                 consensus::GetConsensusStateResponsePB *resp,
                                 rpc::RpcContext context) override;

  virtual void StartRemoteBootstrap(const consensus::StartRemoteBootstrapRequestPB* req,
                                    consensus::StartRemoteBootstrapResponsePB* resp,
                                    rpc::RpcContext context) override;

 private:
  TabletPeerLookupIf* tablet_manager_;
};

}  // namespace tserver
}  // namespace yb

#endif  // YB_TSERVER_TABLET_SERVICE_H_
