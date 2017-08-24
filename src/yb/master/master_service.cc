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

#include "yb/master/master_service.h"

#include <memory>
#include <string>
#include <vector>
#include <gflags/gflags.h>

#include "yb/common/wire_protocol.h"
#include "yb/master/master_service_base-internal.h"
#include "yb/master/master.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"
#include "yb/server/webserver.h"
#include "yb/util/flag_tags.h"

DEFINE_int32(master_inject_latency_on_tablet_lookups_ms, 0,
             "Number of milliseconds that the master will sleep before responding to "
             "requests for tablet locations.");
TAG_FLAG(master_inject_latency_on_tablet_lookups_ms, unsafe);
TAG_FLAG(master_inject_latency_on_tablet_lookups_ms, hidden);

namespace yb {
namespace master {

using std::string;
using std::vector;
using std::shared_ptr;
using consensus::RaftPeerPB;
using rpc::RpcContext;

static void SetupErrorAndRespond(MasterErrorPB* error,
                                 const Status& s,
                                 MasterErrorPB::Code code,
                                 RpcContext* rpc) {
  StatusToPB(s, error->mutable_status());
  error->set_code(code);
  rpc->RespondSuccess();
}

MasterServiceImpl::MasterServiceImpl(Master* server)
  : MasterServiceIf(server->metric_entity()),
    MasterServiceBase(server) {
}

void MasterServiceImpl::TSHeartbeat(const TSHeartbeatRequestPB* req,
                                    TSHeartbeatResponsePB* resp,
                                    RpcContext rpc) {
  // If CatalogManager is not initialized don't even know whether
  // or not we will be a leader (so we can't tell whether or not we can
  // accept tablet reports).
  CatalogManager::ScopedLeaderSharedLock l(server_->catalog_manager());
  if (!l.CheckIsInitializedOrRespond(resp, &rpc)) {
    return;
  }

  resp->mutable_master_instance()->CopyFrom(server_->instance_pb());
  if (!l.leader_status().ok()) {
    // For the time being, ignore heartbeats sent to non-leader distributed
    // masters.
    //
    // TODO KUDU-493 Allow all master processes to receive heartbeat
    // information: by having the TabletServers send heartbeats to all
    // masters, or by storing heartbeat information in a replicated
    // SysTable.
    LOG(WARNING) << "Received a heartbeat, but this Master instance is not a leader or a "
                 << "single Master: " << l.leader_status().ToString();
    resp->set_leader_master(false);
    rpc.RespondSuccess();
    return;
  }
  resp->set_leader_master(true);

  consensus::ConsensusStatePB cpb;
  Status s = server_->catalog_manager()->GetCurrentConfig(&cpb);
  if (!s.ok()) {
    // For now, we skip setting the config on errors (hopefully next heartbeat will work).
    // We could enhance to fail rpc, if there are too many error, on a case by case error basis.
    LOG(WARNING) << "Could not set master raft config : " << s.ToString();
  } else if (cpb.has_config()) {
    if (cpb.config().opid_index() > req->config_index()) {
      *resp->mutable_master_config() = std::move(cpb.config());
      LOG(INFO) << "Set config at index " << resp->master_config().opid_index() << " for ts uuid "
                << req->common().ts_instance().permanent_uuid();
    }
  } // Do nothing if config not ready.

  shared_ptr<TSDescriptor> ts_desc;
  // If the TS is registering, register in the TS manager.
  if (req->has_registration()) {
    Status s = server_->ts_manager()->RegisterTS(req->common().ts_instance(),
                                                 req->registration(),
                                                 &ts_desc);
    if (!s.ok()) {
      LOG(WARNING) << "Unable to register tablet server (" << rpc.requestor_string() << "): "
                   << s.ToString();
      // TODO: add service-specific errors
      rpc.RespondFailure(s);
      return;
    }
  }

  // TODO: KUDU-86 if something fails after this point the TS will not be able
  //       to register again.

  // Look up the TS -- if it just registered above, it will be found here.
  // This allows the TS to register and tablet-report in the same RPC.
  s = server_->ts_manager()->LookupTS(req->common().ts_instance(), &ts_desc);
  if (s.IsNotFound()) {
    LOG(INFO) << "Got heartbeat from  unknown tablet server { "
              << req->common().ts_instance().ShortDebugString()
              << " } as " << rpc.requestor_string()
              << "; Asking this server to re-register.";
    resp->set_needs_reregister(true);
    resp->set_needs_full_tablet_report(true);
    rpc.RespondSuccess();
    return;
  } else if (!s.ok()) {
    LOG(WARNING) << "Unable to look up tablet server for heartbeat request "
                 << req->DebugString() << " from " << rpc.requestor_string()
                 << "\nStatus: " << s.ToString();
    rpc.RespondFailure(s.CloneAndPrepend("Unable to lookup TS"));
    return;
  }

  ts_desc->UpdateHeartbeatTime();
  ts_desc->set_num_live_replicas(req->num_live_tablets());

  if (req->has_tablet_report()) {
    s = server_->catalog_manager()->ProcessTabletReport(
      ts_desc.get(), req->tablet_report(), resp->mutable_tablet_report(), &rpc);
    if (!s.ok()) {
      rpc.RespondFailure(s.CloneAndPrepend("Failed to process tablet report"));
      return;
    }
  }

  if (!ts_desc->has_tablet_report()) {
    resp->set_needs_full_tablet_report(true);
  }

  // Retrieve all the nodes known by the master.
  std::vector<std::shared_ptr<TSDescriptor> > descs;
  server_->ts_manager()->GetAllLiveDescriptors(&descs);
  for (const auto& desc : descs) {
    desc->GetTSInformationPB(resp->add_tservers());
  }

  rpc.RespondSuccess();
}

void MasterServiceImpl::GetTabletLocations(const GetTabletLocationsRequestPB* req,
                                           GetTabletLocationsResponsePB* resp,
                                           RpcContext rpc) {
  CatalogManager::ScopedLeaderSharedLock l(server_->catalog_manager());
  if (!l.CheckIsInitializedAndIsLeaderOrRespond(resp, &rpc)) {
    return;
  }

  if (PREDICT_FALSE(FLAGS_master_inject_latency_on_tablet_lookups_ms > 0)) {
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_master_inject_latency_on_tablet_lookups_ms));
  }

