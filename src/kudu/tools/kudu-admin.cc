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
// Tool to administer a cluster from the CLI.

#include <boost/optional.hpp>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <iostream>
#include <memory>
#include <strstream>

#include "kudu/client/client.h"
#include "kudu/common/wire_protocol.h"
#include "kudu/consensus/consensus.pb.h"
#include "kudu/consensus/consensus.proxy.h"
#include "kudu/gutil/strings/split.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/master/master.h"
#include "kudu/master/master.pb.h"
#include "kudu/master/master.proxy.h"
#include "kudu/tserver/tablet_server.h"
#include "kudu/util/env.h"
#include "kudu/util/flags.h"
#include "kudu/util/logging.h"
#include "kudu/util/net/net_util.h"
#include "kudu/util/net/sockaddr.h"
#include "kudu/util/string_case.h"
#include "kudu/rpc/messenger.h"
#include "kudu/rpc/rpc_controller.h"

DEFINE_string(master_addresses, "localhost",
              "Comma-separated list of Kudu Master server addresses");
DEFINE_int64(timeout_ms, 1000 * 60, "RPC timeout in milliseconds");

#define EXIT_NOT_OK_PREPEND(status, msg) \
  do { \
    Status _s = (status); \
    if (PREDICT_FALSE(!_s.ok())) { \
      std::cerr << _s.CloneAndPrepend(msg).ToString() << std::endl; \
      google::ShowUsageWithFlagsRestrict(g_progname, __FILE__); \
      exit(1); \
    } \
  } while (0)

namespace kudu {
namespace tools {

using std::ostringstream;
using std::string;
using std::vector;

using google::protobuf::RepeatedPtrField;

using client::KuduClient;
using client::KuduClientBuilder;
using client::KuduTabletServer;
using consensus::ConsensusServiceProxy;
using consensus::RaftPeerPB;
using master::ListTabletServersRequestPB;
using master::ListTabletServersResponsePB;
using master::MasterServiceProxy;
using master::TabletLocationsPB;
using master::TSInfoPB;
using rpc::Messenger;
using rpc::MessengerBuilder;
using rpc::RpcController;
using strings::Split;
using strings::Substitute;

const char* const kChangeConfigOp = "change_config";
const char* const kListTablesOp = "list_tables";
const char* const kDeleteTableOp = "delete_table";
static const char* g_progname = nullptr;

class ClusterAdminClient {
 public:
  // Creates an admin client for host/port combination e.g.,
  // "localhost" or "127.0.0.1:7050".
  ClusterAdminClient(std::string addrs, int64_t timeout_millis);

  // Initialized the client and connects to the specified tablet
  // server.
  Status Init();

  // Change the configuration of the specified tablet.
  Status ChangeConfig(const string& tablet_id,
                      const string& change_type,
                      const string& peer_uuid,
                      const boost::optional<string>& member_type);

  // List all the tables.
  Status ListTables();

  // Delete a single table by name.
  Status DeleteTable(const string& table_name);

 private:
  // Fetch the locations of the replicas for a given tablet from the Master.
  Status GetTabletLocations(const std::string& tablet_id,
                            TabletLocationsPB* locations);

  // Fetch information about the location of the tablet leader from the Master.
  Status GetTabletLeader(const std::string& tablet_id, TSInfoPB* ts_info);

  // Fetch the latest list of tablet servers from the Master.
  Status ListTabletServers(RepeatedPtrField<ListTabletServersResponsePB::Entry>* servers);

  // Look up the RPC address of the server with the specified UUID from the Master.
  Status GetFirstRpcAddressForTS(const std::string& uuid, HostPort* hp);

  const std::string master_addr_list_;
  const MonoDelta timeout_;

  bool initted_;
  std::shared_ptr<rpc::Messenger> messenger_;
  gscoped_ptr<MasterServiceProxy> master_proxy_;
  client::sp::shared_ptr<KuduClient> kudu_client_;

