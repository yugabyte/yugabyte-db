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
#include "kudu/integration-tests/cluster_itest_util.h"

#include <algorithm>
#include <boost/optional.hpp>
#include <glog/stl_logging.h>
#include <limits>

#include "kudu/client/client.h"
#include "kudu/common/wire_protocol.h"
#include "kudu/common/wire_protocol.pb.h"
#include "kudu/common/wire_protocol-test-util.h"
#include "kudu/consensus/consensus.proxy.h"
#include "kudu/consensus/opid_util.h"
#include "kudu/consensus/quorum_util.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/strings/join.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/master/master.proxy.h"
#include "kudu/rpc/rpc_controller.h"
#include "kudu/server/server_base.proxy.h"
#include "kudu/tserver/tablet_server_test_util.h"
#include "kudu/tserver/tserver_admin.proxy.h"
#include "kudu/tserver/tserver_service.pb.h"
#include "kudu/tserver/tserver_service.proxy.h"
#include "kudu/util/net/net_util.h"

namespace kudu {
namespace itest {

using client::KuduClient;
using client::KuduSchema;
using client::KuduSchemaBuilder;
using client::KuduTable;
using consensus::CONSENSUS_CONFIG_ACTIVE;
using consensus::CONSENSUS_CONFIG_COMMITTED;
using consensus::ChangeConfigRequestPB;
using consensus::ChangeConfigResponsePB;
using consensus::ConsensusConfigType;
using consensus::ConsensusStatePB;
using consensus::CountVoters;
using consensus::GetConsensusStateRequestPB;
using consensus::GetConsensusStateResponsePB;
using consensus::GetLastOpIdRequestPB;
using consensus::GetLastOpIdResponsePB;
using consensus::LeaderStepDownRequestPB;
using consensus::LeaderStepDownResponsePB;
using consensus::OpId;
using consensus::RaftPeerPB;
using consensus::RunLeaderElectionResponsePB;
using consensus::RunLeaderElectionRequestPB;
using consensus::kInvalidOpIdIndex;
using master::ListTabletServersResponsePB;
using master::MasterServiceProxy;
using master::TabletLocationsPB;
using rpc::Messenger;
using rpc::RpcController;
using std::min;
using std::shared_ptr;
using std::string;
using std::unordered_map;
using std::vector;
using strings::Substitute;
using tserver::CreateTsClientProxies;
using tserver::ListTabletsResponsePB;
using tserver::DeleteTabletRequestPB;
using tserver::DeleteTabletResponsePB;
using tserver::TabletServerAdminServiceProxy;
using tserver::TabletServerErrorPB;
using tserver::TabletServerServiceProxy;
using tserver::WriteRequestPB;
using tserver::WriteResponsePB;

const string& TServerDetails::uuid() const {
  return instance_id.permanent_uuid();
}

string TServerDetails::ToString() const {
  return Substitute("TabletServer: $0, Rpc address: $1",
                    instance_id.permanent_uuid(),
                    registration.rpc_addresses(0).ShortDebugString());
}

client::KuduSchema SimpleIntKeyKuduSchema() {
  KuduSchema s;
  KuduSchemaBuilder b;
  b.AddColumn("key")->Type(client::KuduColumnSchema::INT32)->NotNull()->PrimaryKey();
  CHECK_OK(b.Build(&s));
  return s;
}

Status GetLastOpIdForEachReplica(const string& tablet_id,
                                 const vector<TServerDetails*>& replicas,
                                 consensus::OpIdType opid_type,
                                 const MonoDelta& timeout,
                                 vector<OpId>* op_ids) {
  GetLastOpIdRequestPB opid_req;
  GetLastOpIdResponsePB opid_resp;
  opid_req.set_tablet_id(tablet_id);
  RpcController controller;

  op_ids->clear();
  for (TServerDetails* ts : replicas) {
    controller.Reset();
    controller.set_timeout(timeout);
    opid_resp.Clear();
    opid_req.set_dest_uuid(ts->uuid());
    opid_req.set_tablet_id(tablet_id);
    opid_req.set_opid_type(opid_type);
    RETURN_NOT_OK_PREPEND(
      ts->consensus_proxy->GetLastOpId(opid_req, &opid_resp, &controller),
      Substitute("Failed to fetch last op id from $0",
                 ts->instance_id.ShortDebugString()));
    op_ids->push_back(opid_resp.opid());
  }

  return Status::OK();
}

Status GetLastOpIdForReplica(const std::string& tablet_id,
                             TServerDetails* replica,
                             consensus::OpIdType opid_type,
                             const MonoDelta& timeout,
                             consensus::OpId* op_id) {
  vector<TServerDetails*> replicas;
  replicas.push_back(replica);
  vector<OpId> op_ids;
  RETURN_NOT_OK(GetLastOpIdForEachReplica(tablet_id, replicas, opid_type, timeout, &op_ids));
  CHECK_EQ(1, op_ids.size());
  *op_id = op_ids[0];
  return Status::OK();
}

Status WaitForServersToAgree(const MonoDelta& timeout,
                             const TabletServerMap& tablet_servers,
                             const string& tablet_id,
                             int64_t minimum_index) {
  MonoTime now = MonoTime::Now(MonoTime::COARSE);
  MonoTime deadline = now;
  deadline.AddDelta(timeout);

  for (int i = 1; now.ComesBefore(deadline); i++) {
    vector<TServerDetails*> servers;
    AppendValuesFromMap(tablet_servers, &servers);
    vector<OpId> ids;
    Status s = GetLastOpIdForEachReplica(tablet_id, servers, consensus::RECEIVED_OPID, timeout,
                                         &ids);
    if (s.ok()) {
      bool any_behind = false;
      bool any_disagree = false;
      int64_t cur_index = kInvalidOpIdIndex;
      for (const OpId& id : ids) {
        if (cur_index == kInvalidOpIdIndex) {
          cur_index = id.index();
        }
        if (id.index() != cur_index) {
          any_disagree = true;
          break;
        }
        if (id.index() < minimum_index) {
          any_behind = true;
          break;
        }
      }
      if (!any_behind && !any_disagree) {
        return Status::OK();
      }
    } else {
      LOG(WARNING) << "Got error getting last opid for each replica: " << s.ToString();
    }

    LOG(INFO) << "Not converged past " << minimum_index << " yet: " << ids;
    SleepFor(MonoDelta::FromMilliseconds(min(i * 100, 1000)));

    now = MonoTime::Now(MonoTime::COARSE);
  }
  return Status::TimedOut(Substitute("Index $0 not available on all replicas after $1. ",
                                              minimum_index, timeout.ToString()));
}

// Wait until all specified replicas have logged the given index.
Status WaitUntilAllReplicasHaveOp(const int64_t log_index,
                                  const string& tablet_id,
                                  const vector<TServerDetails*>& replicas,
                                  const MonoDelta& timeout) {
  MonoTime start = MonoTime::Now(MonoTime::FINE);
  MonoDelta passed = MonoDelta::FromMilliseconds(0);
  while (true) {
    vector<OpId> op_ids;
    Status s = GetLastOpIdForEachReplica(tablet_id, replicas, consensus::RECEIVED_OPID, timeout,
                                         &op_ids);
    if (s.ok()) {
      bool any_behind = false;
      for (const OpId& op_id : op_ids) {
        if (op_id.index() < log_index) {
          any_behind = true;
          break;
        }
      }
      if (!any_behind) return Status::OK();
    } else {
      LOG(WARNING) << "Got error getting last opid for each replica: " << s.ToString();
    }
    passed = MonoTime::Now(MonoTime::FINE).GetDeltaSince(start);
    if (passed.MoreThan(timeout)) {
      break;
    }
    SleepFor(MonoDelta::FromMilliseconds(50));
  }
  string replicas_str;
  for (const TServerDetails* replica : replicas) {
    if (!replicas_str.empty()) replicas_str += ", ";
    replicas_str += "{ " + replica->ToString() + " }";
  }
  return Status::TimedOut(Substitute("Index $0 not available on all replicas after $1. "
                                              "Replicas: [ $2 ]",
                                              log_index, passed.ToString()));
}

Status CreateTabletServerMap(MasterServiceProxy* master_proxy,
                             const shared_ptr<Messenger>& messenger,
                             unordered_map<string, TServerDetails*>* ts_map) {
  master::ListTabletServersRequestPB req;
  master::ListTabletServersResponsePB resp;
  rpc::RpcController controller;

  RETURN_NOT_OK(master_proxy->ListTabletServers(req, &resp, &controller));
  RETURN_NOT_OK(controller.status());
  if (resp.has_error()) {
    return Status::RemoteError("Response had an error", resp.error().ShortDebugString());
  }

  ts_map->clear();
  for (const ListTabletServersResponsePB::Entry& entry : resp.servers()) {
    HostPort host_port;
    RETURN_NOT_OK(HostPortFromPB(entry.registration().rpc_addresses(0), &host_port));
    vector<Sockaddr> addresses;
    host_port.ResolveAddresses(&addresses);

    gscoped_ptr<TServerDetails> peer(new TServerDetails);
    peer->instance_id.CopyFrom(entry.instance_id());
    peer->registration.CopyFrom(entry.registration());

    CreateTsClientProxies(addresses[0],
                          messenger,
                          &peer->tserver_proxy,
                          &peer->tserver_admin_proxy,
                          &peer->consensus_proxy,
                          &peer->generic_proxy);

    InsertOrDie(ts_map, peer->instance_id.permanent_uuid(), peer.get());
    ignore_result(peer.release());
  }
  return Status::OK();
}

Status GetConsensusState(const TServerDetails* replica,
                         const string& tablet_id,
                         consensus::ConsensusConfigType type,
                         const MonoDelta& timeout,
                         ConsensusStatePB* consensus_state) {
  GetConsensusStateRequestPB req;
  GetConsensusStateResponsePB resp;
  RpcController controller;
  controller.set_timeout(timeout);
  req.set_dest_uuid(replica->uuid());
  req.set_tablet_id(tablet_id);
  req.set_type(type);

  RETURN_NOT_OK(replica->consensus_proxy->GetConsensusState(req, &resp, &controller));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  *consensus_state = resp.cstate();
  return Status::OK();
}

Status WaitUntilCommittedConfigNumVotersIs(int config_size,
                                           const TServerDetails* replica,
                                           const std::string& tablet_id,
                                           const MonoDelta& timeout) {
  MonoTime start = MonoTime::Now(MonoTime::FINE);
  MonoTime deadline = start;
  deadline.AddDelta(timeout);

  int backoff_exp = 0;
  const int kMaxBackoffExp = 7;
  Status s;
  ConsensusStatePB cstate;
  while (true) {
    MonoDelta remaining_timeout = deadline.GetDeltaSince(MonoTime::Now(MonoTime::FINE));
    s = GetConsensusState(replica, tablet_id, CONSENSUS_CONFIG_COMMITTED,
                          remaining_timeout, &cstate);
    if (s.ok()) {
      if (CountVoters(cstate.config()) == config_size) {
        return Status::OK();
      }
    }

    if (MonoTime::Now(MonoTime::FINE).GetDeltaSince(start).MoreThan(timeout)) {
      break;
    }
    SleepFor(MonoDelta::FromMilliseconds(1 << backoff_exp));
    backoff_exp = min(backoff_exp + 1, kMaxBackoffExp);
  }
  return Status::TimedOut(Substitute("Number of voters does not equal $0 after waiting for $1."
                                     "Last consensus state: $2. Last status: $3",
                                     config_size, timeout.ToString(),
                                     cstate.ShortDebugString(), s.ToString()));
}

Status WaitUntilCommittedConfigOpIdIndexIs(int64_t opid_index,
                                           const TServerDetails* replica,
                                           const std::string& tablet_id,
                                           const MonoDelta& timeout) {
  MonoTime start = MonoTime::Now(MonoTime::FINE);
  MonoTime deadline = start;
  deadline.AddDelta(timeout);

  Status s;
  ConsensusStatePB cstate;
  while (true) {
    MonoDelta remaining_timeout = deadline.GetDeltaSince(MonoTime::Now(MonoTime::FINE));
    s = GetConsensusState(replica, tablet_id, CONSENSUS_CONFIG_COMMITTED,
                          remaining_timeout, &cstate);
    if (s.ok() && cstate.config().opid_index() == opid_index) {
      return Status::OK();
    }
    if (MonoTime::Now(MonoTime::FINE).GetDeltaSince(start).MoreThan(timeout)) break;
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  return Status::TimedOut(Substitute("Committed config opid_index does not equal $0 "
                                     "after waiting for $1. "
                                     "Last consensus state: $2. Last status: $3",
                                     opid_index,
                                     MonoTime::Now(MonoTime::FINE).GetDeltaSince(start).ToString(),
                                     cstate.ShortDebugString(), s.ToString()));
}

Status WaitUntilCommittedOpIdIndexIs(int64_t opid_index,
                                     TServerDetails* replica,
                                     const string& tablet_id,
                                     const MonoDelta& timeout) {
  MonoTime start = MonoTime::Now(MonoTime::FINE);
  MonoTime deadline = start;
  deadline.AddDelta(timeout);

  Status s;
  OpId op_id;
  while (true) {
    MonoDelta remaining_timeout = deadline.GetDeltaSince(MonoTime::Now(MonoTime::FINE));
    s = GetLastOpIdForReplica(tablet_id, replica, consensus::COMMITTED_OPID, remaining_timeout,
                              &op_id);
    if (s.ok() && op_id.index() == opid_index) {
      return Status::OK();
    }
    if (MonoTime::Now(MonoTime::FINE).GetDeltaSince(start).MoreThan(timeout)) break;
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  return Status::TimedOut(Substitute("Committed consensus opid_index does not equal $0 "
                                     "after waiting for $1. Last status: $2",
                                     opid_index,
                                     MonoTime::Now(MonoTime::FINE).GetDeltaSince(start).ToString(),
                                     s.ToString()));
}

Status GetReplicaStatusAndCheckIfLeader(const TServerDetails* replica,
                                        const string& tablet_id,
                                        const MonoDelta& timeout) {
  ConsensusStatePB cstate;
  Status s = GetConsensusState(replica, tablet_id, CONSENSUS_CONFIG_ACTIVE,
                               timeout, &cstate);
  if (PREDICT_FALSE(!s.ok())) {
    VLOG(1) << "Error getting consensus state from replica: "
            << replica->instance_id.permanent_uuid();
    return Status::NotFound("Error connecting to replica", s.ToString());
  }
  const string& replica_uuid = replica->instance_id.permanent_uuid();
  if (cstate.has_leader_uuid() && cstate.leader_uuid() == replica_uuid) {
    return Status::OK();
  }
  VLOG(1) << "Replica not leader of config: " << replica->instance_id.permanent_uuid();
  return Status::IllegalState("Replica found but not leader");
}

Status WaitUntilLeader(const TServerDetails* replica,
                       const string& tablet_id,
                       const MonoDelta& timeout) {
  MonoTime start = MonoTime::Now(MonoTime::FINE);
  MonoTime deadline = start;
  deadline.AddDelta(timeout);

  int backoff_exp = 0;
  const int kMaxBackoffExp = 7;
  Status s;
  while (true) {
    MonoDelta remaining_timeout = deadline.GetDeltaSince(MonoTime::Now(MonoTime::FINE));
    s = GetReplicaStatusAndCheckIfLeader(replica, tablet_id, remaining_timeout);
    if (s.ok()) {
      return Status::OK();
    }

    if (MonoTime::Now(MonoTime::FINE).GetDeltaSince(start).MoreThan(timeout)) {
      break;
    }
    SleepFor(MonoDelta::FromMilliseconds(1 << backoff_exp));
    backoff_exp = min(backoff_exp + 1, kMaxBackoffExp);
  }
  return Status::TimedOut(Substitute("Replica $0 is not leader after waiting for $1: $2",
                                     replica->ToString(), timeout.ToString(), s.ToString()));
}

Status FindTabletLeader(const TabletServerMap& tablet_servers,
                        const string& tablet_id,
                        const MonoDelta& timeout,
                        TServerDetails** leader) {
  vector<TServerDetails*> tservers;
  AppendValuesFromMap(tablet_servers, &tservers);

  MonoTime start = MonoTime::Now(MonoTime::FINE);
  MonoTime deadline = start;
  deadline.AddDelta(timeout);
  Status s;
  int i = 0;
  while (true) {
    MonoDelta remaining_timeout = deadline.GetDeltaSince(MonoTime::Now(MonoTime::FINE));
    s = GetReplicaStatusAndCheckIfLeader(tservers[i], tablet_id, remaining_timeout);
    if (s.ok()) {
      *leader = tservers[i];
      return Status::OK();
    }

    if (deadline.ComesBefore(MonoTime::Now(MonoTime::FINE))) break;
    i = (i + 1) % tservers.size();
    if (i == 0) {
      SleepFor(MonoDelta::FromMilliseconds(10));
    }
  }
  return Status::TimedOut(Substitute("Unable to find leader of tablet $0 after $1. "
                                     "Status message: $2", tablet_id,
                                     MonoTime::Now(MonoTime::FINE).GetDeltaSince(start).ToString(),
                                     s.ToString()));
}

Status StartElection(const TServerDetails* replica,
                     const string& tablet_id,
                     const MonoDelta& timeout) {
  RunLeaderElectionRequestPB req;
  req.set_dest_uuid(replica->uuid());
  req.set_tablet_id(tablet_id);
  RunLeaderElectionResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(timeout);
  RETURN_NOT_OK(replica->consensus_proxy->RunLeaderElection(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status())
      .CloneAndPrepend(Substitute("Code $0", TabletServerErrorPB::Code_Name(resp.error().code())));
  }
  return Status::OK();
}

Status LeaderStepDown(const TServerDetails* replica,
                      const string& tablet_id,
                      const MonoDelta& timeout,
                      TabletServerErrorPB* error) {
  LeaderStepDownRequestPB req;
  req.set_dest_uuid(replica->uuid());
  req.set_tablet_id(tablet_id);
  LeaderStepDownResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(timeout);
  RETURN_NOT_OK(replica->consensus_proxy->LeaderStepDown(req, &resp, &rpc));
  if (resp.has_error()) {
    if (error != nullptr) {
      *error = resp.error();
    }
    return StatusFromPB(resp.error().status())
      .CloneAndPrepend(Substitute("Code $0", TabletServerErrorPB::Code_Name(resp.error().code())));
  }
  return Status::OK();
}

Status WriteSimpleTestRow(const TServerDetails* replica,
                          const std::string& tablet_id,
                          RowOperationsPB::Type write_type,
                          int32_t key,
                          int32_t int_val,
                          const string& string_val,
                          const MonoDelta& timeout) {
  WriteRequestPB req;
  WriteResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(timeout);

  req.set_tablet_id(tablet_id);
  Schema schema = GetSimpleTestSchema();
  RETURN_NOT_OK(SchemaToPB(schema, req.mutable_schema()));
  AddTestRowToPB(write_type, schema, key, int_val, string_val, req.mutable_row_operations());

  RETURN_NOT_OK(replica->tserver_proxy->Write(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Status AddServer(const TServerDetails* leader,
                 const std::string& tablet_id,
                 const TServerDetails* replica_to_add,
                 consensus::RaftPeerPB::MemberType member_type,
                 const boost::optional<int64_t>& cas_config_opid_index,
                 const MonoDelta& timeout,
                 TabletServerErrorPB::Code* error_code) {
  ChangeConfigRequestPB req;
  ChangeConfigResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(timeout);

  req.set_dest_uuid(leader->uuid());
  req.set_tablet_id(tablet_id);
  req.set_type(consensus::ADD_SERVER);
  RaftPeerPB* peer = req.mutable_server();
  peer->set_permanent_uuid(replica_to_add->uuid());
  peer->set_member_type(member_type);
  *peer->mutable_last_known_addr() = replica_to_add->registration.rpc_addresses(0);
  if (cas_config_opid_index) {
    req.set_cas_config_opid_index(*cas_config_opid_index);
  }

  RETURN_NOT_OK(leader->consensus_proxy->ChangeConfig(req, &resp, &rpc));
  if (resp.has_error()) {
    if (error_code) *error_code = resp.error().code();
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Status RemoveServer(const TServerDetails* leader,
                    const std::string& tablet_id,
                    const TServerDetails* replica_to_remove,
                    const boost::optional<int64_t>& cas_config_opid_index,
                    const MonoDelta& timeout,
                    TabletServerErrorPB::Code* error_code) {
  ChangeConfigRequestPB req;
  ChangeConfigResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(timeout);

  req.set_dest_uuid(leader->uuid());
  req.set_tablet_id(tablet_id);
  req.set_type(consensus::REMOVE_SERVER);
  if (cas_config_opid_index) {
    req.set_cas_config_opid_index(*cas_config_opid_index);
  }
  RaftPeerPB* peer = req.mutable_server();
  peer->set_permanent_uuid(replica_to_remove->uuid());

  RETURN_NOT_OK(leader->consensus_proxy->ChangeConfig(req, &resp, &rpc));
  if (resp.has_error()) {
    if (error_code) *error_code = resp.error().code();
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Status ListTablets(const TServerDetails* ts,
                   const MonoDelta& timeout,
                   vector<ListTabletsResponsePB::StatusAndSchemaPB>* tablets) {
  tserver::ListTabletsRequestPB req;
  tserver::ListTabletsResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(timeout);

  RETURN_NOT_OK(ts->tserver_proxy->ListTablets(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  tablets->assign(resp.status_and_schema().begin(), resp.status_and_schema().end());
  return Status::OK();
}

Status ListRunningTabletIds(const TServerDetails* ts,
                            const MonoDelta& timeout,
                            vector<string>* tablet_ids) {
  vector<ListTabletsResponsePB::StatusAndSchemaPB> tablets;
  RETURN_NOT_OK(ListTablets(ts, timeout, &tablets));
  tablet_ids->clear();
  for (const ListTabletsResponsePB::StatusAndSchemaPB& t : tablets) {
    if (t.tablet_status().state() == tablet::RUNNING) {
      tablet_ids->push_back(t.tablet_status().tablet_id());
    }
  }
  return Status::OK();
}

Status GetTabletLocations(const shared_ptr<MasterServiceProxy>& master_proxy,
                          const string& tablet_id,
                          const MonoDelta& timeout,
                          master::TabletLocationsPB* tablet_locations) {
  master::GetTabletLocationsResponsePB resp;
  master::GetTabletLocationsRequestPB req;
  *req.add_tablet_ids() = tablet_id;
  rpc::RpcController rpc;
  rpc.set_timeout(timeout);
  RETURN_NOT_OK(master_proxy->GetTabletLocations(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  if (resp.errors_size() > 0) {
    CHECK_EQ(1, resp.errors_size()) << resp.ShortDebugString();
    return StatusFromPB(resp.errors(0).status());
  }
  CHECK_EQ(1, resp.tablet_locations_size()) << resp.ShortDebugString();
  *tablet_locations = resp.tablet_locations(0);
  return Status::OK();
}

Status GetTableLocations(const shared_ptr<MasterServiceProxy>& master_proxy,
                         const string& table_name,
                         const MonoDelta& timeout,
                         master::GetTableLocationsResponsePB* table_locations) {
  master::GetTableLocationsRequestPB req;
  req.mutable_table()->set_table_name(table_name);
  req.set_max_returned_locations(1000);
  rpc::RpcController rpc;
  rpc.set_timeout(timeout);
  RETURN_NOT_OK(master_proxy->GetTableLocations(req, table_locations, &rpc));
  if (table_locations->has_error()) {
    return StatusFromPB(table_locations->error().status());
  }
  return Status::OK();
}

Status WaitForNumVotersInConfigOnMaster(const shared_ptr<MasterServiceProxy>& master_proxy,
                                        const std::string& tablet_id,
                                        int num_voters,
                                        const MonoDelta& timeout) {
  Status s;
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(timeout);
  int num_voters_found = 0;
  while (true) {
    TabletLocationsPB tablet_locations;
    MonoDelta time_remaining = deadline.GetDeltaSince(MonoTime::Now(MonoTime::FINE));
    s = GetTabletLocations(master_proxy, tablet_id, time_remaining, &tablet_locations);
    if (s.ok()) {
      num_voters_found = 0;
      for (const TabletLocationsPB::ReplicaPB& r : tablet_locations.replicas()) {
        if (r.role() == RaftPeerPB::LEADER || r.role() == RaftPeerPB::FOLLOWER) num_voters_found++;
      }
      if (num_voters_found == num_voters) break;
    }
    if (deadline.ComesBefore(MonoTime::Now(MonoTime::FINE))) break;
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  RETURN_NOT_OK(s);
  if (num_voters_found != num_voters) {
    return Status::IllegalState(
        Substitute("Did not find exactly $0 voters, found $1 voters",
                   num_voters, num_voters_found));
  }
  return Status::OK();
}

Status WaitForNumTabletsOnTS(TServerDetails* ts,
                             int count,
                             const MonoDelta& timeout,
                             vector<ListTabletsResponsePB::StatusAndSchemaPB>* tablets) {
  Status s;
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(timeout);
  while (true) {
    s = ListTablets(ts, MonoDelta::FromSeconds(10), tablets);
    if (s.ok() && tablets->size() == count) break;
    if (deadline.ComesBefore(MonoTime::Now(MonoTime::FINE))) break;
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  RETURN_NOT_OK(s);
  if (tablets->size() != count) {
    return Status::IllegalState(
        Substitute("Did not find exactly $0 tablets, found $1 tablets",
                   count, tablets->size()));
  }
  return Status::OK();
}

Status WaitUntilTabletInState(TServerDetails* ts,
                              const std::string& tablet_id,
                              tablet::TabletStatePB state,
                              const MonoDelta& timeout) {
  MonoTime start = MonoTime::Now(MonoTime::FINE);
  MonoTime deadline = start;
  deadline.AddDelta(timeout);
  vector<ListTabletsResponsePB::StatusAndSchemaPB> tablets;
  Status s;
  tablet::TabletStatePB last_state = tablet::UNKNOWN;
  while (true) {
    s = ListTablets(ts, MonoDelta::FromSeconds(10), &tablets);
    if (s.ok()) {
      bool seen = false;
      for (const ListTabletsResponsePB::StatusAndSchemaPB& t : tablets) {
        if (t.tablet_status().tablet_id() == tablet_id) {
          seen = true;
          last_state = t.tablet_status().state();
          if (last_state == state) {
            return Status::OK();
          }
        }
      }
      if (!seen) {
        s = Status::NotFound("Tablet " + tablet_id + " not found");
      }
    }
    if (deadline.ComesBefore(MonoTime::Now(MonoTime::FINE))) {
      break;
    }
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  return Status::TimedOut(Substitute("T $0 P $1: Tablet not in $2 state after $3: "
                                     "Tablet state: $4, Status message: $5",
                                     tablet_id, ts->uuid(),
                                     tablet::TabletStatePB_Name(state),
                                     MonoTime::Now(MonoTime::FINE).GetDeltaSince(start).ToString(),
                                     tablet::TabletStatePB_Name(last_state), s.ToString()));
}

// Wait until the specified tablet is in RUNNING state.
Status WaitUntilTabletRunning(TServerDetails* ts,
                              const std::string& tablet_id,
                              const MonoDelta& timeout) {
  return WaitUntilTabletInState(ts, tablet_id, tablet::RUNNING, timeout);
}

Status DeleteTablet(const TServerDetails* ts,
                    const std::string& tablet_id,
                    const tablet::TabletDataState delete_type,
                    const boost::optional<int64_t>& cas_config_opid_index_less_or_equal,
                    const MonoDelta& timeout,
                    tserver::TabletServerErrorPB::Code* error_code) {
  DeleteTabletRequestPB req;
  DeleteTabletResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(timeout);

  req.set_dest_uuid(ts->uuid());
  req.set_tablet_id(tablet_id);
  req.set_delete_type(delete_type);
  if (cas_config_opid_index_less_or_equal) {
    req.set_cas_config_opid_index_less_or_equal(*cas_config_opid_index_less_or_equal);
  }

  RETURN_NOT_OK(ts->tserver_admin_proxy->DeleteTablet(req, &resp, &rpc));
  if (resp.has_error()) {
    if (error_code) {
      *error_code = resp.error().code();
    }
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Status StartRemoteBootstrap(const TServerDetails* ts,
                            const string& tablet_id,
                            const string& bootstrap_source_uuid,
                            const HostPort& bootstrap_source_addr,
                            int64_t caller_term,
                            const MonoDelta& timeout) {
  consensus::StartRemoteBootstrapRequestPB req;
  consensus::StartRemoteBootstrapResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(timeout);

  req.set_dest_uuid(ts->uuid());
  req.set_tablet_id(tablet_id);
  req.set_bootstrap_peer_uuid(bootstrap_source_uuid);
  RETURN_NOT_OK(HostPortToPB(bootstrap_source_addr, req.mutable_bootstrap_peer_addr()));
  req.set_caller_term(caller_term);

  RETURN_NOT_OK(ts->consensus_proxy->StartRemoteBootstrap(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

} // namespace itest
} // namespace kudu
