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

#include <array>
#include <type_traits>

#include <boost/tti/has_member_function.hpp>

#include "yb/common/wire_protocol.h"
#include "yb/client/client.h"
#include "yb/client/table_creator.h"
#include "yb/master/sys_catalog.h"
#include "yb/rpc/messenger.h"
#include "yb/util/string_case.h"
#include "yb/util/string_util.h"
#include "yb/util/protobuf_util.h"
#include "yb/util/random_util.h"
#include "yb/gutil/strings/split.h"

#include "yb/consensus/consensus.proxy.h"
#include "yb/tserver/tserver_service.proxy.h"

// Maximum number of elements to dump on unexpected errors.
static constexpr int MAX_NUM_ELEMENTS_TO_SHOW_ON_ERROR = 10;

PB_ENUM_FORMATTERS(yb::consensus::RaftPeerPB::Role);
PB_ENUM_FORMATTERS(yb::AppStatusPB::ErrorCode);
PB_ENUM_FORMATTERS(yb::tablet::RaftGroupStatePB);

namespace yb {
namespace tools {

using namespace std::literals;

using std::cout;
using std::endl;

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

using master::FlushTablesRequestPB;
using master::FlushTablesResponsePB;
using master::IsFlushTablesDoneRequestPB;
using master::IsFlushTablesDoneResponsePB;
using master::ListMastersRequestPB;
using master::ListMastersResponsePB;
using master::ListMasterRaftPeersRequestPB;
using master::ListMasterRaftPeersResponsePB;
using master::ListTabletServersRequestPB;
using master::ListTabletServersResponsePB;
using master::MasterServiceProxy;
using master::TabletLocationsPB;
using master::TSInfoPB;

namespace {

static constexpr const char* kRpcHostPortHeading = "RPC Host/Port";
static constexpr const char* kDBTypePrefixUnknown = "unknown";
static constexpr const char* kDBTypePrefixCql = "ycql";
static constexpr const char* kDBTypePrefixYsql = "ysql";
static constexpr const char* kDBTypePrefixRedis = "yedis";


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
const int kSleepTimeSec = 1;
const int kNumberOfTryouts = 30;

BOOST_TTI_HAS_MEMBER_FUNCTION(has_error)
template<typename T>
constexpr bool HasMemberFunctionHasError = has_member_function_has_error<const T, bool>::value;

template<class Response>
Result<Response> ResponseResult(Response&& response,
    typename std::enable_if<HasMemberFunctionHasError<Response>, void*>::type = nullptr) {
  // Response has has_error method, use status from it
  if(response.has_error()) {
    return StatusFromPB(response.error().status());
  }
  return std::move(response);
}

template<class Response>
Result<Response> ResponseResult(Response&& response,
    typename std::enable_if<!HasMemberFunctionHasError<Response>, void*>::type = nullptr) {
  // Response has no has_error method, nothing to check
  return std::move(response);
}

}  // anonymous namespace

ClusterAdminClient::ClusterAdminClient(string addrs,
                                       int64_t timeout_millis,
                                       string certs_dir)
    : master_addr_list_(std::move(addrs)),
      timeout_(MonoDelta::FromMilliseconds(timeout_millis)),
      client_init_(certs_dir.empty()),
      initted_(false) {}

ClusterAdminClient::~ClusterAdminClient() {
  if (messenger_) {
    messenger_->Shutdown();
  }
}

Status ClusterAdminClient::Init() {
  CHECK(!initted_);

  // Check if caller will initialize the client and related parts.
  if (client_init_) {
    messenger_ = VERIFY_RESULT(MessengerBuilder("yb-admin").Build());
    yb_client_ = VERIFY_RESULT(YBClientBuilder()
        .add_master_server_addr(master_addr_list_)
        .default_admin_operation_timeout(timeout_)
        .Build(messenger_.get()));
    proxy_cache_ = std::make_unique<rpc::ProxyCache>(messenger_.get());

    // Find the leader master's socket info to set up the master proxy.
    leader_addr_ = yb_client_->GetMasterLeaderAddress();
    master_proxy_.reset(new MasterServiceProxy(proxy_cache_.get(), leader_addr_));
  }

  initted_ = true;
  return Status::OK();
}

Status ClusterAdminClient::MasterLeaderStepDown(const string& leader_uuid) {
  auto master_proxy = std::make_unique<ConsensusServiceProxy>(proxy_cache_.get(), leader_addr_);

  return LeaderStepDown(leader_uuid, yb::master::kSysCatalogTabletId, &master_proxy);
}

Status ClusterAdminClient::LeaderStepDown(
    const PeerId& leader_uuid,
    const TabletId& tablet_id,
    std::unique_ptr<ConsensusServiceProxy>* leader_proxy) {
  LeaderStepDownRequestPB req;
  req.set_dest_uuid(leader_uuid);
  req.set_tablet_id(tablet_id);
  const auto resp = VERIFY_RESULT(InvokeRpcNoResponseCheck(&ConsensusServiceProxy::LeaderStepDown,
      leader_proxy->get(), req));
  if (resp.has_error()) {
    LOG(ERROR) << "LeaderStepDown for " << leader_uuid << "received error "
      << resp.error().ShortDebugString();
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

// Force start an election on a randomly chosen non-leader peer of this tablet's raft quorum.
Status ClusterAdminClient::StartElection(const TabletId& tablet_id) {
  HostPort non_leader_addr;
  string non_leader_uuid;
  RETURN_NOT_OK(SetTabletPeerInfo(tablet_id, FOLLOWER, &non_leader_uuid, &non_leader_addr));
  ConsensusServiceProxy non_leader_proxy(proxy_cache_.get(), non_leader_addr);
  RunLeaderElectionRequestPB req;
  req.set_dest_uuid(non_leader_uuid);
  req.set_tablet_id(tablet_id);
  return ResultToStatus(InvokeRpc(&ConsensusServiceProxy::RunLeaderElection,
      &non_leader_proxy, req));
}

// Look up the location of the tablet server leader or non-leader peer from the leader master
Status ClusterAdminClient::SetTabletPeerInfo(
    const TabletId& tablet_id,
    PeerMode mode,
    PeerId* peer_uuid,
    HostPort* peer_addr) {
  TSInfoPB peer_ts_info;
  RETURN_NOT_OK(GetTabletPeer(tablet_id, mode, &peer_ts_info));
  auto rpc_addresses = peer_ts_info.private_rpc_addresses();
  CHECK_GT(rpc_addresses.size(), 0) << peer_ts_info
        .ShortDebugString();

  *peer_addr = HostPortFromPB(rpc_addresses.Get(0));
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
    HostPort host_port = VERIFY_RESULT(GetFirstRpcAddressForTS(peer_uuid));
    HostPortToPB(host_port, peer_pb.mutable_last_known_private_addr()->Add());
  }

  // Look up the location of the tablet leader from the Master.
  HostPort leader_addr;
  string leader_uuid;
  RETURN_NOT_OK(SetTabletPeerInfo(tablet_id, LEADER, &leader_uuid, &leader_addr));

  auto consensus_proxy = std::make_unique<ConsensusServiceProxy>(proxy_cache_.get(), leader_addr);
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
    consensus_proxy.reset(new ConsensusServiceProxy(proxy_cache_.get(), leader_addr));
  }

  consensus::ChangeConfigRequestPB req;
  req.set_dest_uuid(leader_uuid);
  req.set_tablet_id(tablet_id);
  req.set_type(cc_type);
  *req.mutable_server() = peer_pb;
  return ResultToStatus(InvokeRpc(&ConsensusServiceProxy::ChangeConfig,
      consensus_proxy.get(), req));
}

Status ClusterAdminClient::GetMasterLeaderInfo(PeerId* leader_uuid) {
  const auto list_resp = VERIFY_RESULT(InvokeRpc(&MasterServiceProxy::ListMasters,
      master_proxy_.get(), ListMastersRequestPB()));
  for (const auto& master : list_resp.masters()) {
    if (master.role() == RaftPeerPB::LEADER) {
      CHECK(leader_uuid->empty()) << "Found two LEADER's in the same raft config.";
      *leader_uuid = master.instance_id().permanent_uuid();
    }
  }

  return Status::OK();
}

Status ClusterAdminClient::DumpMasterState(bool to_console) {
  CHECK(initted_);
  master::DumpMasterStateRequestPB req;
  req.set_peers_also(true);
  req.set_on_disk(true);
  req.set_return_dump_as_string(to_console);

  const auto resp = VERIFY_RESULT(InvokeRpc(
      &MasterServiceProxy::DumpState, master_proxy_.get(), req));

  if (to_console) {
    cout << resp.dump() << endl;
  } else {
    cout << "Master state dump has been completed and saved into "
            "the master respective log files." << endl;
  }
  return Status::OK();
}

Status ClusterAdminClient::GetLoadMoveCompletion() {
  CHECK(initted_);
  const auto resp = VERIFY_RESULT(InvokeRpc(
      &MasterServiceProxy::GetLoadMoveCompletion, master_proxy_.get(),
      master::GetLoadMovePercentRequestPB()));
  cout << "Percent complete = " << resp.percent() << " : "
    << resp.remaining() << " remaining out of " << resp.total() << endl;
  return Status::OK();
}

Status ClusterAdminClient::GetLeaderBlacklistCompletion() {
  CHECK(initted_);
  const auto resp = VERIFY_RESULT(InvokeRpc(
      &MasterServiceProxy::GetLeaderBlacklistCompletion, master_proxy_.get(),
      master::GetLeaderBlacklistPercentRequestPB()));
  cout << "Percent complete = " << resp.percent() << " : "
    << resp.remaining() << " remaining out of " << resp.total() << endl;
  return Status::OK();
}

Status ClusterAdminClient::GetIsLoadBalancerIdle() {
  CHECK(initted_);
  const auto resp = VERIFY_RESULT(InvokeRpc(
      &MasterServiceProxy::IsLoadBalancerIdle, master_proxy_.get(),
      master::IsLoadBalancerIdleRequestPB()));

  cout << "Idle = " << !resp.has_error() << endl;
  return Status::OK();
}

Status ClusterAdminClient::ListLeaderCounts(const YBTableName& table_name) {
  vector<string> tablet_ids, ranges;
  RETURN_NOT_OK(yb_client_->GetTablets(table_name, 0, &tablet_ids, &ranges));
  master::GetTabletLocationsRequestPB req;
  for (const auto& tablet_id : tablet_ids) {
    req.add_tablet_ids(tablet_id);
  }
  const auto resp = VERIFY_RESULT(InvokeRpc(&MasterServiceProxy::GetTabletLocations,
      master_proxy_.get(), req));

  unordered_map<string, int> leader_counts;
  int total_leader_count = 0;
  for (const auto& locs : resp.tablet_locations()) {
    for (const auto& replica : locs.replicas()) {
      const auto uuid = replica.ts_info().permanent_uuid();
      switch(replica.role()) {
        case RaftPeerPB::LEADER:
          // If this is a leader, increment leader counts.
          ++leader_counts[uuid];
          ++total_leader_count;
          break;
        case RaftPeerPB::FOLLOWER:
          // If this is a follower, touch the leader count entry also so that tablet server with
          // followers only and 0 leader will be accounted for still.
          leader_counts[uuid];
          break;
        default:
          break;
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
  cout << RightPadToUuidWidth("Server UUID") << kColumnSep << "Leader Count" << endl;
  for (const auto& leader_count : leader_counts) {
    cout << leader_count.first << kColumnSep << leader_count.second << endl;
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
    cout << "Standard deviation: " << stdev << endl;
    cout << "Adjusted deviation percentage: " << percent_dev << "%" << endl;
  }

  return Status::OK();
}

Status ClusterAdminClient::SetupRedisTable() {
  const YBTableName table_name(common::kRedisKeyspaceName, common::kRedisTableName);
  RETURN_NOT_OK(yb_client_->CreateNamespaceIfNotExists(common::kRedisKeyspaceName,
                                                       YQLDatabase::YQL_DATABASE_REDIS));
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
    LOG(ERROR) << s;
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
    int16 peer_port,
    bool use_hostport) {
  CHECK(initted_);

  consensus::ChangeConfigType cc_type;
  RETURN_NOT_OK(ParseChangeType(change_type, &cc_type));

  string peer_uuid;
  if (!use_hostport) {
    RETURN_NOT_OK(yb_client_->GetMasterUUID(peer_host, peer_port, &peer_uuid));
  }

  string leader_uuid;
  RETURN_NOT_OK_PREPEND(GetMasterLeaderInfo(&leader_uuid), "Could not locate master leader");
  if (leader_uuid.empty()) {
    return STATUS(ConfigurationError, "Could not locate master leader!");
  }

  // If removing the leader master, then first make it step down and that
  // starts an election and gets a new leader master.
  auto changed_leader_addr = leader_addr_;
  if (cc_type == consensus::REMOVE_SERVER && leader_uuid == peer_uuid) {
    string old_leader_uuid = leader_uuid;
    RETURN_NOT_OK(MasterLeaderStepDown(leader_uuid));
    sleep(5);  // TODO - wait for exactly the time needed for new leader to get elected.
    // Reget the leader master's socket info to set up the proxy
    leader_addr_ = VERIFY_RESULT(yb_client_->RefreshMasterLeaderAddress());
    master_proxy_.reset(new MasterServiceProxy(proxy_cache_.get(), leader_addr_));
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
    new consensus::ConsensusServiceProxy(proxy_cache_.get(), leader_addr_);
  consensus::ChangeConfigRequestPB req;

  RaftPeerPB peer_pb;
  peer_pb.set_permanent_uuid(peer_uuid);
  // Ignored by ChangeConfig if request != ADD_SERVER.
  peer_pb.set_member_type(RaftPeerPB::PRE_VOTER);
  HostPortPB *peer_host_port = peer_pb.mutable_last_known_private_addr()->Add();
  peer_host_port->set_port(peer_port);
  peer_host_port->set_host(peer_host);
  req.set_dest_uuid(leader_uuid);
  req.set_tablet_id(yb::master::kSysCatalogTabletId);
  req.set_type(cc_type);
  req.set_use_host(use_hostport);
  *req.mutable_server() = peer_pb;

  RETURN_NOT_OK(InvokeRpc(&consensus::ConsensusServiceProxy::ChangeConfig, leader_proxy, req));

  if (cc_type == consensus::ADD_SERVER) {
    RETURN_NOT_OK(yb_client_->AddMasterToClient(changed_leader_addr));
  } else {
    RETURN_NOT_OK(yb_client_->RemoveMasterFromClient(changed_leader_addr));
  }

  return Status::OK();
}

Status ClusterAdminClient::GetTabletLocations(const TabletId& tablet_id,
                                              TabletLocationsPB* locations) {
  master::GetTabletLocationsRequestPB req;
  req.add_tablet_ids(tablet_id);
  const auto resp = VERIFY_RESULT(InvokeRpc(&MasterServiceProxy::GetTabletLocations,
      master_proxy_.get(), req));

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
  auto resp = VERIFY_RESULT(InvokeRpc(&MasterServiceProxy::ListTabletServers, master_proxy_.get(),
      ListTabletServersRequestPB()));
  *servers = std::move(*resp.mutable_servers());
  return Status::OK();
}

Result<HostPort> ClusterAdminClient::GetFirstRpcAddressForTS(const PeerId& uuid) {
  RepeatedPtrField<ListTabletServersResponsePB::Entry> servers;
  RETURN_NOT_OK(ListTabletServers(&servers));
  for (const ListTabletServersResponsePB::Entry& server : servers) {
    if (server.instance_id().permanent_uuid() == uuid) {
      if (!server.has_registration() ||
          server.registration().common().private_rpc_addresses().empty()) {
        break;
      }
      return HostPortFromPB(server.registration().common().private_rpc_addresses(0));
    }
  }

  return STATUS_FORMAT(
      NotFound, "Server with UUID $0 has no RPC address registered with the Master", uuid);
}

Status ClusterAdminClient::ListAllTabletServers() {
  RepeatedPtrField<ListTabletServersResponsePB::Entry> servers;
  RETURN_NOT_OK(ListTabletServers(&servers));

  if (!servers.empty()) {
    cout << RightPadToUuidWidth("Tablet Server UUID") << kColumnSep
         << kRpcHostPortHeading << endl;
  }
  for (const ListTabletServersResponsePB::Entry& server : servers) {
    cout << server.instance_id().permanent_uuid() << kColumnSep
         << FormatFirstHostPort(server.registration().common().private_rpc_addresses())
         << endl;
  }

  return Status::OK();
}

Status ClusterAdminClient::ListAllMasters() {
  const auto lresp = VERIFY_RESULT(InvokeRpc(&MasterServiceProxy::ListMasters,
      master_proxy_.get(), ListMastersRequestPB()));
  if (!lresp.masters().empty()) {
    cout << RightPadToUuidWidth("Master UUID") << kColumnSep
         << RightPadToWidth(kRpcHostPortHeading, kHostPortColWidth)<< kColumnSep
         << "State" << kColumnSep
         << "Role" << endl;
  }
  int i = 0;
  for (const auto& master : lresp.masters()) {
    if (master.role() != consensus::RaftPeerPB::UNKNOWN_ROLE) {
      const auto master_reg = master.has_registration() ? &master.registration() : nullptr;
      cout << (master_reg ? master.instance_id().permanent_uuid()
                          : RightPadToUuidWidth("UNKNOWN_UUID")) << kColumnSep;
      cout << RightPadToWidth(master_reg ? FormatFirstHostPort(master_reg->private_rpc_addresses())
                                         : "UNKNOWN", kHostPortColWidth) << kColumnSep;
      cout << (master.has_error() ? PBEnumToString(master.error().code()) : "ALIVE") << kColumnSep;
      cout << PBEnumToString(master.role()) << endl;
    } else {
      cout << "UNREACHABLE MASTER at index " << i << "." << endl;
    }
    ++i;
  }

  const auto r_resp = VERIFY_RESULT(InvokeRpcNoResponseCheck(
      &MasterServiceProxy::ListMasterRaftPeers, master_proxy_.get(),
      ListMasterRaftPeersRequestPB()));
  if (r_resp.has_error()) {
    return STATUS_FORMAT(RuntimeError, "List Raft Masters RPC response hit error: $0",
        r_resp.error().ShortDebugString());
  }

  if (r_resp.masters_size() != lresp.masters_size()) {
    cout << "WARNING: Mismatch in in-memory masters and raft peers info."
         << "Raft peer info from master leader dumped below." << endl;
    int i = 0;
    for (const auto& master : r_resp.masters()) {
      if (master.member_type() != consensus::RaftPeerPB::UNKNOWN_MEMBER_TYPE) {
        cout << master.permanent_uuid() << "  "
             << master.last_known_private_addr(0).host() << "/"
             << master.last_known_private_addr(0).port() << endl;
      } else {
        cout << "UNREACHABLE MASTER at index " << i << "." << endl;
      }
      ++i;
    }
  }

  return Status::OK();
}

Status ClusterAdminClient::ListTabletServersLogLocations() {
  RepeatedPtrField<ListTabletServersResponsePB::Entry> servers;
  RETURN_NOT_OK(ListTabletServers(&servers));

  if (!servers.empty()) {
    cout << RightPadToUuidWidth("TS UUID") << kColumnSep
         << kRpcHostPortHeading << kColumnSep
         << "LogLocation"
         << endl;
  }

  for (const ListTabletServersResponsePB::Entry& server : servers) {
    auto ts_uuid = server.instance_id().permanent_uuid();

    HostPort ts_addr = VERIFY_RESULT(GetFirstRpcAddressForTS(ts_uuid));

    TabletServerServiceProxy ts_proxy(proxy_cache_.get(), ts_addr);

    const auto resp = VERIFY_RESULT(InvokeRpc(&TabletServerServiceProxy::GetLogLocation,
        &ts_proxy, tserver::GetLogLocationRequestPB()));
    cout << ts_uuid << kColumnSep
         << ts_addr << kColumnSep
         << resp.log_location() << endl;
  }

  return Status::OK();
}

const char* DatabasePrefix(YQLDatabase db) {
  switch(db) {
    case YQL_DATABASE_UNKNOWN: break;
    case YQL_DATABASE_CQL: return kDBTypePrefixCql;
    case YQL_DATABASE_PGSQL: return kDBTypePrefixYsql;
    case YQL_DATABASE_REDIS: return kDBTypePrefixRedis;
  }
  CHECK(false) << "Unexpected db type " << db;
  return kDBTypePrefixUnknown;
}

Status ClusterAdminClient::ListTables(bool include_db_type) {
  vector<YBTableName> tables;
  RETURN_NOT_OK(yb_client_->ListTables(&tables));
  if (include_db_type) {
    vector<string> full_names;
    const auto& namespace_metadata = VERIFY_RESULT_REF(GetNamespaceMap());
    for (const auto& table : tables) {
      const auto db_type_iter = namespace_metadata.find(table.namespace_id());
      if (db_type_iter != namespace_metadata.end()) {
        full_names.push_back(Format("$0.$1",
                               DatabasePrefix(db_type_iter->second.database_type()),
                               table));
      } else {
        LOG(WARNING) << "Table in unknown namespace found "
                     << table.ToString()
                     << ", probably it has been just created";
      }
    }
    sort(full_names.begin(), full_names.end());
    copy(full_names.begin(), full_names.end(), std::ostream_iterator<string>(cout, "\n"));
  } else {
    for (const auto& table : tables) {
      cout << table.ToString() << endl;
    }
  }
  return Status::OK();
}

Status ClusterAdminClient::ListTablets(const YBTableName& table_name, int max_tablets) {
  vector<string> tablet_uuids, ranges;
  std::vector<master::TabletLocationsPB> locations;
  RETURN_NOT_OK(yb_client_->GetTablets(
      table_name, max_tablets, &tablet_uuids, &ranges, &locations));
  cout << RightPadToUuidWidth("Tablet UUID") << kColumnSep
       << RightPadToWidth("Range", kPartitionRangeColWidth) << kColumnSep
       << "Leader" << endl;
  for (int i = 0; i < tablet_uuids.size(); i++) {
    string tablet_uuid = tablet_uuids[i];
    string leader_host_port;
    const auto& locations_of_this_tablet = locations[i];
    for (const auto& replica : locations_of_this_tablet.replicas()) {
      if (replica.role() == RaftPeerPB::Role::RaftPeerPB_Role_LEADER) {
        if (leader_host_port.empty()) {
          leader_host_port = FormatHostPort(replica.ts_info().private_rpc_addresses(0));
        } else {
          LOG(ERROR) << "Multiple leader replicas found for tablet " << tablet_uuid
                     << ": " << locations_of_this_tablet.ShortDebugString();
        }
      }
    }
    cout << tablet_uuid << kColumnSep
         << RightPadToWidth(ranges[i], kPartitionRangeColWidth) << kColumnSep
         << leader_host_port << endl;
  }
  return Status::OK();
}

Status ClusterAdminClient::ListPerTabletTabletServers(const TabletId& tablet_id) {
  master::GetTabletLocationsRequestPB req;
  req.add_tablet_ids(tablet_id);
  const auto resp = VERIFY_RESULT(InvokeRpc(&MasterServiceProxy::GetTabletLocations,
      master_proxy_.get(), req));

  if (resp.tablet_locations_size() != 1) {
    if (resp.tablet_locations_size() > 0) {
      std::cerr << "List of all incorrect locations - " << resp.tablet_locations_size()
                << " : " << endl;
      const auto limit = std::min(resp.tablet_locations_size(), MAX_NUM_ELEMENTS_TO_SHOW_ON_ERROR);
      for (int i = 0; i < limit; ++i) {
        std::cerr << i << " : " << resp.tablet_locations(i).DebugString();
      }
      std::cerr << endl;
    }
    return STATUS_FORMAT(IllegalState,
                         "Incorrect number of locations $0 for tablet $1.",
                         resp.tablet_locations_size(), tablet_id);
  }

  TabletLocationsPB locs = resp.tablet_locations(0);
  if (!locs.replicas().empty()) {
    cout << RightPadToUuidWidth("Server UUID") << kColumnSep
         << RightPadToWidth(kRpcHostPortHeading, kHostPortColWidth) << kColumnSep
         << "Role" << endl;
  }
  for (const auto& replica : locs.replicas()) {
    cout << replica.ts_info().permanent_uuid() << kColumnSep
         << RightPadToWidth(FormatHostPort(replica.ts_info().private_rpc_addresses(0)),
                            kHostPortColWidth) << kColumnSep
         << PBEnumToString(replica.role()) << endl;
  }

  return Status::OK();
}

Status ClusterAdminClient::DeleteTable(const YBTableName& table_name) {
  RETURN_NOT_OK(yb_client_->DeleteTable(table_name));
  cout << "Deleted table " << table_name.ToString() << endl;
  return Status::OK();
}

Status ClusterAdminClient::ListTabletsForTabletServer(const PeerId& ts_uuid) {
  auto ts_addr = VERIFY_RESULT(GetFirstRpcAddressForTS(ts_uuid));

  TabletServerServiceProxy ts_proxy(proxy_cache_.get(), ts_addr);

  const auto resp = VERIFY_RESULT(InvokeRpc(&TabletServerServiceProxy::ListTabletsForTabletServer,
      &ts_proxy, tserver::ListTabletsForTabletServerRequestPB()));

  cout << RightPadToWidth("Table name", kTableNameColWidth) << kColumnSep
       << RightPadToUuidWidth("Tablet ID") << kColumnSep
       << "Is Leader" << kColumnSep
       << "State" << kColumnSep
       << "Num SST Files" << kColumnSep
       << "Num Log Segments" << endl;
  for (const auto& entry : resp.entries()) {
    cout << RightPadToWidth(entry.table_name(), kTableNameColWidth) << kColumnSep
         << RightPadToUuidWidth(entry.tablet_id()) << kColumnSep
         << entry.is_leader() << kColumnSep
         << PBEnumToString(entry.state()) << kColumnSep
         << entry.num_sst_files() << kColumnSep
         << entry.num_log_segments() << endl;
  }
  return Status::OK();
}

Status ClusterAdminClient::SetLoadBalancerEnabled(bool is_enabled) {
  const auto list_resp = VERIFY_RESULT(InvokeRpc(&MasterServiceProxy::ListMasters,
      master_proxy_.get(), ListMastersRequestPB()));

  master::ChangeLoadBalancerStateRequestPB req;
  req.set_is_enabled(is_enabled);
  for (const auto& master : list_resp.masters()) {

    if (master.role() == RaftPeerPB::LEADER) {
      RETURN_NOT_OK(InvokeRpc(&MasterServiceProxy::ChangeLoadBalancerState,
          master_proxy_.get(), req));
    } else {
      HostPortPB hp_pb = master.registration().private_rpc_addresses(0);

      MasterServiceProxy proxy(proxy_cache_.get(), HostPortFromPB(hp_pb));
      RETURN_NOT_OK(InvokeRpc(&MasterServiceProxy::ChangeLoadBalancerState, &proxy, req));
    }
  }

  return Status::OK();
}

Status ClusterAdminClient::FlushTable(const YBTableName& table_name,
                                      int timeout_secs,
                                      bool is_compaction) {
  FlushTablesRequestPB req;
  req.set_is_compaction(is_compaction);
  table_name.SetIntoTableIdentifierPB(req.add_tables());
  const auto resp = VERIFY_RESULT(InvokeRpc(&MasterServiceProxy::FlushTables,
      master_proxy_.get(), req));

  if (is_compaction) {
    cout << "Started compaction of table " << table_name.ToString() << endl
         << "Compaction request id: " << resp.flush_request_id() << endl;
  } else {
    cout << "Started flushing table " << table_name.ToString() << endl
         << "Flush request id: " << resp.flush_request_id() << endl;
  }

  IsFlushTablesDoneRequestPB wait_req;
  // Wait for table creation.
  wait_req.set_flush_request_id(resp.flush_request_id());

  for (int k = 0; k < timeout_secs; ++k) {
    const auto wait_resp = VERIFY_RESULT(InvokeRpcNoResponseCheck(
        &MasterServiceProxy::IsFlushTablesDone, master_proxy_.get(), wait_req));

    if (wait_resp.has_error()) {
      if (wait_resp.error().status().code() == AppStatusPB::NOT_FOUND) {
        cout << (is_compaction ? "Compaction" : "Flush") << " request was deleted: "
             << resp.flush_request_id() << endl;
      }

      return StatusFromPB(wait_resp.error().status());
    }

    if (wait_resp.done()) {
      cout << (is_compaction ? "Compaction" : "Flushing") << " complete: "
           << (wait_resp.success() ? "SUCCESS" : "FAILED") << endl;
      return Status::OK();
    }

    cout << "Waiting for " << (is_compaction ? "compaction..." : "flushing...")
         << (wait_resp.success() ? "" : " Already FAILED") << endl;
    std::this_thread::sleep_for(1s);
  }

  return STATUS(TimedOut,
      Substitute("Expired timeout ($0 seconds) for table $1 $2",
          timeout_secs, table_name.ToString(), is_compaction ? "compaction" : "flushing"));
}

Status ClusterAdminClient::WaitUntilMasterLeaderReady() {
  for(int iter = 0; iter < kNumberOfTryouts; ++iter) {
    const auto res_leader_ready = VERIFY_RESULT(InvokeRpcNoResponseCheck(
        &MasterServiceProxy::IsMasterLeaderServiceReady,
        master_proxy_.get(),  master::IsMasterLeaderReadyRequestPB(),
        "MasterServiceImpl::IsMasterLeaderServiceReady call failed."));
    if (!res_leader_ready.has_error()) {
      return Status::OK();
    }
    sleep(kSleepTimeSec);
  }
  return STATUS(TimedOut, "ClusterAdminClient::WaitUntilMasterLeaderReady timed out.");
}

Status ClusterAdminClient::AddReadReplicaPlacementInfo(
    const string& placement_info, int replication_factor, const std::string& optional_uuid) {
  RETURN_NOT_OK_PREPEND(WaitUntilMasterLeaderReady(), "Wait for master leader failed!");

  // Get the cluster config from the master leader.
  auto resp_cluster_config = VERIFY_RESULT(GetMasterClusterConfig());

  auto* cluster_config = resp_cluster_config.mutable_cluster_config();
  if (cluster_config->replication_info().read_replicas_size() > 0) {
    return STATUS(InvalidCommand, "Already have a read replica placement, cannot add another.");
  }
  auto* read_replica_config = cluster_config->mutable_replication_info()->add_read_replicas();

  // If optional_uuid is set, make that the placement info, otherwise generate a random one.
  string uuid_str = optional_uuid;
  if (optional_uuid.empty()) {
    uuid_str = RandomHumanReadableString(16);
  }
  read_replica_config->set_num_replicas(replication_factor);
  read_replica_config->set_placement_uuid(uuid_str);

  // Fill in the placement info with new stuff.
  RETURN_NOT_OK(FillPlacementInfo(read_replica_config, placement_info));

  master::ChangeMasterClusterConfigRequestPB req_new_cluster_config;

  *req_new_cluster_config.mutable_cluster_config() = *cluster_config;

  RETURN_NOT_OK(InvokeRpc(&MasterServiceProxy::ChangeMasterClusterConfig,
                          master_proxy_.get(), req_new_cluster_config,
                          "MasterServiceImpl::ChangeMasterClusterConfig call failed."));

  LOG(INFO)<< "Created read replica placement with uuid: " << uuid_str;
  return Status::OK();
}

CHECKED_STATUS ClusterAdminClient::ModifyReadReplicaPlacementInfo(
    const std::string& placement_uuid, const std::string& placement_info, int replication_factor) {
  RETURN_NOT_OK_PREPEND(WaitUntilMasterLeaderReady(), "Wait for master leader failed!");

  // Get the cluster config from the master leader.
  auto master_resp = VERIFY_RESULT(GetMasterClusterConfig());
  auto* cluster_config = master_resp.mutable_cluster_config();

  auto* replication_info = cluster_config->mutable_replication_info();
  if (replication_info->read_replicas_size() == 0) {
    return STATUS(InvalidCommand, "No read replica placement info to modify.");
  }

  auto* read_replica_config = replication_info->mutable_read_replicas(0);

  std::string config_placement_uuid;
  if (placement_uuid.empty())  {
    // If there is no placement_uuid set, use the existing uuid.
    config_placement_uuid = read_replica_config->placement_uuid();
  } else {
    // Otherwise, use the passed in value.
    config_placement_uuid = placement_uuid;
  }

  read_replica_config->Clear();

  read_replica_config->set_num_replicas(replication_factor);
  read_replica_config->set_placement_uuid(config_placement_uuid);
  RETURN_NOT_OK(FillPlacementInfo(read_replica_config, placement_info));

  master::ChangeMasterClusterConfigRequestPB req_new_cluster_config;

  *req_new_cluster_config.mutable_cluster_config() = *cluster_config;

  RETURN_NOT_OK(InvokeRpc(&MasterServiceProxy::ChangeMasterClusterConfig,
                          master_proxy_.get(), req_new_cluster_config,
                          "MasterServiceImpl::ChangeMasterClusterConfig call failed."));

  LOG(INFO)<< "Changed read replica placement.";
  return Status::OK();
}

CHECKED_STATUS ClusterAdminClient::DeleteReadReplicaPlacementInfo() {
  RETURN_NOT_OK_PREPEND(WaitUntilMasterLeaderReady(), "Wait for master leader failed!");

  auto master_resp = VERIFY_RESULT(GetMasterClusterConfig());
  auto* cluster_config = master_resp.mutable_cluster_config();

  auto* replication_info = cluster_config->mutable_replication_info();
  if (replication_info->read_replicas_size() == 0) {
    return STATUS(InvalidCommand, "No read replica placement info to delete.");
  }

  replication_info->clear_read_replicas();

  master::ChangeMasterClusterConfigRequestPB req_new_cluster_config;

  *req_new_cluster_config.mutable_cluster_config() = *cluster_config;

  RETURN_NOT_OK(InvokeRpc(&MasterServiceProxy::ChangeMasterClusterConfig,
                          master_proxy_.get(), req_new_cluster_config,
                          "MasterServiceImpl::ChangeMasterClusterConfig call failed."));

  LOG(INFO)<< "Deleted read replica placement.";
  return Status::OK();
}

Status ClusterAdminClient::FillPlacementInfo(
    master::PlacementInfoPB* placement_info_pb, const string& placement_str) {

  std::vector<std::string> placement_info_split = strings::Split(
      placement_str, ",", strings::SkipEmpty());
  if (placement_info_split.size() < 1) {
    return STATUS(InvalidCommand, "Cluster config must be a list of "
                                  "placement infos seperated by commas. "
                                  "Format: 'cloud1.region1.zone1:rf,cloud2.region2.zone2:rf, ..."
        + std::to_string(placement_info_split.size()));
  }

  for (int iter = 0; iter < placement_info_split.size(); iter++) {
    std::vector<std::string> placement_block = strings::Split(placement_info_split[iter], ":",
                                                              strings::SkipEmpty());

    if (placement_block.size() != 2) {
      return STATUS(InvalidCommand, "Each placement info must be in format placement:rf");
    }

    int min_num_replicas = boost::lexical_cast<int>(placement_block[1]);

    std::vector<std::string> block = strings::Split(placement_block[0], ".",
                                                    strings::SkipEmpty());
    if (block.size() != 3) {
      return STATUS(InvalidCommand,
          "Each placement info must have exactly 3 values seperated"
          "by dots that denote cloud, region and zone. Block: " + placement_info_split[iter]
          + " is invalid");
    }
    auto pb = placement_info_pb->add_placement_blocks();
    pb->mutable_cloud_info()->set_placement_cloud(block[0]);
    pb->mutable_cloud_info()->set_placement_region(block[1]);
    pb->mutable_cloud_info()->set_placement_zone(block[2]);

    pb->set_min_num_replicas(min_num_replicas);
  }

  return Status::OK();
}

Status ClusterAdminClient::ModifyPlacementInfo(
    std::string placement_info, int replication_factor, const std::string& optional_uuid) {

  // Wait to make sure that master leader is ready.
  RETURN_NOT_OK_PREPEND(WaitUntilMasterLeaderReady(), "Wait for master leader failed!");

  // Get the cluster config from the master leader.
  auto resp_cluster_config = VERIFY_RESULT(GetMasterClusterConfig());

  // Create a new cluster config.
  std::vector<std::string> placement_info_split = strings::Split(
      placement_info, ",", strings::SkipEmpty());
  if (placement_info_split.size() < 1) {
    return STATUS(InvalidCommand, "Cluster config must be a list of "
    "placement infos seperated by commas. "
    "Format: 'cloud1.region1.zone1,cloud2.region2.zone2,cloud3.region3.zone3 ..."
    + std::to_string(placement_info_split.size()));
  }
  master::ChangeMasterClusterConfigRequestPB req_new_cluster_config;
  master::SysClusterConfigEntryPB* sys_cluster_config_entry =
      resp_cluster_config.mutable_cluster_config();
  master::PlacementInfoPB* live_replicas = new master::PlacementInfoPB;
  live_replicas->set_num_replicas(replication_factor);
  // Iterate over the placement blocks of the placementInfo structure.
  for (int iter = 0; iter < placement_info_split.size(); iter++) {
    std::vector<std::string> block = strings::Split(placement_info_split[iter], ".",
                                                    strings::SkipEmpty());
    if (block.size() != 3) {
      return STATUS(InvalidCommand, "Each placement info must have exactly 3 values seperated"
          "by dots that denote cloud, region and zone. Block: " + placement_info_split[iter]
          + " is invalid");
    }
    auto pb = live_replicas->add_placement_blocks();
    pb->mutable_cloud_info()->set_placement_cloud(block[0]);
    pb->mutable_cloud_info()->set_placement_region(block[1]);
    pb->mutable_cloud_info()->set_placement_zone(block[2]);
    pb->set_min_num_replicas(1);
  }

  if (!optional_uuid.empty()) {
    // If we have an optional uuid, set it.
    live_replicas->set_placement_uuid(optional_uuid);
  } else if (sys_cluster_config_entry->replication_info().live_replicas().has_placement_uuid()) {
    // Otherwise, if we have an existing placement uuid, use that.
    live_replicas->set_placement_uuid(
        sys_cluster_config_entry->replication_info().live_replicas().placement_uuid());
  }

  sys_cluster_config_entry->mutable_replication_info()->set_allocated_live_replicas(live_replicas);
  req_new_cluster_config.mutable_cluster_config()->CopyFrom(*sys_cluster_config_entry);

  RETURN_NOT_OK(InvokeRpc(&MasterServiceProxy::ChangeMasterClusterConfig,
      master_proxy_.get(), req_new_cluster_config,
      "MasterServiceImpl::ChangeMasterClusterConfig call failed."));

  LOG(INFO)<< "Changed master cluster config.";
  return Status::OK();
}

Status ClusterAdminClient::GetUniverseConfig() {
  const auto cluster_config = VERIFY_RESULT(GetMasterClusterConfig());
  cout << "Config: \r\n"  << cluster_config.cluster_config().DebugString() << endl;
  return Status::OK();
}

Status ClusterAdminClient::ChangeBlacklist(const std::vector<HostPort>& servers, bool add,
    bool blacklist_leader) {
  auto config = VERIFY_RESULT(GetMasterClusterConfig());
  auto& cluster_config = *config.mutable_cluster_config();
  auto& blacklist = (blacklist_leader) ?
    *cluster_config.mutable_leader_blacklist() :
    *cluster_config.mutable_server_blacklist();
  std::vector<HostPort> result_blacklist;
  for (const auto& host : blacklist.hosts()) {
    const HostPort hostport(host.host(), host.port());
    if (std::find(servers.begin(), servers.end(), hostport) == servers.end()) {
      result_blacklist.emplace_back(host.host(), host.port());
    }
  }
  if (add) {
    result_blacklist.insert(result_blacklist.end(), servers.begin(), servers.end());
  }
  auto result_begin = result_blacklist.begin(), result_end = result_blacklist.end();
  std::sort(result_begin, result_end);
  result_blacklist.erase(std::unique(result_begin, result_end), result_end);
  blacklist.clear_hosts();
  for (const auto& hostport : result_blacklist) {
    auto& new_host = *blacklist.add_hosts();
    new_host.set_host(hostport.host());
    new_host.set_port(hostport.port());
  }
  master::ChangeMasterClusterConfigRequestPB req_new_cluster_config;
  req_new_cluster_config.mutable_cluster_config()->Swap(&cluster_config);
  return ResultToStatus(InvokeRpc(&MasterServiceProxy::ChangeMasterClusterConfig,
                                  master_proxy_.get(), req_new_cluster_config,
                                  "MasterServiceImpl::ChangeMasterClusterConfig call failed."));
}

Result<const master::NamespaceIdentifierPB&> ClusterAdminClient::GetNamespaceInfo(
    YQLDatabase db_type, const std::string& namespace_name) {
  LOG(INFO) << Format("Resolving namespace id for '$0' of type '$1'",
                      namespace_name, DatabasePrefix(db_type));
  for (const auto& item : VERIFY_RESULT_REF(GetNamespaceMap())) {
    const auto& namespace_info = item.second;
    if (namespace_info.database_type() == db_type && namespace_name == namespace_info.name()) {
      return namespace_info;
    }
  }
  return STATUS(NotFound,
                Format("Namespace '$0' of type '$1' not found",
                       namespace_name, DatabasePrefix(db_type)));
}

Result<master::GetMasterClusterConfigResponsePB> ClusterAdminClient::GetMasterClusterConfig() {
  return InvokeRpc(&MasterServiceProxy::GetMasterClusterConfig, master_proxy_.get(),
                   master::GetMasterClusterConfigRequestPB(),
                   "MasterServiceImpl::GetMasterClusterConfig call failed.");
}

template<class Response, class Request, class Object>
Result<Response> ClusterAdminClient::InvokeRpcNoResponseCheck(
    Status (Object::*func)(const Request&, Response*, rpc::RpcController*),
    Object* obj, const Request& req, const char* error_message) {
  rpc::RpcController rpc;
  rpc.set_timeout(timeout_);
  Response response;
  auto result = (obj->*func)(req, &response, &rpc);
  if (error_message) {
    RETURN_NOT_OK_PREPEND(result, error_message);
  } else {
    RETURN_NOT_OK(result);
  }
  return std::move(response);
}

template<class Response, class Request, class Object>
Result<Response> ClusterAdminClient::InvokeRpc(
    Status (Object::*func)(const Request&, Response*, rpc::RpcController*),
    Object* obj, const Request& req, const char* error_message) {
  return ResponseResult(VERIFY_RESULT(InvokeRpcNoResponseCheck(func, obj, req, error_message)));
}

Result<const ClusterAdminClient::NamespaceMap&> ClusterAdminClient::GetNamespaceMap() {
  if (namespace_map_.empty()) {
    auto v = VERIFY_RESULT(yb_client_->ListNamespaces());
    for (auto& ns : v) {
      namespace_map_.emplace(string(ns.id()), std::move(ns));
    }
  }
  return const_cast<const ClusterAdminClient::NamespaceMap&>(namespace_map_);
}

string RightPadToUuidWidth(const string &s) {
  return RightPadToWidth(s, kNumCharactersInUuid);
}

Result<TypedNamespaceName> ParseNamespaceName(const std::string& full_namespace_name) {
  YQLDatabase db_type = YQL_DATABASE_UNKNOWN;
  const size_t dot_pos = full_namespace_name.find('.');
  if (dot_pos != string::npos) {
    static const std::array<pair<const char*, YQLDatabase>, 3> type_prefixes{
        make_pair(kDBTypePrefixCql, YQL_DATABASE_CQL),
        make_pair(kDBTypePrefixYsql, YQL_DATABASE_PGSQL),
        make_pair(kDBTypePrefixRedis, YQL_DATABASE_REDIS)};
    const Slice namespace_type(full_namespace_name.data(), dot_pos);
    for (const auto& prefix : type_prefixes) {
      if (namespace_type == prefix.first) {
        db_type = prefix.second;
        break;
      }
    }
    if (db_type == YQL_DATABASE_UNKNOWN) {
      return STATUS(InvalidArgument, Format("Invalid db type name '$0'", namespace_type));
    }
  } else {
    db_type = (full_namespace_name == common::kRedisKeyspaceName ? YQL_DATABASE_REDIS
        : YQL_DATABASE_CQL);
  }

  const size_t name_start = (dot_pos == string::npos ? 0 : (dot_pos + 1));
  return TypedNamespaceName{
      db_type,
      std::string(full_namespace_name.data() + name_start,
                  full_namespace_name.length() - name_start)};
}

}  // namespace tools
}  // namespace yb
