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

#include "kudu/master/master_service.h"

#include <gflags/gflags.h>
#include <memory>
#include <string>
#include <vector>

#include "kudu/common/wire_protocol.h"
#include "kudu/master/catalog_manager.h"
#include "kudu/master/master.h"
#include "kudu/master/ts_descriptor.h"
#include "kudu/master/ts_manager.h"
#include "kudu/rpc/rpc_context.h"
#include "kudu/server/webserver.h"
#include "kudu/util/flag_tags.h"


DEFINE_int32(master_inject_latency_on_tablet_lookups_ms, 0,
             "Number of milliseconds that the master will sleep before responding to "
             "requests for tablet locations.");
TAG_FLAG(master_inject_latency_on_tablet_lookups_ms, unsafe);
TAG_FLAG(master_inject_latency_on_tablet_lookups_ms, hidden);

namespace kudu {
namespace master {

using consensus::RaftPeerPB;
using std::string;
using std::vector;
using std::shared_ptr;

namespace {

template<class RespClass>
bool CheckCatalogManagerInitializedOrRespond(Master* master,
                                             RespClass* resp,
                                             rpc::RpcContext* rpc) {
  if (PREDICT_FALSE(!master->catalog_manager()->IsInitialized())) {
    SetupErrorAndRespond(resp->mutable_error(),
                         Status::ServiceUnavailable("catalog manager has not been initialized"),
                         MasterErrorPB::CATALOG_MANAGER_NOT_INITIALIZED,
                         rpc);
    return false;
  }
  return true;
}

template<class RespClass>
bool CheckIsLeaderOrRespond(Master* master,
                            RespClass* resp,
                            rpc::RpcContext* rpc) {
  Status s = master->catalog_manager()->CheckIsLeaderAndReady();
  if (PREDICT_FALSE(!s.ok())) {
    SetupErrorAndRespond(resp->mutable_error(), s,
                         MasterErrorPB::NOT_THE_LEADER,
                         rpc);
    return false;
  }
  return true;
}

template<class RespClass>
bool CheckLeaderAndCatalogManagerInitializedOrRespond(Master* master,
                                                      RespClass* resp,
                                                      rpc::RpcContext* rpc) {
  return PREDICT_TRUE(CheckCatalogManagerInitializedOrRespond(master, resp, rpc) &&
                      CheckIsLeaderOrRespond(master, resp, rpc));
}

// If 's' is not OK and 'resp' has no application specific error set,
// set the error field of 'resp' to match 's' and set the code to
// UNKNOWN_ERROR.
template<class RespClass>
void CheckRespErrorOrSetUnknown(const Status& s, RespClass* resp) {
  if (PREDICT_FALSE(!s.ok() && !resp->has_error())) {
    StatusToPB(s, resp->mutable_error()->mutable_status());
    resp->mutable_error()->set_code(MasterErrorPB::UNKNOWN_ERROR);
  }
}

} // anonymous namespace

static void SetupErrorAndRespond(MasterErrorPB* error,
                                 const Status& s,
                                 MasterErrorPB::Code code,
                                 rpc::RpcContext* rpc) {
  StatusToPB(s, error->mutable_status());
  error->set_code(code);
  // TODO RespondSuccess() is better called 'Respond'.
  rpc->RespondSuccess();
}


MasterServiceImpl::MasterServiceImpl(Master* server)
  : MasterServiceIf(server->metric_entity()),
    server_(server) {
}

void MasterServiceImpl::Ping(const PingRequestPB* req,
                             PingResponsePB* resp,
                             rpc::RpcContext* rpc) {
  rpc->RespondSuccess();
}

void MasterServiceImpl::TSHeartbeat(const TSHeartbeatRequestPB* req,
                                    TSHeartbeatResponsePB* resp,
                                    rpc::RpcContext* rpc) {
  // If CatalogManager is not initialized don't even know whether
  // or not we will be a leader (so we can't tell whether or not we can
  // accept tablet reports).
  if (!CheckCatalogManagerInitializedOrRespond(server_, resp, rpc)) {
    return;
  }

  resp->mutable_master_instance()->CopyFrom(server_->instance_pb());
  Status s = server_->catalog_manager()->CheckIsLeaderAndReady();
  if (!s.ok()) {
    // For the time being, ignore heartbeats sent to non-leader distributed
    // masters.
    //
    // TODO KUDU-493 Allow all master processes to receive heartbeat
    // information: by having the TabletServers send heartbeats to all
    // masters, or by storing heartbeat information in a replicated
    // SysTable.
    LOG(WARNING) << "Received a heartbeat, but this Master instance is not a leader or a "
                 << "single Master: " << s.ToString();
    resp->set_leader_master(false);
    rpc->RespondSuccess();
    return;
  }
  resp->set_leader_master(true);

  shared_ptr<TSDescriptor> ts_desc;
  // If the TS is registering, register in the TS manager.
  if (req->has_registration()) {
    Status s = server_->ts_manager()->RegisterTS(req->common().ts_instance(),
                                                 req->registration(),
                                                 &ts_desc);
    if (!s.ok()) {
      LOG(WARNING) << "Unable to register tablet server (" << rpc->requestor_string() << "): "
                   << s.ToString();
      // TODO: add service-specific errors
      rpc->RespondFailure(s);
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
              << " } as " << rpc->requestor_string()
              << "; Asking this server to re-register.";
    resp->set_needs_reregister(true);
    resp->set_needs_full_tablet_report(true);
    rpc->RespondSuccess();
    return;
  } else if (!s.ok()) {
    LOG(WARNING) << "Unable to look up tablet server for heartbeat request "
                 << req->DebugString() << " from " << rpc->requestor_string()
                 << "\nStatus: " << s.ToString();
    rpc->RespondFailure(s.CloneAndPrepend("Unable to lookup TS"));
    return;
  }

  ts_desc->UpdateHeartbeatTime();
  ts_desc->set_num_live_replicas(req->num_live_tablets());

  if (req->has_tablet_report()) {
    s = server_->catalog_manager()->ProcessTabletReport(
      ts_desc.get(), req->tablet_report(), resp->mutable_tablet_report(), rpc);
    if (!s.ok()) {
      rpc->RespondFailure(s.CloneAndPrepend("Failed to process tablet report"));
      return;
    }
  }

  if (!ts_desc->has_tablet_report()) {
    resp->set_needs_full_tablet_report(true);
  }

  rpc->RespondSuccess();
}

void MasterServiceImpl::GetTabletLocations(const GetTabletLocationsRequestPB* req,
                                           GetTabletLocationsResponsePB* resp,
                                           rpc::RpcContext* rpc) {
  if (!CheckLeaderAndCatalogManagerInitializedOrRespond(server_, resp, rpc)) {
    return;
  }

  if (PREDICT_FALSE(FLAGS_master_inject_latency_on_tablet_lookups_ms > 0)) {
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_master_inject_latency_on_tablet_lookups_ms));
  }