  std::vector<TSDescriptor*> locs;
  for (const string& tablet_id : req->tablet_ids()) {
    // TODO: once we have catalog data. ACL checks would also go here, probably.
    TabletLocationsPB* locs_pb = resp->add_tablet_locations();
    Status s = server_->catalog_manager()->GetTabletLocations(tablet_id, locs_pb);
    if (!s.ok()) {
      resp->mutable_tablet_locations()->RemoveLast();

      GetTabletLocationsResponsePB::Error* err = resp->add_errors();
      err->set_tablet_id(tablet_id);
      StatusToPB(s, err->mutable_status());
    }
  }

  rpc.RespondSuccess();
}

void MasterServiceImpl::CreateTable(const CreateTableRequestPB* req,
                                    CreateTableResponsePB* resp,
                                    RpcContext rpc) {
  HandleIn(req, resp, &rpc, &CatalogManager::CreateTable);
}

void MasterServiceImpl::IsCreateTableDone(const IsCreateTableDoneRequestPB* req,
                                          IsCreateTableDoneResponsePB* resp,
                                          RpcContext rpc) {
  HandleIn(req, resp, &rpc, &CatalogManager::IsCreateTableDone);
}

void MasterServiceImpl::DeleteTable(const DeleteTableRequestPB* req,
                                    DeleteTableResponsePB* resp,
                                    RpcContext rpc) {
  HandleIn(req, resp, &rpc, &CatalogManager::DeleteTable);
}

void MasterServiceImpl::IsDeleteTableDone(const IsDeleteTableDoneRequestPB* req,
                                          IsDeleteTableDoneResponsePB* resp,
                                          RpcContext rpc) {
  HandleIn(req, resp, &rpc, &CatalogManager::IsDeleteTableDone);
}

void MasterServiceImpl::AlterTable(const AlterTableRequestPB* req,
                                   AlterTableResponsePB* resp,
                                   RpcContext rpc) {
  HandleIn(req, resp, &rpc, &CatalogManager::AlterTable);
}

void MasterServiceImpl::IsAlterTableDone(const IsAlterTableDoneRequestPB* req,
                                         IsAlterTableDoneResponsePB* resp,
                                         RpcContext rpc) {
  HandleIn(req, resp, &rpc, &CatalogManager::IsAlterTableDone);
}

void MasterServiceImpl::ListTables(const ListTablesRequestPB* req,
                                   ListTablesResponsePB* resp,
                                   RpcContext rpc) {
  HandleIn(req, resp, &rpc, &CatalogManager::ListTables);
}

void MasterServiceImpl::GetTableLocations(const GetTableLocationsRequestPB* req,
                                          GetTableLocationsResponsePB* resp,
                                          RpcContext rpc) {
  HandleOnLeader(req, resp, &rpc, [&]() -> Status {
    if (PREDICT_FALSE(FLAGS_master_inject_latency_on_tablet_lookups_ms > 0)) {
      SleepFor(MonoDelta::FromMilliseconds(FLAGS_master_inject_latency_on_tablet_lookups_ms));
    }
    return server_->catalog_manager()->GetTableLocations(req, resp); });
}