  DISALLOW_COPY_AND_ASSIGN(ClusterAdminClient);
};

ClusterAdminClient::ClusterAdminClient(string addrs, int64_t timeout_millis)
    : master_addr_list_(std::move(addrs)),
      timeout_(MonoDelta::FromMilliseconds(timeout_millis)),
      initted_(false) {}

Status ClusterAdminClient::Init() {
  CHECK(!initted_);

  // Build master proxy.
  // TODO: Support multi-master by adding replica lookup support to the client.
  vector<string> master_addr_strings = Split(master_addr_list_, ",");
  if (master_addr_strings.size() != 1) {
    return Status::InvalidArgument("This tool does not yet support multiple masters. "
                                   "Please specify only the leader master address in "
                                   "-master_addresses");
  }

  HostPort master_hostport;
  RETURN_NOT_OK(master_hostport.ParseString(master_addr_strings[0],
                                            master::Master::kDefaultPort));
  MessengerBuilder builder("kudu-admin");
  RETURN_NOT_OK(builder.Build(&messenger_));
  vector<Sockaddr> master_addrs;
  RETURN_NOT_OK(master_hostport.ResolveAddresses(&master_addrs));
  CHECK(!master_addrs.empty()) << "Unable to resolve IP address for master host: "
                               << master_hostport.ToString();
  master_proxy_.reset(new MasterServiceProxy(messenger_, master_addrs[0]));

  CHECK_OK(KuduClientBuilder()
      .add_master_server_addr(master_addr_list_)
      .default_admin_operation_timeout(timeout_)
      .Build(&kudu_client_));

  initted_ = true;
  return Status::OK();
}

Status ClusterAdminClient::ChangeConfig(const string& tablet_id,
                                        const string& change_type,
                                        const string& peer_uuid,
                                        const boost::optional<string>& member_type) {
  CHECK(initted_);

  // Parse the change type.
  consensus::ChangeConfigType cc_type = consensus::UNKNOWN_CHANGE;
  string uppercase_change_type;
  ToUpperCase(change_type, &uppercase_change_type);
  if (!consensus::ChangeConfigType_Parse(uppercase_change_type, &cc_type) ||
      cc_type == consensus::UNKNOWN_CHANGE) {
    return Status::InvalidArgument("Unsupported change_type", change_type);
  }

  RaftPeerPB peer_pb;
  peer_pb.set_permanent_uuid(peer_uuid);

  // Parse the optional fields.
  if (member_type) {
    RaftPeerPB::MemberType member_type_val;
    string uppercase_member_type;
    ToUpperCase(*member_type, &uppercase_member_type);
    if (!RaftPeerPB::MemberType_Parse(uppercase_member_type, &member_type_val)) {
      return Status::InvalidArgument("Unrecognized member_type", *member_type);
    }
    peer_pb.set_member_type(member_type_val);
  }

  // Validate the existence of the optional fields.
  if (!member_type && (cc_type == consensus::ADD_SERVER || cc_type == consensus::CHANGE_ROLE)) {
    return Status::InvalidArgument("Must specify member_type when adding "
                                   "a server or changing a role");
  }

  // Look up RPC address of peer if adding as a new server.
  if (cc_type == consensus::ADD_SERVER) {
    HostPort host_port;
    RETURN_NOT_OK(GetFirstRpcAddressForTS(peer_uuid, &host_port));
    RETURN_NOT_OK(HostPortToPB(host_port, peer_pb.mutable_last_known_addr()));
  }

  // Look up the location of the tablet leader from the Master.
  TSInfoPB leader_ts_info;
  RETURN_NOT_OK(GetTabletLeader(tablet_id, &leader_ts_info));
  CHECK_GT(leader_ts_info.rpc_addresses_size(), 0) << leader_ts_info.ShortDebugString();

  HostPort leader_hostport;
  RETURN_NOT_OK(HostPortFromPB(leader_ts_info.rpc_addresses(0), &leader_hostport));
  vector<Sockaddr> leader_addrs;
  RETURN_NOT_OK(leader_hostport.ResolveAddresses(&leader_addrs));
  CHECK(!leader_addrs.empty()) << "Unable to resolve IP address for tablet leader host: "
                               << leader_hostport.ToString();
  gscoped_ptr<ConsensusServiceProxy> consensus_proxy(
      new ConsensusServiceProxy(messenger_, leader_addrs[0]));

  consensus::ChangeConfigRequestPB req;
  consensus::ChangeConfigResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(timeout_);

  req.set_dest_uuid(leader_ts_info.permanent_uuid());
  req.set_tablet_id(tablet_id);
  req.set_type(cc_type);
  *req.mutable_server() = peer_pb;

  RETURN_NOT_OK(consensus_proxy->ChangeConfig(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Status ClusterAdminClient::GetTabletLocations(const string& tablet_id,
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

Status ClusterAdminClient::GetTabletLeader(const string& tablet_id,
                                           TSInfoPB* ts_info) {
  TabletLocationsPB locations;
  RETURN_NOT_OK(GetTabletLocations(tablet_id, &locations));
  CHECK_EQ(tablet_id, locations.tablet_id()) << locations.ShortDebugString();
  bool found = false;
  for (const TabletLocationsPB::ReplicaPB& replica : locations.replicas()) {
    if (replica.role() == RaftPeerPB::LEADER) {
      *ts_info = replica.ts_info();
      found = true;
      break;
    }
  }
  if (!found) {
    return Status::NotFound("No leader replica found for tablet", tablet_id);
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

Status ClusterAdminClient::GetFirstRpcAddressForTS(const std::string& uuid, HostPort* hp) {
  RepeatedPtrField<ListTabletServersResponsePB::Entry> servers;
  RETURN_NOT_OK(ListTabletServers(&servers));
  for (const ListTabletServersResponsePB::Entry& server : servers) {
    if (server.instance_id().permanent_uuid() == uuid) {
      if (!server.has_registration() || server.registration().rpc_addresses_size() == 0) {
        break;
      }
      RETURN_NOT_OK(HostPortFromPB(server.registration().rpc_addresses(0), hp));
      return Status::OK();
    }
  }

  return Status::NotFound(Substitute("Server with UUID $0 has no RPC address "
                                     "registered with the Master", uuid));
}

Status ClusterAdminClient::ListTables() {
  vector<string> tables;
  RETURN_NOT_OK(kudu_client_->ListTables(&tables));
  for (const string& table : tables) {
    std::cout << table << std::endl;
  }
  return Status::OK();
}

Status ClusterAdminClient::DeleteTable(const string& table_name) {
  vector<Sockaddr> tables;
  RETURN_NOT_OK(kudu_client_->DeleteTable(table_name));
  std::cout << "Deleted table " << table_name << std::endl;
  return Status::OK();
}

static void SetUsage(const char* argv0) {
  ostringstream str;

  str << argv0 << " [-master_addresses server1,server2,server3] <operation> <operation-arguments>\n"
      << "<operation> must be one of:\n"
      << "  " << kChangeConfigOp << " <tablet_id> "
              << "<ADD_SERVER|REMOVE_SERVER|CHANGE_ROLE> <peer_uuid> "
              << "[VOTER|NON_VOTER]" << std::endl
      << "  " << kListTablesOp << std::endl
      << "  " << kDeleteTableOp << " <table_name>";
  google::SetUsageMessage(str.str());
}

static string GetOp(int argc, char** argv) {
  if (argc < 2) {
    google::ShowUsageWithFlagsRestrict(argv[0], __FILE__);
    exit(1);
  }

  return argv[1];
}

static int ClusterAdminCliMain(int argc, char** argv) {
  g_progname = argv[0];
  FLAGS_logtostderr = 1;
  SetUsage(argv[0]);
  ParseCommandLineFlags(&argc, &argv, true);
  InitGoogleLoggingSafe(argv[0]);
  const string addrs = FLAGS_master_addresses;

  string op = GetOp(argc, argv);

  ClusterAdminClient client(addrs, FLAGS_timeout_ms);

  EXIT_NOT_OK_PREPEND(client.Init(), "Unable to establish connection to " + addrs);

  if (op == kChangeConfigOp) {
    if (argc < 5) {
      google::ShowUsageWithFlagsRestrict(argv[0], __FILE__);
      exit(1);
    }
    string tablet_id = argv[2];
    string change_type = argv[3];
    string peer_uuid = argv[4];
    boost::optional<string> member_type;
    if (argc > 5) {
      member_type = argv[5];
    }
    Status s = client.ChangeConfig(tablet_id, change_type, peer_uuid, member_type);
    if (!s.ok()) {
      std::cerr << "Unable to change config: " << s.ToString() << std::endl;
      return 1;
    }
  } else if (op == kListTablesOp) {
    Status s = client.ListTables();
    if (!s.ok()) {
      std::cerr << "Unable to list tables: " << s.ToString() << std::endl;
      return 1;
    }
  } else if (op == kDeleteTableOp) {
    if (argc < 3) {
      google::ShowUsageWithFlagsRestrict(argv[0], __FILE__);
      exit(1);
    }
    string table_name = argv[2];
    Status s = client.DeleteTable(table_name);
    if (!s.ok()) {
      std::cerr << "Unable to delete table " << table_name << ": " << s.ToString() << std::endl;
      return 1;
    }
  } else {
    std::cerr << "Invalid operation: " << op << std::endl;
    google::ShowUsageWithFlagsRestrict(argv[0], __FILE__);
    exit(1);
  }

  return 0;
}

} // namespace tools
} // namespace kudu

int main(int argc, char** argv) {
  return kudu::tools::ClusterAdminCliMain(argc, argv);
}