  TSRegistrationPB reg;
  vector<TSDescriptor*> locs;
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

  rpc->RespondSuccess();
}

void MasterServiceImpl::CreateTable(const CreateTableRequestPB* req,
                                    CreateTableResponsePB* resp,
                                    rpc::RpcContext* rpc) {
  if (!CheckLeaderAndCatalogManagerInitializedOrRespond(server_, resp, rpc)) {
    return;
  }

  Status s = server_->catalog_manager()->CreateTable(req, resp, rpc);
  CheckRespErrorOrSetUnknown(s, resp);
  rpc->RespondSuccess();
}

void MasterServiceImpl::IsCreateTableDone(const IsCreateTableDoneRequestPB* req,
                                          IsCreateTableDoneResponsePB* resp,
                                          rpc::RpcContext* rpc) {
  if (!CheckLeaderAndCatalogManagerInitializedOrRespond(server_, resp, rpc)) {
    return;
  }

  Status s = server_->catalog_manager()->IsCreateTableDone(req, resp);
  CheckRespErrorOrSetUnknown(s, resp);
  rpc->RespondSuccess();
}

void MasterServiceImpl::DeleteTable(const DeleteTableRequestPB* req,
                                    DeleteTableResponsePB* resp,
                                    rpc::RpcContext* rpc) {
  if (!CheckLeaderAndCatalogManagerInitializedOrRespond(server_, resp, rpc)) {
    return;
  }

  Status s = server_->catalog_manager()->DeleteTable(req, resp, rpc);
  CheckRespErrorOrSetUnknown(s, resp);
  rpc->RespondSuccess();
}

void MasterServiceImpl::AlterTable(const AlterTableRequestPB* req,
                                   AlterTableResponsePB* resp,
                                   rpc::RpcContext* rpc) {
  if (!CheckLeaderAndCatalogManagerInitializedOrRespond(server_, resp, rpc)) {
    return;
  }

  Status s = server_->catalog_manager()->AlterTable(req, resp, rpc);
  CheckRespErrorOrSetUnknown(s, resp);
  rpc->RespondSuccess();
}

void MasterServiceImpl::IsAlterTableDone(const IsAlterTableDoneRequestPB* req,
                                         IsAlterTableDoneResponsePB* resp,
                                         rpc::RpcContext* rpc) {
  if (!CheckLeaderAndCatalogManagerInitializedOrRespond(server_, resp, rpc)) {
    return;
  }

  Status s = server_->catalog_manager()->IsAlterTableDone(req, resp, rpc);
  CheckRespErrorOrSetUnknown(s, resp);
  rpc->RespondSuccess();
}

void MasterServiceImpl::ListTables(const ListTablesRequestPB* req,
                                   ListTablesResponsePB* resp,
                                   rpc::RpcContext* rpc) {
  if (!CheckLeaderAndCatalogManagerInitializedOrRespond(server_, resp, rpc)) {
    return;
  }

  Status s = server_->catalog_manager()->ListTables(req, resp);
  CheckRespErrorOrSetUnknown(s, resp);
  rpc->RespondSuccess();
}

void MasterServiceImpl::GetTableLocations(const GetTableLocationsRequestPB* req,
                                          GetTableLocationsResponsePB* resp,
                                          rpc::RpcContext* rpc) {
  if (!CheckLeaderAndCatalogManagerInitializedOrRespond(server_, resp, rpc)) {
    return;
  }
  if (PREDICT_FALSE(FLAGS_master_inject_latency_on_tablet_lookups_ms > 0)) {
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_master_inject_latency_on_tablet_lookups_ms));
  }
  Status s = server_->catalog_manager()->GetTableLocations(req, resp);
  CheckRespErrorOrSetUnknown(s, resp);
  rpc->RespondSuccess();
}