void MasterServiceImpl::GetTableSchema(const GetTableSchemaRequestPB* req,
                                       GetTableSchemaResponsePB* resp,
                                       RpcContext rpc) {
  HandleIn(req, resp, &rpc, &CatalogManager::GetTableSchema);
}

void MasterServiceImpl::CreateNamespace(const CreateNamespaceRequestPB* req,
                                        CreateNamespaceResponsePB* resp,
                                        RpcContext rpc) {
  HandleIn(req, resp, &rpc, &CatalogManager::CreateNamespace);
}

void MasterServiceImpl::DeleteNamespace(const DeleteNamespaceRequestPB* req,
                                        DeleteNamespaceResponsePB* resp,
                                        RpcContext rpc) {
  HandleIn(req, resp, &rpc, &CatalogManager::DeleteNamespace);
}

void MasterServiceImpl::ListNamespaces(const ListNamespacesRequestPB* req,
                                       ListNamespacesResponsePB* resp,
                                       RpcContext rpc) {
  HandleIn(req, resp, &rpc, &CatalogManager::ListNamespaces);
}

void MasterServiceImpl::CreateUDType(const CreateUDTypeRequestPB* req,
                                        CreateUDTypeResponsePB* resp,
                                        rpc::RpcContext rpc) {
  HandleIn(req, resp, &rpc, &CatalogManager::CreateUDType);
}

void MasterServiceImpl::DeleteUDType(const DeleteUDTypeRequestPB* req,
                                        DeleteUDTypeResponsePB* resp,
                                        rpc::RpcContext rpc) {
  HandleIn(req, resp, &rpc, &CatalogManager::DeleteUDType);
}

void MasterServiceImpl::ListUDTypes(const ListUDTypesRequestPB* req,
                                       ListUDTypesResponsePB* resp,
                                       rpc::RpcContext rpc) {
  HandleIn(req, resp, &rpc, &CatalogManager::ListUDTypes);
}

void MasterServiceImpl::GetUDTypeInfo(const GetUDTypeInfoRequestPB* req,
                                            GetUDTypeInfoResponsePB* resp,
                                            rpc::RpcContext rpc) {
  HandleIn(req, resp, &rpc, &CatalogManager::GetUDTypeInfo);
}

void MasterServiceImpl::ListTabletServers(const ListTabletServersRequestPB* req,
                                          ListTabletServersResponsePB* resp,
                                          RpcContext rpc) {
  CatalogManager::ScopedLeaderSharedLock l(server_->catalog_manager());
  if (!l.CheckIsInitializedAndIsLeaderOrRespond(resp, &rpc)) {
    return;
  }

  std::vector<std::shared_ptr<TSDescriptor> > descs;
  server_->ts_manager()->GetAllDescriptors(&descs);
  for (const std::shared_ptr<TSDescriptor>& desc : descs) {
    ListTabletServersResponsePB::Entry* entry = resp->add_servers();
    desc->GetNodeInstancePB(entry->mutable_instance_id());
    desc->GetRegistration(entry->mutable_registration());
    entry->set_millis_since_heartbeat(desc->TimeSinceHeartbeat().ToMilliseconds());
  }
  rpc.RespondSuccess();
}

void MasterServiceImpl::ListMasters(
    const ListMastersRequestPB* req,
    ListMastersResponsePB* resp,
    RpcContext rpc) {
  std::vector<ServerEntryPB> masters;
  Status s = server_->ListMasters(&masters);
  if (s.ok()) {
    for (const ServerEntryPB& master : masters) {
      resp->add_masters()->CopyFrom(master);
    }
    rpc.RespondSuccess();
  } else {
    SetupErrorAndRespond(resp->mutable_error(), s, MasterErrorPB_Code_UNKNOWN_ERROR, &rpc);
  }
}

void MasterServiceImpl::ListMasterRaftPeers(
    const ListMasterRaftPeersRequestPB* req,
    ListMasterRaftPeersResponsePB* resp,
    RpcContext rpc) {
  std::vector<RaftPeerPB> masters;
  Status s = server_->ListRaftConfigMasters(&masters);
  if (s.ok()) {
    for (const RaftPeerPB& master : masters) {
      resp->add_masters()->CopyFrom(master);
    }
    rpc.RespondSuccess();
  } else {
    SetupErrorAndRespond(resp->mutable_error(), s, MasterErrorPB_Code_UNKNOWN_ERROR, &rpc);
  }
}

void MasterServiceImpl::GetMasterRegistration(const GetMasterRegistrationRequestPB* req,
                                              GetMasterRegistrationResponsePB* resp,
                                              RpcContext rpc) {
  // instance_id must always be set in order for status pages to be useful.
  resp->mutable_instance_id()->CopyFrom(server_->instance_pb());
  CatalogManager::ScopedLeaderSharedLock l(server_->catalog_manager());
  if (!l.CheckIsInitializedOrRespond(resp, &rpc)) {
    return;
  }
  Status s = server_->GetMasterRegistration(resp->mutable_registration());
  CheckRespErrorOrSetUnknown(s, resp);
  resp->set_role(server_->catalog_manager()->Role());
  rpc.RespondSuccess();
}

