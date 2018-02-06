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
#include "yb/tools/yb-admin_client.h"

#include "yb/common/wire_protocol.h"
#include "yb/client/client.h"
#include "yb/master/sys_catalog.h"
#include "yb/rpc/messenger.h"
#include "yb/util/string_case.h"
#include "yb/util/string_util.h"
#include "yb/util/protobuf_util.h"

#include "yb/consensus/consensus.proxy.h"
#include "yb/tserver/tserver_service.proxy.h"

// Maximum number of elements to dump on unexpected errors.
static constexpr int MAX_NUM_ELEMENTS_TO_SHOW_ON_ERROR = 10;

PB_ENUM_FORMATTERS(yb::consensus::RaftPeerPB::Role);
PB_ENUM_FORMATTERS(yb::AppStatusPB::ErrorCode);
PB_ENUM_FORMATTERS(yb::tablet::TabletStatePB);

namespace yb {
namespace tools {

using google::protobuf::RepeatedPtrField;

using client::YBClientBuilder;
using client::YBTableName;
using rpc::MessengerBuilder;
using rpc::RpcController;
using strings::Substitute;
using tserver::TabletServerServiceProxy;

using consensus::ConsensusServiceProxy;
using consensus::LeaderStepDownRequestPB;
using consensus::LeaderStepDownResponsePB;
using consensus::RaftPeerPB;
using consensus::RunLeaderElectionRequestPB;
using consensus::RunLeaderElectionResponsePB;

using master::MasterServiceProxy;
using master::ListMastersRequestPB;
using master::ListMastersResponsePB;
using master::ListMasterRaftPeersRequestPB;
using master::ListMasterRaftPeersResponsePB;
using master::ListTabletServersRequestPB;
using master::ListTabletServersResponsePB;
using master::TabletLocationsPB;
using master::TSInfoPB;

namespace {

static constexpr const char* kRpcHostPortHeading = "RPC Host/Port";

string FormatHostPort(const HostPortPB& host_port) {
  return Format("$0:$1", host_port.host(), host_port.port());
}

string FormatFirstHostPort(
    const google::protobuf::RepeatedPtrField<yb::HostPortPB>& rpc_addresses) {
  if (rpc_addresses.empty()) {
    return "N/A";
  } else {
    return FormatHostPort(rpc_addresses.Get(0));
  }
}

const int kPartitionRangeColWidth = 56;
const int kHostPortColWidth = 20;
const int kTableNameColWidth = 48;
const int kNumCharactersInUuid = 32;

}  // anonymous namespace

ClusterAdminClient::ClusterAdminClient(string addrs, int64_t timeout_millis)
    : master_addr_list_(std::move(addrs)),
      timeout_(MonoDelta::FromMilliseconds(timeout_millis)),
      initted_(false) {}

Status ClusterAdminClient::Init() {
  CHECK(!initted_);

  // Build master proxy.
  CHECK_OK(YBClientBuilder()
    .add_master_server_addr(master_addr_list_)
    .default_admin_operation_timeout(timeout_)
    .Build(&yb_client_));

  MessengerBuilder builder("yb-admin");
  RETURN_NOT_OK(builder.Build().MoveTo(&messenger_));

  // Find the leader master's socket info to set up the proxy
  RETURN_NOT_OK(yb_client_->SetMasterLeaderSocket(&leader_sock_));
  master_proxy_.reset(new MasterServiceProxy(messenger_, leader_sock_));

  initted_ = true;
  return Status::OK();
}

Status ClusterAdminClient::MasterLeaderStepDown(const string& leader_uuid) {
  std::unique_ptr<ConsensusServiceProxy>
    master_proxy(new ConsensusServiceProxy(messenger_, leader_sock_));

  return LeaderStepDown(leader_uuid, yb::master::kSysCatalogTabletId, &master_proxy);
}

Status ClusterAdminClient::LeaderStepDown(
    const PeerId& leader_uuid,
    const TabletId& tablet_id,
    std::unique_ptr<ConsensusServiceProxy>* leader_proxy) {
  LeaderStepDownRequestPB req;
  req.set_dest_uuid(leader_uuid);
  req.set_tablet_id(tablet_id);
  LeaderStepDownResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK((*leader_proxy)->LeaderStepDown(req, &resp, &rpc));
  if (resp.has_error()) {
    LOG(ERROR) << "LeaderStepDown for " << leader_uuid << "received error "
      << resp.error().ShortDebugString();
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

// Force start an election on a randomly chosen non-leader peer of this tablet's raft quorum.
Status ClusterAdminClient::StartElection(const TabletId& tablet_id) {
  Endpoint non_leader_addr;
  string non_leader_uuid;
  RETURN_NOT_OK(SetTabletPeerInfo(tablet_id, FOLLOWER, &non_leader_uuid, &non_leader_addr));
  std::unique_ptr<ConsensusServiceProxy>
    non_leader_proxy(new ConsensusServiceProxy(messenger_, non_leader_addr));
  RunLeaderElectionRequestPB req;
  req.set_dest_uuid(non_leader_uuid);
  req.set_tablet_id(tablet_id);
  RunLeaderElectionResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(timeout_);

  RETURN_NOT_OK(non_leader_proxy->RunLeaderElection(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

// Look up the location of the tablet server leader or non-leader peer from the leader master
Status ClusterAdminClient::SetTabletPeerInfo(
    const TabletId& tablet_id,
    PeerMode mode,
    PeerId* peer_uuid,
    Endpoint* peer_socket) {
  TSInfoPB peer_ts_info;
  RETURN_NOT_OK(GetTabletPeer(tablet_id, mode, &peer_ts_info));
  auto rpc_addresses = peer_ts_info.rpc_addresses();
  CHECK_GT(rpc_addresses.size(), 0) << peer_ts_info
        .ShortDebugString();

  HostPort peer_hostport;
  RETURN_NOT_OK(HostPortFromPB(rpc_addresses.Get(0), &peer_hostport));
  std::vector<Endpoint> peer_addrs;
  RETURN_NOT_OK(peer_hostport.ResolveAddresses(&peer_addrs));
  CHECK(!peer_addrs.empty()) << "Unable to resolve IP address for tablet leader host: "
    << peer_hostport.ToString();
  CHECK(peer_addrs.size() == 1) << "Expected only one tablet leader, but got : "
    << peer_hostport.ToString();
  *peer_socket = peer_addrs[0];
  *peer_uuid = peer_ts_info.permanent_uuid();
  return Status::OK();
}

Status ClusterAdminClient::ParseChangeType(
    const string& change_type,
    consensus::ChangeConfigType* cc_type) {
  consensus::ChangeConfigType cctype = consensus::UNKNOWN_CHANGE;
  *cc_type = cctype;
  string uppercase_change_type;
  ToUpperCase(change_type, &uppercase_change_type);
  if (!consensus::ChangeConfigType_Parse(uppercase_change_type, &cctype) ||
    cctype == consensus::UNKNOWN_CHANGE) {
    return STATUS(InvalidArgument, "Unsupported change_type", change_type);
  }

  *cc_type = cctype;

  return Status::OK();
}

Status ClusterAdminClient::ChangeConfig(
    const TabletId& tablet_id,
    const string& change_type,
    const PeerId& peer_uuid,
    const boost::optional<string>& member_type) {
  CHECK(initted_);

  consensus::ChangeConfigType cc_type;
  RETURN_NOT_OK(ParseChangeType(change_type, &cc_type));

  RaftPeerPB peer_pb;
  peer_pb.set_permanent_uuid(peer_uuid);

  // Parse the optional fields.
  if (member_type) {
    RaftPeerPB::MemberType member_type_val;
    string uppercase_member_type;
    ToUpperCase(*member_type, &uppercase_member_type);
    if (!RaftPeerPB::MemberType_Parse(uppercase_member_type, &member_type_val)) {
      return STATUS(InvalidArgument, "Unrecognized member_type", *member_type);
    }
    if (member_type_val != RaftPeerPB::PRE_VOTER && member_type_val != RaftPeerPB::PRE_OBSERVER) {
      return STATUS(InvalidArgument, "member_type should be PRE_VOTER or PRE_OBSERVER");
    }
    peer_pb.set_member_type(member_type_val);
  }

  // Validate the existence of the optional fields.
  if (!member_type && cc_type == consensus::ADD_SERVER) {
    return STATUS(InvalidArgument, "Must specify member_type when adding a server.");
  }

  // Look up RPC address of peer if adding as a new server.
  if (cc_type == consensus::ADD_SERVER) {
    HostPort host_port;
    RETURN_NOT_OK(GetFirstRpcAddressForTS(peer_uuid, &host_port));
    RETURN_NOT_OK(HostPortToPB(host_port, peer_pb.mutable_last_known_addr()));
  }

  // Look up the location of the tablet leader from the Master.
  Endpoint leader_addr;
  string leader_uuid;
  RETURN_NOT_OK(SetTabletPeerInfo(tablet_id, LEADER, &leader_uuid, &leader_addr));

  std::unique_ptr<ConsensusServiceProxy>
    consensus_proxy(new ConsensusServiceProxy(messenger_, leader_addr));
  // If removing the leader ts, then first make it step down and that
  // starts an election and gets a new leader ts.
  if (cc_type == consensus::REMOVE_SERVER &&
      leader_uuid == peer_uuid) {
    string old_leader_uuid = leader_uuid;
    RETURN_NOT_OK(LeaderStepDown(leader_uuid, tablet_id, &consensus_proxy));
    sleep(5);  // TODO - election completion timing is not known accurately
    RETURN_NOT_OK(SetTabletPeerInfo(tablet_id, LEADER, &leader_uuid, &leader_addr));
    if (leader_uuid != old_leader_uuid) {
      return STATUS(ConfigurationError,
                    "Old tablet server leader same as new even after re-election!");
    }
    consensus_proxy.reset(new ConsensusServiceProxy(messenger_, leader_addr));
  }

  consensus::ChangeConfigRequestPB req;
  consensus::ChangeConfigResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(timeout_);

  req.set_dest_uuid(leader_uuid);
  req.set_tablet_id(tablet_id);
  req.set_type(cc_type);
  *req.mutable_server() = peer_pb;

  RETURN_NOT_OK(consensus_proxy.get()->ChangeConfig(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Status ClusterAdminClient::GetMasterLeaderInfo(PeerId* leader_uuid) {
  master::ListMastersRequestPB list_req;
  master::ListMastersResponsePB list_resp;

  RpcController rpc;
  rpc.set_timeout(timeout_);
  master_proxy_->ListMasters(list_req, &list_resp, &rpc);
  if (list_resp.has_error()) {
    return StatusFromPB(list_resp.error().status());
  }
  for (int i = 0; i < list_resp.masters_size(); i++) {
    if (list_resp.masters(i).role() == RaftPeerPB::LEADER) {
      CHECK(leader_uuid->empty()) << "Found two LEADER's in the same raft config.";
      *leader_uuid = list_resp.masters(i).instance_id().permanent_uuid();
    }
  }

  return Status::OK();
}

Status ClusterAdminClient::DumpMasterState() {
  CHECK(initted_);
  master::DumpMasterStateRequestPB req;
  master::DumpMasterStateResponsePB resp;

  RpcController rpc;
  rpc.set_timeout(timeout_);
  req.set_peers_also(true);
  req.set_on_disk(true);
  master_proxy_->DumpState(req, &resp, &rpc);

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  return Status::OK();
}

Status ClusterAdminClient::GetLoadMoveCompletion() {
  CHECK(initted_);
  master::GetLoadMovePercentRequestPB req;
  master::GetLoadMovePercentResponsePB resp;

  RpcController rpc;
  rpc.set_timeout(timeout_);
  master_proxy_->GetLoadMoveCompletion(req, &resp, &rpc);

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  std::cout << "Percent complete = " << resp.percent() << std::endl;
  return Status::OK();
}

Status ClusterAdminClient::ListLeaderCounts(const YBTableName& table_name) {
  vector<string> tablet_ids, ranges;
  RETURN_NOT_OK(yb_client_->GetTablets(table_name, 0, &tablet_ids, &ranges));
  rpc::RpcController rpc;
  master::GetTabletLocationsRequestPB req;
  master::GetTabletLocationsResponsePB resp;
  rpc.set_timeout(timeout_);
  for (const auto& tablet_id : tablet_ids) {
    req.add_tablet_ids(tablet_id);
  }
  RETURN_NOT_OK(master_proxy_->GetTabletLocations(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  unordered_map<string, int> leader_counts;
  int total_leader_count = 0;
  for (int i = 0; i < resp.tablet_locations_size(); ++i) {
    TabletLocationsPB locs = resp.tablet_locations(i);
    for (int i = 0; i < locs.replicas_size(); i++) {
      if (locs.replicas(i).role() == RaftPeerPB::LEADER) {
        // If this is a leader, increment leader counts.
        leader_counts[locs.replicas(i).ts_info().permanent_uuid()]++;
        total_leader_count++;
      } else if (locs.replicas(i).role() == RaftPeerPB::FOLLOWER) {
        // If this is a follower, touch the leader count entry also so that tablet server with
        // followers only and 0 leader will be accounted for still.
        leader_counts[locs.replicas(i).ts_info().permanent_uuid()];
      }
    }
  }

  // Calculate the standard deviation and adjusted deviation percentage according to the best and
  // worst-case scenarios. Best-case distribution is when leaders are evenly distributed and
  // worst-case is when leaders are all on one tablet server.
  // For example, say we have 16 leaders on 3 tablet servers:
  //   Leader distribution:    7 5 4
  //   Best-case scenario:     6 5 5
  //   Worst-case scenario:   12 0 0
  //   Standard deviation:    1.24722
  //   Adjusted deviation %:  10.9717%
  vector<double> leader_dist, best_case, worst_case;
  std::cout << RightPadToUuidWidth("Server UUID") << kColumnSep << "Leader Count" << std::endl;
  for (const auto& leader_count : leader_counts) {
    std::cout << leader_count.first << kColumnSep << leader_count.second << std::endl;
    leader_dist.push_back(leader_count.second);
  }

  if (!leader_dist.empty()) {
    for (int i = 0; i < leader_dist.size(); ++i) {
      best_case.push_back(total_leader_count / leader_dist.size());
      worst_case.push_back(0);
    }
    for (int i = 0; i < total_leader_count % leader_dist.size(); ++i) {
      ++best_case[i];
    }
    worst_case[0] = total_leader_count;

    double stdev = yb::standard_deviation(leader_dist);
    double best_stdev = yb::standard_deviation(best_case);
    double worst_stdev = yb::standard_deviation(worst_case);
    double percent_dev = (stdev - best_stdev) / (worst_stdev - best_stdev) * 100.0;
    std::cout << "Standard deviation: " << stdev << std::endl;
    std::cout << "Adjusted deviation percentage: " << percent_dev << "%" << std::endl;
  }

  return Status::OK();
}

Status ClusterAdminClient::SetupRedisTable() {
  const YBTableName table_name(common::kRedisKeyspaceName, common::kRedisTableName);
  RETURN_NOT_OK(yb_client_->CreateNamespaceIfNotExists(common::kRedisKeyspaceName));
  // Try to create the table.
  gscoped_ptr<yb::client::YBTableCreator> table_creator(yb_client_->NewTableCreator());
  Status s = table_creator->table_name(table_name)
                              .table_type(yb::client::YBTableType::REDIS_TABLE_TYPE)
                              .Create();
  // If we could create it, then all good!
  if (s.ok()) {
    LOG(INFO) << "Table '" << table_name.ToString() << "' created.";
    // If the table was already there, also not an error...
  } else if (s.IsAlreadyPresent()) {
    LOG(INFO) << "Table '" << table_name.ToString() << "' already exists";
  } else {
    // If any other error, report that!
    RETURN_NOT_OK(s);
  }
  return Status::OK();
}

Status ClusterAdminClient::DropRedisTable() {
  const YBTableName table_name(common::kRedisKeyspaceName, common::kRedisTableName);
  Status s = yb_client_->DeleteTable(table_name, true /* wait */);
  if (s.ok()) {
    LOG(INFO) << "Table '" << table_name.ToString() << "' deleted.";
  } else if (s.IsNotFound()) {
    LOG(INFO) << "Table '" << table_name.ToString() << "' does not exist.";
  } else {
    RETURN_NOT_OK(s);
  }
  return Status::OK();
}

Status ClusterAdminClient::ChangeMasterConfig(
    const string& change_type,
    const string& peer_host,
    int16 peer_port) {
  CHECK(initted_);

  consensus::ChangeConfigType cc_type;
  RETURN_NOT_OK(ParseChangeType(change_type, &cc_type));

  string peer_uuid;
  RETURN_NOT_OK(yb_client_->GetMasterUUID(peer_host, peer_port, &peer_uuid));

  string leader_uuid;
  RETURN_NOT_OK_PREPEND(GetMasterLeaderInfo(&leader_uuid), "Could not locate master leader");
  if (leader_uuid.empty()) {
    return STATUS(ConfigurationError, "Could not locate master leader!");
  }

  // If removing the leader master, then first make it step down and that
  // starts an election and gets a new leader master.
  auto changed_leader_sock = leader_sock_;
  if (cc_type == consensus::REMOVE_SERVER &&
      leader_uuid == peer_uuid) {
    string old_leader_uuid = leader_uuid;
    RETURN_NOT_OK(MasterLeaderStepDown(leader_uuid));
    sleep(5);  // TODO - wait for exactly the time needed for new leader to get elected.
    // Reget the leader master's socket info to set up the proxy
    RETURN_NOT_OK(yb_client_->RefreshMasterLeaderSocket(&leader_sock_));
    master_proxy_.reset(new MasterServiceProxy(messenger_, leader_sock_));
    leader_uuid = "";  // reset so it can be set to new leader in the GetMasterLeaderInfo call below
    RETURN_NOT_OK_PREPEND(GetMasterLeaderInfo(&leader_uuid), "Could not locate new master leader");
    if (leader_uuid.empty()) {
      return STATUS(ConfigurationError, "Could not locate new master leader!");
    }
    if (leader_uuid == old_leader_uuid) {
      return STATUS(ConfigurationError,
        Substitute("Old master leader uuid $0 same as new one even after stepdown!", leader_uuid));
    }
    // Go ahead below and send the actual config change message to the new master
  }

  consensus::ConsensusServiceProxy *leader_proxy =
    new consensus::ConsensusServiceProxy(messenger_, leader_sock_);
  consensus::ChangeConfigRequestPB req;
  consensus::ChangeConfigResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(timeout_);

  RaftPeerPB peer_pb;
  peer_pb.set_permanent_uuid(peer_uuid);
  // Ignored by ChangeConfig if request != ADD_SERVER.
  peer_pb.set_member_type(RaftPeerPB::PRE_VOTER);
  HostPortPB *peer_host_port = peer_pb.mutable_last_known_addr();
  peer_host_port->set_port(peer_port);
  peer_host_port->set_host(peer_host);
  req.set_dest_uuid(leader_uuid);
  req.set_tablet_id(yb::master::kSysCatalogTabletId);
  req.set_type(cc_type);
  *req.mutable_server() = peer_pb;

  RETURN_NOT_OK(leader_proxy->ChangeConfig(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  if (cc_type == consensus::ADD_SERVER) {
    RETURN_NOT_OK(yb_client_->AddMasterToClient(changed_leader_sock));
  } else {
    RETURN_NOT_OK(yb_client_->RemoveMasterFromClient(changed_leader_sock));
  }

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  return Status::OK();
}

Status ClusterAdminClient::GetTabletLocations(const TabletId& tablet_id,
                                              TabletLocationsPB* locations) {
  rpc::RpcController rpc;
  rpc.set_timeout(timeout_);
  master::GetTabletLocationsRequestPB req;
  *req.add_tablet_ids() = tablet_id;
  master::GetTabletLocationsResponsePB resp;
  RETURN_NOT_OK(master_proxy_->GetTabletLocations(req, &resp, &rpc));

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  if (resp.errors_size() > 0) {
    // This tool only needs to support one-by-one requests for tablet
    // locations, so we only look at the first error.
    return StatusFromPB(resp.errors(0).status());
  }

  // Same as above, no batching, and we already got past the error checks.
  CHECK_EQ(1, resp.tablet_locations_size()) << resp.ShortDebugString();

  *locations = resp.tablet_locations(0);
  return Status::OK();
}

Status ClusterAdminClient::GetTabletPeer(const TabletId& tablet_id,
                                         PeerMode mode,
                                         TSInfoPB* ts_info) {
  TabletLocationsPB locations;
  RETURN_NOT_OK(GetTabletLocations(tablet_id, &locations));
  CHECK_EQ(tablet_id, locations.tablet_id()) << locations.ShortDebugString();
  bool found = false;
  for (const TabletLocationsPB::ReplicaPB& replica : locations.replicas()) {
    if (mode == LEADER && replica.role() == RaftPeerPB::LEADER) {
      *ts_info = replica.ts_info();
      found = true;
      break;
    }
    if (mode == FOLLOWER && replica.role() != RaftPeerPB::LEADER) {
      *ts_info = replica.ts_info();
      found = true;
      break;
    }
  }

  if (!found) {
    return STATUS(NotFound,
      Substitute("No peer replica found in $0 mode for tablet $1", mode, tablet_id));
  }

  return Status::OK();
}

Status ClusterAdminClient::ListTabletServers(
    RepeatedPtrField<ListTabletServersResponsePB::Entry>* servers) {
  rpc::RpcController rpc;
  rpc.set_timeout(timeout_);
  ListTabletServersRequestPB req;
  ListTabletServersResponsePB resp;
  RETURN_NOT_OK(master_proxy_->ListTabletServers(req, &resp, &rpc));

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  servers->Swap(resp.mutable_servers());
  return Status::OK();
}

Status ClusterAdminClient::GetFirstRpcAddressForTS(
    const PeerId& uuid,
    HostPort* hp) {
  RepeatedPtrField<ListTabletServersResponsePB::Entry> servers;
  RETURN_NOT_OK(ListTabletServers(&servers));
  for (const ListTabletServersResponsePB::Entry& server : servers) {
    if (server.instance_id().permanent_uuid() == uuid) {
      if (!server.has_registration() || server.registration().common().rpc_addresses_size() == 0) {
        break;
      }
      RETURN_NOT_OK(HostPortFromPB(server.registration().common().rpc_addresses(0), hp));
      return Status::OK();
    }
  }

  return STATUS(NotFound, Substitute("Server with UUID $0 has no RPC address "
                                     "registered with the Master", uuid));
}

Status ClusterAdminClient::ListAllTabletServers() {
  RepeatedPtrField<ListTabletServersResponsePB::Entry> servers;
  RETURN_NOT_OK(ListTabletServers(&servers));

  if (!servers.empty()) {
    std::cout << RightPadToUuidWidth("Tablet Server UUID") << kColumnSep
              << kRpcHostPortHeading << std::endl;
  }
  for (const ListTabletServersResponsePB::Entry& server : servers) {
    std::cout << server.instance_id().permanent_uuid() << kColumnSep
              << FormatFirstHostPort(server.registration().common().rpc_addresses())
              << std::endl;
  }

  return Status::OK();
}

Status ClusterAdminClient::ListAllMasters() {
  ListMastersRequestPB lreq;
  ListMastersResponsePB lresp;
  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_proxy_->ListMasters(lreq, &lresp, &rpc));
  if (lresp.has_error()) {
    return StatusFromPB(lresp.error().status());
  }
  if (!lresp.masters().empty()) {
    std::cout << RightPadToUuidWidth("Master UUID") << kColumnSep
              << RightPadToWidth(kRpcHostPortHeading, kHostPortColWidth)<< kColumnSep
              << "State" << kColumnSep
              << "Role" << std::endl;
  }
  for (int i = 0; i < lresp.masters_size(); i++) {
    if (lresp.masters(i).role() != consensus::RaftPeerPB::UNKNOWN_ROLE) {
      auto master_reg =
          lresp.masters(i).has_registration() ? &lresp.masters(i).registration() : nullptr;
      std::cout
          << (master_reg ? lresp.masters(i).instance_id().permanent_uuid() :
              RightPadToUuidWidth("UNKNOWN_UUID")) << kColumnSep
          << RightPadToWidth(
              (master_reg ? FormatFirstHostPort(master_reg->rpc_addresses()) : "UNKNOWN"),
              kHostPortColWidth) << kColumnSep
          << (lresp.masters(i).has_error() ?
              PBEnumToString(lresp.masters(i).error().code()) : "ALIVE") << kColumnSep
          << PBEnumToString(lresp.masters(i).role()) << std::endl;
    } else {
      std::cout << "UNREACHABLE MASTER at index " << i << "." << std::endl;
    }
  }

  ListMasterRaftPeersRequestPB r_req;
  ListMasterRaftPeersResponsePB r_resp;
  rpc.Reset();
  RETURN_NOT_OK(master_proxy_->ListMasterRaftPeers(r_req, &r_resp, &rpc));
  if (r_resp.has_error()) {
    return STATUS(RuntimeError, Substitute(
        "List Raft Masters RPC response hit error: $0", r_resp.error().ShortDebugString()));
  }

  if (r_resp.masters_size() != lresp.masters_size()) {
    std::cout << "WARNING: Mismatch in in-memory masters and raft peers info."
              << "Raft peer info from master leader dumped below.\n";
    for (int i = 0; i < r_resp.masters_size(); i++) {
      if (r_resp.masters(i).member_type() != consensus::RaftPeerPB::UNKNOWN_MEMBER_TYPE) {
        const auto& master = r_resp.masters(i);
        std::cout << master.permanent_uuid() << "  "
                  << master.last_known_addr().host() << "/"
                  << master.last_known_addr().port() << std::endl;
      } else {
        std::cout << "UNREACHABLE MASTER at index " << i << "." << std::endl;
      }
    }
  }

  return Status::OK();
}

Status ClusterAdminClient::ListTabletServersLogLocations() {
  RepeatedPtrField<ListTabletServersResponsePB::Entry> servers;
  RETURN_NOT_OK(ListTabletServers(&servers));

  if (!servers.empty()) {
    std::cout << RightPadToUuidWidth("TS UUID") << kColumnSep
              << kRpcHostPortHeading << kColumnSep
              << "LogLocation"
              << std::endl;
  }

  for (const ListTabletServersResponsePB::Entry& server : servers) {
    auto ts_uuid = server.instance_id().permanent_uuid();

    Endpoint ts_addr;
    RETURN_NOT_OK(GetEndpointForTS(ts_uuid, &ts_addr));

    std::unique_ptr<TabletServerServiceProxy> ts_proxy(
        new TabletServerServiceProxy(messenger_, ts_addr));

    rpc::RpcController rpc;
    rpc.set_timeout(timeout_);
    tserver::GetLogLocationRequestPB req;
    tserver::GetLogLocationResponsePB resp;
    ts_proxy.get()->GetLogLocation(req, &resp, &rpc);

    std::cout << ts_uuid << kColumnSep
              << ts_addr << kColumnSep
              << resp.log_location() << std::endl;
  }

  return Status::OK();
}

Status ClusterAdminClient::ListTables() {
  vector<YBTableName> tables;
  RETURN_NOT_OK(yb_client_->ListTables(&tables));
  for (const YBTableName& table : tables) {
    std::cout << table.ToString() << std::endl;
  }
  return Status::OK();
}

Status ClusterAdminClient::ListTablets(const YBTableName& table_name, const int max_tablets) {
  vector<string> tablet_uuids, ranges;
  std::vector<master::TabletLocationsPB> locations;
  RETURN_NOT_OK(yb_client_->GetTablets(
      table_name, max_tablets, &tablet_uuids, &ranges, &locations));
  std::cout << RightPadToUuidWidth("Tablet UUID") << kColumnSep
            << RightPadToWidth("Range", kPartitionRangeColWidth) << kColumnSep
            << "Leader" << std::endl;
  for (int i = 0; i < tablet_uuids.size(); i++) {
    string tablet_uuid = tablet_uuids[i];
    string leader_host_port;
    const auto& locations_of_this_tablet = locations[i];
    for (const auto& replica : locations_of_this_tablet.replicas()) {
      if (replica.role() == RaftPeerPB::Role::RaftPeerPB_Role_LEADER) {
        if (leader_host_port.empty()) {
          leader_host_port = FormatHostPort(replica.ts_info().rpc_addresses(0));
        } else {
          LOG(ERROR) << "Multiple leader replicas found for tablet " << tablet_uuid
                     << ": " << locations_of_this_tablet.ShortDebugString();
        }
      }
    }
    std::cout << tablet_uuid << kColumnSep
              << RightPadToWidth(ranges[i], kPartitionRangeColWidth) << kColumnSep
              << leader_host_port << std::endl;
  }
  return Status::OK();
}

Status ClusterAdminClient::ListPerTabletTabletServers(const TabletId& tablet_id) {
  rpc::RpcController rpc;
  rpc.set_timeout(timeout_);
  master::GetTabletLocationsRequestPB req;
  *req.add_tablet_ids() = tablet_id;
  master::GetTabletLocationsResponsePB resp;
  RETURN_NOT_OK(master_proxy_->GetTabletLocations(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  if (resp.tablet_locations_size() != 1) {
    if (resp.tablet_locations_size() > 0) {
      std::cerr << "List of all incorrect locations - " << resp.tablet_locations_size()
                << " : " << std::endl;
      for (int i = 0; i < resp.tablet_locations_size(); i++) {
        std::cerr << i << " : " << resp.tablet_locations(i).DebugString();
        if (i >= MAX_NUM_ELEMENTS_TO_SHOW_ON_ERROR) {
          break;
        }
      }
      std::cerr << std::endl;
    }
    return STATUS_FORMAT(IllegalState,
                         "Incorrect number of locations $0 for tablet $1.",
                         resp.tablet_locations_size(), tablet_id);
  }

  TabletLocationsPB locs = resp.tablet_locations(0);
  if (!locs.replicas().empty()) {
    std::cout << RightPadToUuidWidth("Server UUID") << kColumnSep
              << RightPadToWidth(kRpcHostPortHeading, kHostPortColWidth) << kColumnSep
              << "Role" << std::endl;
  }
  for (const auto& replica : locs.replicas()) {
    std::cout << replica.ts_info().permanent_uuid() << kColumnSep
              << RightPadToWidth(FormatHostPort(replica.ts_info().rpc_addresses(0)),
                            kHostPortColWidth) << kColumnSep
              << PBEnumToString(replica.role()) << std::endl;
  }

  return Status::OK();
}

Status ClusterAdminClient::DeleteTable(const YBTableName& table_name) {
  RETURN_NOT_OK(yb_client_->DeleteTable(table_name));
  std::cout << "Deleted table " << table_name.ToString() << std::endl;
  return Status::OK();
}

Status ClusterAdminClient::GetEndpointForHostPort(const HostPort& hp, Endpoint* addr) {
  std::vector<Endpoint> sock_addrs;
  RETURN_NOT_OK(hp.ResolveAddresses(&sock_addrs));
  if (sock_addrs.empty()) {
    return STATUS(IllegalState,
        Substitute("Unable to resolve IP address for host: $0", hp.ToString()));
  }
  if (sock_addrs.size() != 1) {
    return STATUS(IllegalState,
        Substitute("Expected only one IP for host, but got : $0", hp.ToString()));
  }

  *addr = sock_addrs[0];

  return Status::OK();
}

Status ClusterAdminClient::GetEndpointForTS(const PeerId& ts_uuid, Endpoint* ts_addr) {
  HostPort hp;
  RETURN_NOT_OK(GetFirstRpcAddressForTS(ts_uuid, &hp));
  RETURN_NOT_OK(GetEndpointForHostPort(hp, ts_addr));

  return Status::OK();
}

Status ClusterAdminClient::ListTabletsForTabletServer(const PeerId& ts_uuid) {
  Endpoint ts_addr;
  RETURN_NOT_OK(GetEndpointForTS(ts_uuid, &ts_addr));

  std::unique_ptr<TabletServerServiceProxy> ts_proxy(
      new TabletServerServiceProxy(messenger_, ts_addr));

  rpc::RpcController rpc;
  rpc.set_timeout(timeout_);

  tserver::ListTabletsForTabletServerRequestPB req;
  tserver::ListTabletsForTabletServerResponsePB resp;
  RETURN_NOT_OK(ts_proxy.get()->ListTabletsForTabletServer(req, &resp, &rpc));

  std::cout << RightPadToWidth("Table name", kTableNameColWidth) << kColumnSep
            << RightPadToUuidWidth("Tablet ID") << kColumnSep
            << "Is Leader" << kColumnSep
            << "State" << std::endl;
  for (const auto& entry : resp.entries()) {
    std::cout << RightPadToWidth(entry.table_name(), kTableNameColWidth) << kColumnSep
              << RightPadToUuidWidth(entry.tablet_id()) << kColumnSep
              << entry.is_leader() << kColumnSep
              << PBEnumToString(entry.state()) << std::endl;
  }
  return Status::OK();
}

Status ClusterAdminClient::SetLoadBalancerEnabled(const bool is_enabled) {
  master::ListMastersRequestPB list_req;
  master::ListMastersResponsePB list_resp;

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_proxy_->ListMasters(list_req, &list_resp, &rpc));
  if (list_resp.has_error()) {
    return StatusFromPB(list_resp.error().status());
  }

  master::ChangeLoadBalancerStateRequestPB req;
  req.set_is_enabled(is_enabled);
  master::ChangeLoadBalancerStateResponsePB resp;
  for (int i = 0; i < list_resp.masters_size(); ++i) {
    rpc.Reset();
    resp.Clear();

    if (list_resp.masters(i).role() == RaftPeerPB::LEADER) {
      master_proxy_->ChangeLoadBalancerState(req, &resp, &rpc);
    } else {
      HostPortPB hp_pb = list_resp.masters(i).registration().rpc_addresses(0);
      HostPort hp(hp_pb.host(), hp_pb.port());
      Endpoint master_addr;
      RETURN_NOT_OK(GetEndpointForHostPort(hp, &master_addr));

      auto proxy =
          std::unique_ptr<MasterServiceProxy>(new MasterServiceProxy(messenger_, master_addr));
      proxy->ChangeLoadBalancerState(req, &resp, &rpc);
    }

    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
  }

  return Status::OK();
}

string RightPadToUuidWidth(const string &s) {
  return RightPadToWidth(s, kNumCharactersInUuid);
}

}  // namespace tools
}  // namespace yb