void MasterServiceImpl::GetTableSchema(const GetTableSchemaRequestPB* req,
                                       GetTableSchemaResponsePB* resp,
                                       rpc::RpcContext* rpc) {
  if (!CheckLeaderAndCatalogManagerInitializedOrRespond(server_, resp, rpc)) {
    return;
  }

  Status s = server_->catalog_manager()->GetTableSchema(req, resp);
  CheckRespErrorOrSetUnknown(s, resp);
  rpc->RespondSuccess();
}

void MasterServiceImpl::ListTabletServers(const ListTabletServersRequestPB* req,
                                          ListTabletServersResponsePB* resp,
                                          rpc::RpcContext* rpc) {
  if (!CheckLeaderAndCatalogManagerInitializedOrRespond(server_, resp, rpc)) {
    return;
  }

  vector<std::shared_ptr<TSDescriptor> > descs;
  server_->ts_manager()->GetAllDescriptors(&descs);
  for (const std::shared_ptr<TSDescriptor>& desc : descs) {
    ListTabletServersResponsePB::Entry* entry = resp->add_servers();
    desc->GetNodeInstancePB(entry->mutable_instance_id());
    desc->GetRegistration(entry->mutable_registration());
    entry->set_millis_since_heartbeat(desc->TimeSinceHeartbeat().ToMilliseconds());
  }
  rpc->RespondSuccess();
}

void MasterServiceImpl::ListMasters(const ListMastersRequestPB* req,
                                    ListMastersResponsePB* resp,
                                    rpc::RpcContext* rpc) {
  vector<ServerEntryPB> masters;
  Status s = server_->ListMasters(&masters);
  if (!s.ok()) {
    StatusToPB(s, resp->mutable_error());
    resp->mutable_error()->set_code(AppStatusPB::UNKNOWN_ERROR);
  } else {
    for (const ServerEntryPB& master : masters) {
      resp->add_masters()->CopyFrom(master);
    }
  }
  rpc->RespondSuccess();
}

void MasterServiceImpl::GetMasterRegistration(const GetMasterRegistrationRequestPB* req,
                                              GetMasterRegistrationResponsePB* resp,
                                              rpc::RpcContext* rpc) {
  // instance_id must always be set in order for status pages to be useful.
  resp->mutable_instance_id()->CopyFrom(server_->instance_pb());
  if (!CheckCatalogManagerInitializedOrRespond(server_, resp, rpc)) {
    return;
  }
  Status s = server_->GetMasterRegistration(resp->mutable_registration());
  CheckRespErrorOrSetUnknown(s, resp);
  resp->set_role(server_->catalog_manager()->Role());
  rpc->RespondSuccess();
}

} // namespace master
} // namespace kudu