void MasterServiceImpl::DumpState(
    const DumpMasterStateRequestPB* req,
    DumpMasterStateResponsePB* resp,
    RpcContext rpc) {
  CatalogManager::ScopedLeaderSharedLock l(server_->catalog_manager());
  if (!l.CheckIsInitializedOrRespond(resp, &rpc)) {
    return;
  }

  server_->catalog_manager()->DumpState(&LOG(INFO), req->on_disk());

  if (req->has_peers_also() && req->peers_also()) {
    std::vector<RaftPeerPB> masters_raft;
    Status s = server_->ListRaftConfigMasters(&masters_raft);
    CheckRespErrorOrSetUnknown(s, resp);

    if (!s.ok())
      return;

    LOG(INFO) << "Sending dump command to " << masters_raft.size()-1 << " peers.";

    // Remove our entry before broadcasting to all peers
    std::vector<RaftPeerPB>::iterator it;
    bool found = false;
    for (it = masters_raft.begin(); it != masters_raft.end(); it++) {
      RaftPeerPB peerPB = *it;
      if (server_->instance_pb().permanent_uuid() == peerPB.permanent_uuid()) {
        found = true;
        break;
      }
    }

    if (!found) {
      CHECK(found) << "Did not find leader in raft config";
    }

    masters_raft.erase(it);

    s = server_->catalog_manager()->PeerStateDump(masters_raft, req->on_disk());
    CheckRespErrorOrSetUnknown(s, resp);
  }

  rpc.RespondSuccess();
}

void MasterServiceImpl::RemovedMasterUpdate(const RemovedMasterUpdateRequestPB* req,
                                            RemovedMasterUpdateResponsePB* resp,
                                            RpcContext rpc) {
  CatalogManager::ScopedLeaderSharedLock l(server_->catalog_manager());
  if (!l.CheckIsInitializedOrRespond(resp, &rpc)) {
    return;
  }

  Status s = server_->GoIntoShellMode();
  CheckRespErrorOrSetUnknown(s, resp);
  rpc.RespondSuccess();
}

void MasterServiceImpl::ChangeLoadBalancerState(
    const ChangeLoadBalancerStateRequestPB* req, ChangeLoadBalancerStateResponsePB* resp,
    RpcContext rpc) {
  CatalogManager::ScopedLeaderSharedLock l(server_->catalog_manager());
  if (!l.CheckIsInitializedAndIsLeaderOrRespond(resp, &rpc)) {
    return;
  }

  if (req->has_is_enabled()) {
    LOG(INFO) << "Changing balancer state to " << req->is_enabled();
    server_->catalog_manager()->SetLoadBalancerEnabled(req->is_enabled());
  }

  rpc.RespondSuccess();
}

void MasterServiceImpl::GetMasterClusterConfig(
    const GetMasterClusterConfigRequestPB* req, GetMasterClusterConfigResponsePB* resp,
    RpcContext rpc) {
  // Explicit instantiation here because the handler method has a few overloadings.
  HandleIn<CatalogManager>(req, resp, &rpc, &CatalogManager::GetClusterConfig);
}

void MasterServiceImpl::ChangeMasterClusterConfig(
    const ChangeMasterClusterConfigRequestPB* req, ChangeMasterClusterConfigResponsePB* resp,
    RpcContext rpc) {
  HandleIn(req, resp, &rpc, &CatalogManager::SetClusterConfig);
}

void MasterServiceImpl::GetLoadMoveCompletion(
    const GetLoadMovePercentRequestPB* req, GetLoadMovePercentResponsePB* resp,
    RpcContext rpc) {
  HandleIn(req, resp, &rpc, &CatalogManager::GetLoadMoveCompletionPercent);
}

void MasterServiceImpl::IsMasterLeaderServiceReady(
    const IsMasterLeaderReadyRequestPB* req, IsMasterLeaderReadyResponsePB* resp,
    RpcContext rpc) {
  CatalogManager::ScopedLeaderSharedLock l(server_->catalog_manager());
  if (!l.CheckIsInitializedAndIsLeaderOrRespond(resp, &rpc)) {
    return;
  }

  rpc.RespondSuccess();
}

void MasterServiceImpl::IsLoadBalanced(
    const IsLoadBalancedRequestPB* req, IsLoadBalancedResponsePB* resp,
    RpcContext rpc) {
  HandleIn(req, resp, &rpc, &CatalogManager::IsLoadBalanced);
}

} // namespace master
} // namespace yb
