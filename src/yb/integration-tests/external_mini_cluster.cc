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

#include "yb/integration-tests/external_mini_cluster.h"

#include <gtest/gtest.h>
#include <memory>
#include <rapidjson/document.h>
#include <string>

#include "yb/client/client.h"
#include "yb/common/wire_protocol.h"
#include "yb/gutil/mathlimits.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/strings/util.h"
#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/master/master.proxy.h"
#include "yb/master/master_rpc.h"
#include "yb/server/server_base.pb.h"
#include "yb/server/server_base.proxy.h"
#include "yb/tserver/tserver_service.proxy.h"
#include "yb/rpc/messenger.h"
#include "yb/master/sys_catalog.h"
#include "yb/util/async_util.h"
#include "yb/util/curl_util.h"
#include "yb/util/env.h"
#include "yb/util/jsonreader.h"
#include "yb/util/metrics.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/net/socket.h"
#include "yb/util/path_util.h"
#include "yb/util/pb_util.h"
#include "yb/util/stopwatch.h"
#include "yb/util/subprocess.h"
#include "yb/util/test_util.h"

#include <thread>
#include <mutex>

using rapidjson::Value;
using std::string;
using std::shared_ptr;
using strings::Substitute;

using yb::master::GetLeaderMasterRpc;
using yb::master::MasterServiceProxy;
using yb::server::ServerStatusPB;
using yb::tserver::ListTabletsRequestPB;
using yb::tserver::ListTabletsResponsePB;
using yb::tserver::TabletServerServiceProxy;
using yb::consensus::ConsensusServiceProxy;
using yb::consensus::RaftPeerPB;
using yb::consensus::ChangeConfigRequestPB;
using yb::consensus::ChangeConfigResponsePB;
using yb::consensus::ChangeConfigType;
using yb::consensus::GetLastOpIdRequestPB;
using yb::consensus::GetLastOpIdResponsePB;
using yb::master::ListMastersRequestPB;
using yb::master::ListMastersResponsePB;
using yb::master::ListMasterRaftPeersRequestPB;
using yb::master::ListMasterRaftPeersResponsePB;
using yb::rpc::RpcController;

typedef ListTabletsResponsePB::StatusAndSchemaPB StatusAndSchemaPB;

namespace yb {

static const char* const kMasterBinaryName = "yb-master";
static const char* const kTabletServerBinaryName = "yb-tserver";
static double kProcessStartTimeoutSeconds = 30.0;
static double kTabletServerRegistrationTimeoutSeconds = 10.0;

#if defined(__APPLE__)
static bool kBindToUniqueLoopbackAddress = false;
#else
static bool kBindToUniqueLoopbackAddress = true;
#endif

ExternalMiniClusterOptions::ExternalMiniClusterOptions()
    : num_masters(1),
      num_tablet_servers(1),
      bind_to_unique_loopback_addresses(kBindToUniqueLoopbackAddress),
      timeout_(MonoDelta::FromMilliseconds(1000 * 10)) {
  if (bind_to_unique_loopback_addresses && sizeof(pid_t) > 2) {
    LOG(WARNING) << "pid size is " << sizeof(pid_t)
                 << ", setting bind_to_unique_loopback_addresses=false";
    bind_to_unique_loopback_addresses = false;
  }
}

ExternalMiniClusterOptions::~ExternalMiniClusterOptions() {
}


ExternalMiniCluster::ExternalMiniCluster(const ExternalMiniClusterOptions& opts)
  : opts_(opts) {
}

ExternalMiniCluster::~ExternalMiniCluster() {
  Shutdown();
}

Status ExternalMiniCluster::DeduceBinRoot(std::string* ret) {
  string exe;
  RETURN_NOT_OK(Env::Default()->GetExecutablePath(&exe));
  *ret = DirName(exe);
  return Status::OK();
}

Status ExternalMiniCluster::HandleOptions() {
  daemon_bin_path_ = opts_.daemon_bin_path;
  if (daemon_bin_path_.empty()) {
    RETURN_NOT_OK(DeduceBinRoot(&daemon_bin_path_));
  }

  data_root_ = opts_.data_root;
  if (data_root_.empty()) {
    // If they don't specify a data root, use the current gtest directory.
    data_root_ = JoinPathSegments(GetTestDataDirectory(), "minicluster-data");
  }

  return Status::OK();
}

Status ExternalMiniCluster::Start() {
  CHECK(masters_.empty()) << "Masters are not empty (size: " << masters_.size()
      << "). Maybe you meant Restart()?";
  CHECK(tablet_servers_.empty()) << "Tablet servers are not empty (size: "
      << tablet_servers_.size() << "). Maybe you meant Restart()?";
  RETURN_NOT_OK(HandleOptions());

  RETURN_NOT_OK_PREPEND(rpc::MessengerBuilder("minicluster-messenger")
                        .set_num_reactors(1)
                        .set_negotiation_threads(1)
                        .Build(&messenger_),
                        "Failed to start Messenger for minicluster");

  Status s = Env::Default()->CreateDir(data_root_);
  if (!s.ok() && !s.IsAlreadyPresent()) {
    RETURN_NOT_OK_PREPEND(s, "Could not create root dir " + data_root_);
  }

  if (opts_.num_masters != 1) {
    LOG(INFO) << "Starting " << opts_.num_masters << " masters";
    RETURN_NOT_OK_PREPEND(StartDistributedMasters(),
                          "Failed to add distributed masters");
  } else {
    LOG(INFO) << "Starting a single master";
    RETURN_NOT_OK_PREPEND(StartSingleMaster(),
                          Substitute("Failed to start a single Master"));
  }

  LOG(INFO) << "Starting " << opts_.num_tablet_servers << " tablet servers";

  for (int i = 1; i <= opts_.num_tablet_servers; i++) {
    RETURN_NOT_OK_PREPEND(AddTabletServer(),
                          Substitute("Failed starting tablet server $0", i));
  }
  RETURN_NOT_OK(WaitForTabletServerCount(
                  opts_.num_tablet_servers,
                  MonoDelta::FromSeconds(kTabletServerRegistrationTimeoutSeconds)));

  return Status::OK();
}

void ExternalMiniCluster::Shutdown(NodeSelectionMode mode) {
  if (mode == ALL) {
    for (const scoped_refptr<ExternalMaster>& master : masters_) {
      if (master) {
        master->Shutdown();
      }
    }
  }

  for (const scoped_refptr<ExternalTabletServer>& ts : tablet_servers_) {
    ts->Shutdown();
  }
}

Status ExternalMiniCluster::Restart() {
  LOG(INFO) << "Restarting cluster with " << masters_.size() << " masters.";
  for (const scoped_refptr<ExternalMaster>& master : masters_) {
    if (master && master->IsShutdown()) {
      RETURN_NOT_OK_PREPEND(master->Restart(), "Cannot restart master bound at: " +
                                               master->bound_rpc_hostport().ToString());
    }
  }

  for (const scoped_refptr<ExternalTabletServer>& ts : tablet_servers_) {
    if (ts->IsShutdown()) {
      RETURN_NOT_OK_PREPEND(ts->Restart(), "Cannot restart tablet server bound at: " +
                                           ts->bound_rpc_hostport().ToString());
    }
  }

  RETURN_NOT_OK(WaitForTabletServerCount(
      tablet_servers_.size(),
      MonoDelta::FromSeconds(kTabletServerRegistrationTimeoutSeconds)));

  return Status::OK();
}

string ExternalMiniCluster::GetBinaryPath(const string& binary) const {
  CHECK(!daemon_bin_path_.empty());
  string default_path = JoinPathSegments(daemon_bin_path_, binary);
  if (Env::Default()->FileExists(default_path)) {
    return default_path;
  }

  // In CLion-based builds we sometimes have to look for the binary in other directories.
  string alternative_dir;
  if (binary == "yb-master") {
    alternative_dir = "master";
  } else if (binary == "yb-tserver") {
    alternative_dir = "tserver";
  } else {
    LOG(WARNING) << "Default path " << default_path << " for binary " << binary <<
      " does not exist, and no alternative directory is available for this binary";
    return default_path;
  }

  string alternative_path = JoinPathSegments(daemon_bin_path_,
    "../" + alternative_dir + "/" + binary);
  if (Env::Default()->FileExists(alternative_path)) {
    LOG(INFO) << "Default path " << default_path << " for binary " << binary <<
      " does not exist, using alternative location: " << alternative_path;
    return alternative_path;
  } else {
    LOG(WARNING) << "Neither " << default_path << " nor " << alternative_path << " exist";
    return default_path;
  }
}

string ExternalMiniCluster::GetDataPath(const string& daemon_id) const {
  CHECK(!data_root_.empty());
  return JoinPathSegments(data_root_, daemon_id);
}

namespace {
vector<string> SubstituteInFlags(const vector<string>& orig_flags,
                                 int index) {
  string str_index = strings::Substitute("$0", index);
  vector<string> ret;
  for (const string& orig : orig_flags) {
    ret.push_back(StringReplace(orig, "${index}", str_index, true));
  }
  return ret;
}

} // anonymous namespace

Status ExternalMiniCluster::StartSingleMaster() {
  string exe = GetBinaryPath(kMasterBinaryName);
  uint16_t free_port = GetFreePort();
  LOG(INFO) << "Using an auto-assigned port " << free_port
      << " to start an external mini-cluster non-distributed master";
  scoped_refptr<ExternalMaster> master =
    new ExternalMaster(0, messenger_, exe, GetDataPath("master"),
                       SubstituteInFlags(opts_.extra_master_flags, 0),
                       Substitute("127.0.0.1:$0", free_port));
  RETURN_NOT_OK(master->Start());
  masters_.push_back(master);
  return Status::OK();
}

Status ExternalMiniCluster::StartNewMaster(ExternalMaster** new_master) {
  int add_at_index = num_masters();

  int new_port = GetFreePort();
  LOG(INFO) << "Using an auto-assigned port " << new_port
            << " to start a new external mini-cluster master.";

  string addr = Substitute("127.0.0.1:$0", new_port);

  string exe = GetBinaryPath(kMasterBinaryName);

  ExternalMaster* master = new ExternalMaster(
      add_at_index,
      messenger_,
      exe,
      GetDataPath(Substitute("master-$0", add_at_index)),
      opts_.extra_master_flags,
      addr,
      "");
  RETURN_NOT_OK_PREPEND(
      master->Start(true),
      Substitute("Unable to start 'shell' mode master at index $0", add_at_index));

  *new_master = master;

  return Status::OK();
}

Status ExternalMiniClusterOptions::RemovePort(const uint16_t port) {
  auto iter = std::find(master_rpc_ports.begin(), master_rpc_ports.end(), port);

  if (iter == master_rpc_ports.end()) {
    return Status::InvalidArgument(Substitute(
        "Port to be removed '$0' not found in existing list of $1 masters.",
        port, num_masters));
  }

  master_rpc_ports.erase(iter);
  --num_masters;

  return Status::OK();
}

Status ExternalMiniClusterOptions::AddPort(const uint16_t port) {
  auto iter = std::find(master_rpc_ports.begin(), master_rpc_ports.end(), port);

  if (iter != master_rpc_ports.end()) {
    return Status::InvalidArgument(Substitute(
        "Port to be added '$0' already found in the existing list of $1 masters.",
        port, num_masters));
  }

  master_rpc_ports.push_back(port);
  ++num_masters;

  return Status::OK();
}

Status ExternalMiniCluster::CheckPortAndMasterSizes() const {
  if (opts_.num_masters != masters_.size() ||
      opts_.num_masters != opts_.master_rpc_ports.size()) {
    string fatal_err_msg = Substitute(
        "Mismatch number of masters in options $0, compared to masters vector $1 or rpc ports $2",
        opts_.num_masters, masters_.size(), opts_.master_rpc_ports.size());
    LOG(FATAL) << fatal_err_msg;
  }

  return Status::OK();
}

Status ExternalMiniCluster::AddMaster(ExternalMaster* master) {
  auto iter = std::find_if(masters_.begin(), masters_.end(), MasterComparator(master));

  if (iter != masters_.end()) {
    return Status::InvalidArgument(Substitute(
        "Master to be added '$0' already found in existing list of $1 masters.",
        master->bound_rpc_hostport().ToString(), opts_.num_masters));
  }

  RETURN_NOT_OK(opts_.AddPort(master->bound_rpc_hostport().port()));
  masters_.push_back(master);

  RETURN_NOT_OK(CheckPortAndMasterSizes());

  return Status::OK();
}

Status ExternalMiniCluster::RemoveMaster(ExternalMaster* master) {
  auto iter = std::find_if(masters_.begin(), masters_.end(), MasterComparator(master));

  if (iter == masters_.end()) {
    return Status::InvalidArgument(Substitute(
        "Master to be removed '$0' not found in existing list of $1 masters.",
        master->bound_rpc_hostport().ToString(), opts_.num_masters));
  }

  RETURN_NOT_OK(opts_.RemovePort(master->bound_rpc_hostport().port()));
  masters_.erase(iter);

  RETURN_NOT_OK(CheckPortAndMasterSizes());

  return Status::OK();
}

std::shared_ptr<ConsensusServiceProxy> ExternalMiniCluster::GetLeaderConsensusProxy() {
  Sockaddr leader_master_sock = GetLeaderMaster()->bound_rpc_addr();

  return std::make_shared<ConsensusServiceProxy>(messenger_, leader_master_sock);
}

std::shared_ptr<ConsensusServiceProxy> ExternalMiniCluster::GetConsensusProxy(
    scoped_refptr<ExternalMaster> master) {
  Sockaddr master_sock = master->bound_rpc_addr();

  return std::make_shared<ConsensusServiceProxy>(messenger_, master_sock);
}

Status ExternalMiniCluster::ChangeConfig(ExternalMaster* master, ChangeConfigType type) {
  if (type != consensus::ADD_SERVER && type != consensus::REMOVE_SERVER) {
    return Status::InvalidArgument(Substitute("Invalid Change Config type $0", type));
  }

  std::shared_ptr<ConsensusServiceProxy> leader_proxy = GetLeaderConsensusProxy();
  ChangeConfigRequestPB req;
  ChangeConfigResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(opts_.timeout_);

  RaftPeerPB peer_pb;
  peer_pb.set_permanent_uuid(master->uuid());
  peer_pb.set_member_type(RaftPeerPB::VOTER);
  RETURN_NOT_OK(HostPortToPB(master->bound_rpc_hostport(), peer_pb.mutable_last_known_addr()));
  req.set_dest_uuid(GetLeaderMaster()->uuid());
  req.set_tablet_id(yb::master::kSysCatalogTabletId);
  req.set_type(type);
  *req.mutable_server() = peer_pb;

  RETURN_NOT_OK(leader_proxy->ChangeConfig(req, &resp, &rpc));
  if (resp.has_error()) {
    return Status::RuntimeError(Substitute(
        "Change Config RPC to leader hit error: $0", resp.error().ShortDebugString()));
  }

  LOG(INFO) << "Master " << master->bound_rpc_hostport().ToString() << ", change type "
            << type << " to " << masters_.size() << " masters.";

  if (type == consensus::ADD_SERVER) {
    return AddMaster(master);
  } else if (type == consensus::REMOVE_SERVER) {
    return RemoveMaster(master);
  }

  string err_msg = Substitute("Should not reach here - change type $0", type);

  LOG(FATAL) << err_msg;

  // Satisfy the compiler with a return from here
  return Status::RuntimeError(err_msg);
}

// We look for the exact master match. Since it is possible to stop/restart master on
// a given host/port, we do not want a stale master pointer input to match a newer master.
int ExternalMiniCluster::GetIndexOfMaster(ExternalMaster* master) const {
  for (int i = 0; i < masters_.size(); i++) {
    if (masters_[i].get() == master) {
      return i;
    }
  }
  return -1;
}

Status ExternalMiniCluster::GetNumMastersAsSeenBy(ExternalMaster* master, int* num_peers) {
  ListMastersRequestPB list_req;
  ListMastersResponsePB list_resp;
  int index = GetIndexOfMaster(master);

  if (index == -1) {
    return Status::InvalidArgument(Substitute(
        "Given master '$0' not in the current list of $1 masters.",
        master->bound_rpc_hostport().ToString(), masters_.size()));
  }

  std::shared_ptr<MasterServiceProxy> proxy = master_proxy(index);
  rpc::RpcController rpc;
  rpc.set_timeout(opts_.timeout_);
  RETURN_NOT_OK(proxy->ListMasters(list_req, &list_resp, &rpc));
  if (list_resp.has_error()) {
    return Status::RuntimeError(Substitute(
        "List Masters RPC response hit error: $0", list_resp.error().ShortDebugString()));
  }

  LOG(INFO) << "List Masters for master at index " << index
            << " got " << list_resp.masters_size() << " peers";

  *num_peers = list_resp.masters_size();

  return Status::OK();
}

Status ExternalMiniCluster::WaitForLeaderCommitTermAdvance() {
  OpId start_opid;
  GetLastOpIdForLeader(&start_opid);
  LOG(INFO) << "Start OPID : " << start_opid.ShortDebugString();

  // Need not do any wait if it is a restart case - so the commit term will be > 0.
  if (start_opid.term() != 0)
    return Status::OK();

  MonoTime now = MonoTime::Now(MonoTime::FINE);
  MonoTime deadline = now;
  deadline.AddDelta(opts_.timeout_);
  OpId opid = start_opid;

  for (int i = 1; now.ComesBefore(deadline); ++i) {
    if (opid.term() > start_opid.term()) {
      LOG(INFO) << "Final OPID: " << opid.ShortDebugString() << " after "
                << i << " iterations.";

      return Status::OK();
    }
    SleepFor(MonoDelta::FromMilliseconds(min(i, 10)));
    RETURN_NOT_OK(GetLastOpIdForLeader(&opid));
    now = MonoTime::Now(MonoTime::FINE);
  }

  return Status::TimedOut(Substitute("Term did not advance from $0.", start_opid.term()));
}

Status ExternalMiniCluster::GetLastOpIdForEachMasterPeer(
    const MonoDelta& timeout,
    consensus::OpIdType opid_type,
    vector<OpId>* op_ids) {
  GetLastOpIdRequestPB opid_req;
  GetLastOpIdResponsePB opid_resp;
  opid_req.set_tablet_id(yb::master::kSysCatalogTabletId);
  RpcController controller;
  controller.set_timeout(timeout);

  op_ids->clear();
  for (scoped_refptr<ExternalMaster> master : masters_) {
    opid_req.set_dest_uuid(master->uuid());
    opid_req.set_opid_type(opid_type);
    RETURN_NOT_OK_PREPEND(
        GetConsensusProxy(master)->GetLastOpId(opid_req, &opid_resp, &controller),
        Substitute("Failed to fetch last op id from $0", master->bound_rpc_hostport().port()));
    op_ids->push_back(opid_resp.opid());
    controller.Reset();
  }

  return Status::OK();
}

Status ExternalMiniCluster::WaitForMastersToCommitUpTo(int target_index) {
  MonoTime now = MonoTime::Now(MonoTime::COARSE);
  MonoTime deadline = now;
  deadline.AddDelta(opts_.timeout_);

  for (int i = 1; now.ComesBefore(deadline); i++) {
    vector<OpId> ids;
    Status s = GetLastOpIdForEachMasterPeer(opts_.timeout_, consensus::COMMITTED_OPID, &ids);

    if (s.ok()) {
      bool any_behind = false;
      for (const OpId& id : ids) {
        if (id.index() < target_index) {
          any_behind = true;
          break;
        }
      }
      if (!any_behind) {
        LOG(INFO) << "Committed up to " << target_index;
        return Status::OK();
      }
    } else {
      LOG(WARNING) << "Got error getting last opid for each replica: " << s.ToString();
    }

    SleepFor(MonoDelta::FromMilliseconds(min(i * 100, 1000)));

    now = MonoTime::Now(MonoTime::COARSE);
  }

  return Status::TimedOut(
      Substitute("Index $0 not available on all replicas after $1. ",
                 target_index,
                 opts_.timeout_.ToString()));
}

Status ExternalMiniCluster::GetLastOpIdForLeader(OpId* opid) {
  std::shared_ptr<ConsensusServiceProxy> leader_proxy = GetLeaderConsensusProxy();

  RETURN_NOT_OK(itest::GetLastOpIdForMasterReplica(
      leader_proxy,
      yb::master::kSysCatalogTabletId,
      GetLeaderMaster()->uuid(),
      consensus::COMMITTED_OPID,
      opts_.timeout_,
      opid));

  return Status::OK();
}

Status ExternalMiniCluster::StartDistributedMasters() {
  int num_masters = opts_.num_masters;

  if (opts_.master_rpc_ports.size() != num_masters) {
    LOG(FATAL) << num_masters << " masters requested, but " <<
        opts_.master_rpc_ports.size() << " ports specified in 'master_rpc_ports'";
  }

  for (int i = 0; i < opts_.master_rpc_ports.size(); ++i) {
    if (opts_.master_rpc_ports[i] == 0) {
      opts_.master_rpc_ports[i] = GetFreePort();
      LOG(INFO) << "Using an auto-assigned port " << opts_.master_rpc_ports[i]
        << " to start an external mini-cluster master";
    }
  }

  vector<string> peer_addrs;
  for (int i = 0; i < num_masters; i++) {
    string addr = Substitute("127.0.0.1:$0", opts_.master_rpc_ports[i]);
    peer_addrs.push_back(addr);
  }
  string peer_addrs_str = JoinStrings(peer_addrs, ",");
  vector<string> flags = opts_.extra_master_flags;
  flags.push_back("--enable_leader_failure_detection=true");
  string exe = GetBinaryPath(kMasterBinaryName);

  // Start the masters.
  for (int i = 0; i < num_masters; i++) {
    scoped_refptr<ExternalMaster> peer =
      new ExternalMaster(
        i,
        messenger_,
        exe,
        GetDataPath(Substitute("master-$0", i)),
        SubstituteInFlags(flags, i),
        peer_addrs[i],
        peer_addrs_str);
    RETURN_NOT_OK_PREPEND(peer->Start(),
                          Substitute("Unable to start Master at index $0", i));
    masters_.push_back(peer);
  }

  return Status::OK();
}

string ExternalMiniCluster::GetBindIpForTabletServer(int index) const {
  if (opts_.bind_to_unique_loopback_addresses) {
    pid_t p = getpid();
    CHECK_LE(p, MathLimits<uint16_t>::kMax)
        << "bind_to_unique_loopback_addresses does not work on systems with >16-bit pid";
    return Substitute("127.$0.$1.$2", p >> 8, p & 0xff, index);
  } else {
    return "127.0.0.1";
  }
}

Status ExternalMiniCluster::AddTabletServer() {
  CHECK(GetLeaderMaster() != nullptr)
      << "Must have started at least 1 master before adding tablet servers";

  int idx = tablet_servers_.size();

  string exe = GetBinaryPath(kTabletServerBinaryName);
  vector<HostPort> master_hostports;
  for (int i = 0; i < num_masters(); i++) {
    master_hostports.push_back(DCHECK_NOTNULL(master(i))->bound_rpc_hostport());
  }

  scoped_refptr<ExternalTabletServer> ts =
    new ExternalTabletServer(
      idx, messenger_, exe, GetDataPath(Substitute("ts-$0", idx)), GetBindIpForTabletServer(idx),
      master_hostports, SubstituteInFlags(opts_.extra_tserver_flags, idx));
  RETURN_NOT_OK(ts->Start());
  tablet_servers_.push_back(ts);
  return Status::OK();
}

Status ExternalMiniCluster::WaitForTabletServerCount(int count, const MonoDelta& timeout) {
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(timeout);

  while (true) {
    MonoDelta remaining = deadline.GetDeltaSince(MonoTime::Now(MonoTime::FINE));
    if (remaining.ToSeconds() < 0) {
      return Status::TimedOut(Substitute("$0 TS(s) never registered with master", count));
    }

    for (int i = 0; i < masters_.size(); i++) {
      master::ListTabletServersRequestPB req;
      master::ListTabletServersResponsePB resp;
      rpc::RpcController rpc;
      rpc.set_timeout(remaining);
      RETURN_NOT_OK_PREPEND(master_proxy(i)->ListTabletServers(req, &resp, &rpc),
                            "ListTabletServers RPC failed");
      // ListTabletServers() may return servers that are no longer online.
      // Do a second step of verification to verify that the descs that we got
      // are aligned (same uuid/seqno) with the TSs that we have in the cluster.
      int match_count = 0;
      for (const master::ListTabletServersResponsePB_Entry& e : resp.servers()) {
        for (const scoped_refptr<ExternalTabletServer>& ets : tablet_servers_) {
          if (ets->instance_id().permanent_uuid() == e.instance_id().permanent_uuid() &&
              ets->instance_id().instance_seqno() == e.instance_id().instance_seqno()) {
            match_count++;
            break;
          }
        }
      }
      if (match_count == count) {
        LOG(INFO) << count << " TS(s) registered with Master";
        return Status::OK();
      }
    }
    SleepFor(MonoDelta::FromMilliseconds(1));
  }
}

void ExternalMiniCluster::AssertNoCrashes() {
  vector<ExternalDaemon*> daemons = this->daemons();
  for (ExternalDaemon* d : daemons) {
    if (d->IsShutdown()) continue;
    EXPECT_TRUE(d->IsProcessAlive()) << "At least one process crashed";
  }
}

Status ExternalMiniCluster::WaitForTabletsRunning(ExternalTabletServer* ts,
                                                  const MonoDelta& timeout) {
  TabletServerServiceProxy proxy(messenger_, ts->bound_rpc_addr());
  ListTabletsRequestPB req;
  ListTabletsResponsePB resp;

  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(timeout);
  while (MonoTime::Now(MonoTime::FINE).ComesBefore(deadline)) {
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(10));
    RETURN_NOT_OK(proxy.ListTablets(req, &resp, &rpc));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }

    int num_not_running = 0;
    for (const StatusAndSchemaPB& status : resp.status_and_schema()) {
      if (status.tablet_status().state() != tablet::RUNNING) {
        num_not_running++;
      }
    }

    if (num_not_running == 0) {
      return Status::OK();
    }

    SleepFor(MonoDelta::FromMilliseconds(10));
  }

  return Status::TimedOut(resp.DebugString());
}

namespace {
void LeaderMasterCallback(HostPort* dst_hostport,
                          Synchronizer* sync,
                          const Status& status,
                          const HostPort& result) {
  if (status.ok()) {
    *dst_hostport = result;
  }
  sync->StatusCB(status);
}
} // anonymous namespace

Status ExternalMiniCluster::GetFirstNonLeaderMasterIndex(int* idx) {
  return GetPeerMasterIndex(idx, false);
}

Status ExternalMiniCluster::GetLeaderMasterIndex(int* idx) {
  return GetPeerMasterIndex(idx, true);
}

Status ExternalMiniCluster::GetPeerMasterIndex(int* idx, bool is_leader) {
  scoped_refptr<GetLeaderMasterRpc> rpc;
  Synchronizer sync;
  vector<Sockaddr> addrs;
  HostPort leader_master_hp;
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(MonoDelta::FromSeconds(5));

  *idx = 0; // default to 0'th index, even in case of errors.

  for (const scoped_refptr<ExternalMaster>& master : masters_) {
    addrs.push_back(master->bound_rpc_addr());
  }
  rpc.reset(new GetLeaderMasterRpc(Bind(&LeaderMasterCallback,
                                        &leader_master_hp,
                                        &sync),
                                   addrs,
                                   deadline,
                                   messenger_));
  rpc->SendRpc();
  RETURN_NOT_OK(sync.Wait());
  bool found = false;
  for (int i = 0; i < masters_.size(); i++) {
    if (is_leader && masters_[i]->bound_rpc_hostport().port() == leader_master_hp.port() ||
        !is_leader && masters_[i]->bound_rpc_hostport().port() != leader_master_hp.port()) {
      found = true;
      *idx = i;
      break;
    }
  }

  const string peer_type = is_leader ? "leader" : "non-leader";
  if (!found) {
    // There is never a situation where this should happen, so it's
    // better to exit with a FATAL log message right away vs. return a
    // Status::IllegalState().
    LOG(FATAL) << "Peer " << peer_type << " master is not in masters_ list.";
  }

  LOG(INFO) << "Found peer " << peer_type << " at index " << *idx << ".";

  return Status::OK();
}

ExternalMaster* ExternalMiniCluster::GetLeaderMaster() {
  int idx = 0;
  Status s = GetLeaderMasterIndex(&idx);
  if (!s.ok()) {
    LOG(INFO) << "GetLeaderMasterIndex hit error : " << s.ToString();
    // For now, the first one is assumed to be the leader master in case
    // of errors/timeouts.
  }

  return master(idx);
}

ExternalTabletServer* ExternalMiniCluster::tablet_server_by_uuid(const std::string& uuid) const {
  for (const scoped_refptr<ExternalTabletServer>& ts : tablet_servers_) {
    if (ts->instance_id().permanent_uuid() == uuid) {
      return ts.get();
    }
  }
  return nullptr;
}

int ExternalMiniCluster::tablet_server_index_by_uuid(const std::string& uuid) const {
  for (int i = 0; i < tablet_servers_.size(); i++) {
    if (tablet_servers_[i]->uuid() == uuid) {
      return i;
    }
  }
  return -1;
}

vector<ExternalDaemon*> ExternalMiniCluster::daemons() const {
  vector<ExternalDaemon*> results;
  for (const scoped_refptr<ExternalTabletServer>& ts : tablet_servers_) {
    results.push_back(ts.get());
  }
  for (const scoped_refptr<ExternalMaster>& master : masters_) {
    results.push_back(master.get());
  }
  return results;
}

std::shared_ptr<rpc::Messenger> ExternalMiniCluster::messenger() {
  return messenger_;
}

std::shared_ptr<MasterServiceProxy> ExternalMiniCluster::master_proxy() {
  CHECK_EQ(masters_.size(), 1);
  return master_proxy(0);
}

std::shared_ptr<MasterServiceProxy> ExternalMiniCluster::master_proxy(int idx) {
  CHECK_LT(idx, masters_.size());
  return std::shared_ptr<MasterServiceProxy>(
      new MasterServiceProxy(messenger_, CHECK_NOTNULL(master(idx))->bound_rpc_addr()));
}

Status ExternalMiniCluster::CreateClient(client::YBClientBuilder& builder,
                                         client::sp::shared_ptr<client::YBClient>* client) {
  CHECK(!masters_.empty());
  builder.clear_master_server_addrs();
  for (const scoped_refptr<ExternalMaster>& master : masters_) {
    builder.add_master_server_addr(master->bound_rpc_hostport().ToString());
  }
  return builder.Build(client);
}

Status ExternalMiniCluster::SetFlag(ExternalDaemon* daemon,
                                    const string& flag,
                                    const string& value) {
  server::GenericServiceProxy proxy(messenger_, daemon->bound_rpc_addr());

  rpc::RpcController controller;
  controller.set_timeout(MonoDelta::FromSeconds(30));
  server::SetFlagRequestPB req;
  server::SetFlagResponsePB resp;
  req.set_flag(flag);
  req.set_value(value);
  req.set_force(true);
  RETURN_NOT_OK_PREPEND(proxy.SetFlag(req, &resp, &controller),
                        "rpc failed");
  if (resp.result() != server::SetFlagResponsePB::SUCCESS) {
    return Status::RemoteError("failed to set flag",
                               resp.ShortDebugString());
  }
  return Status::OK();
}

//------------------------------------------------------------
// ExternalDaemon
//------------------------------------------------------------

ExternalDaemon::ExternalDaemon(
    std::string short_description,
    std::shared_ptr<rpc::Messenger> messenger,
    string exe,
    string data_dir,
    vector<string> extra_flags)
  : short_description_(short_description),
    messenger_(std::move(messenger)),
    exe_(std::move(exe)),
    data_dir_(std::move(data_dir)),
    extra_flags_(std::move(extra_flags)) {}

ExternalDaemon::~ExternalDaemon() {
}


Status ExternalDaemon::StartProcess(const vector<string>& user_flags) {
  CHECK(!process_);

  vector<string> argv;
  // First the exe for argv[0]
  argv.push_back(BaseName(exe_));

  // Then all the flags coming from the minicluster framework.
  argv.insert(argv.end(), user_flags.begin(), user_flags.end());

  // Enable metrics logging.
  // Even though we set -logtostderr down below, metrics logs end up being written
  // based on -log_dir. So, we have to set that too.
  argv.push_back("--metrics_log_interval_ms=1000");
  argv.push_back("--log_dir=" + data_dir_);

  // Then the "extra flags" passed into the ctor (from the ExternalMiniCluster
  // options struct). These come at the end so they can override things like
  // web port or RPC bind address if necessary.
  argv.insert(argv.end(), extra_flags_.begin(), extra_flags_.end());

  // Tell the server to dump its port information so we can pick it up.
  string info_path = JoinPathSegments(data_dir_, "info.pb");
  argv.push_back("--server_dump_info_path=" + info_path);
  argv.push_back("--server_dump_info_format=pb");

  // We use ephemeral ports in many tests. They don't work for production, but are OK
  // in unit tests.
  argv.push_back("--rpc_server_allow_ephemeral_ports");

  // A previous instance of the daemon may have run in the same directory. So, remove
  // the previous info file if it's there.
  ignore_result(Env::Default()->DeleteFile(info_path));

  // Ensure that logging goes to the test output and doesn't get buffered.
  argv.push_back("--logtostderr");
  argv.push_back("--logbuflevel=-1");

  // Use the same verbose logging level in the child process as in the test driver.
  argv.push_back(Substitute("-v=$0", FLAGS_v));

  gscoped_ptr<Subprocess> p(new Subprocess(exe_, argv));
  p->ShareParentStdout(false);
  p->ShareParentStderr(false);
  auto default_output_prefix = Substitute("[$0]", short_description_);
  LOG(INFO) << "Running " << default_output_prefix << ": " << exe_ << "\n"
    << JoinStrings(argv, "\n");
  RETURN_NOT_OK_PREPEND(p->Start(),
                        Substitute("Failed to start subprocess $0", exe_));

  StartTailerThread(Substitute("[$0 stdout]", short_description_), p->ReleaseChildStdoutFd(),
    &std::cout);
  // We will mostly see stderr output from the child process (because of --logtostderr), so we'll
  // assume that by default in the output prefix.
  StartTailerThread(default_output_prefix, p->ReleaseChildStderrFd(), &std::cerr);

  // The process is now starting -- wait for the bound port info to show up.
  Stopwatch sw;
  sw.start();
  bool success = false;
  while (sw.elapsed().wall_seconds() < kProcessStartTimeoutSeconds) {
    if (Env::Default()->FileExists(info_path)) {
      success = true;
      break;
    }
    SleepFor(MonoDelta::FromMilliseconds(10));
    int rc;
    Status s = p->WaitNoBlock(&rc);
    if (s.IsTimedOut()) {
      // The process is still running.
      continue;
    }
    RETURN_NOT_OK_PREPEND(s, Substitute("Failed waiting on $0", exe_));
    return Status::RuntimeError(
      Substitute("Process exited with rc=$0", rc),
      exe_);
  }

  if (!success) {
    ignore_result(p->Kill(SIGKILL));
    return Status::TimedOut(
        Substitute("Timed out after $0s waiting for process ($1) to write info file ($2)",
                   kProcessStartTimeoutSeconds, exe_, info_path));
  }

  status_.reset(new ServerStatusPB());
  RETURN_NOT_OK_PREPEND(pb_util::ReadPBFromPath(Env::Default(), info_path, status_.get()),
                        "Failed to read info file from " + info_path);
  LOG(INFO) << "Started " << default_output_prefix << " " << exe_ << " as pid " << p->pid();
  VLOG(1) << exe_ << " instance information:\n" << status_->DebugString();

  process_.swap(p);
  return Status::OK();
}

Status ExternalDaemon::Pause() {
  if (!process_) return Status::OK();
  VLOG(1) << "Pausing " << exe_ << " with pid " << process_->pid();
  return process_->Kill(SIGSTOP);
}

Status ExternalDaemon::Resume() {
  if (!process_) return Status::OK();
  VLOG(1) << "Resuming " << exe_ << " with pid " << process_->pid();
  return process_->Kill(SIGCONT);
}

bool ExternalDaemon::IsShutdown() const {
  return process_.get() == nullptr;
}

bool ExternalDaemon::IsProcessAlive() const {
  if (IsShutdown()) {
    return false;
  }

  int rc = 0;
  Status s = process_->WaitNoBlock(&rc);
  // If the non-blocking Wait "times out", that means the process
  // is running.
  return s.IsTimedOut();
}

pid_t ExternalDaemon::pid() const {
  return process_->pid();
}

void ExternalDaemon::Shutdown() {
  if (!process_) return;

  // Before we kill the process, store the addresses. If we're told to
  // start again we'll reuse these.
  bound_rpc_ = bound_rpc_hostport();
  bound_http_ = bound_http_hostport();

  if (IsProcessAlive()) {
    // In coverage builds, ask the process nicely to flush coverage info
    // before we kill -9 it. Otherwise, we never get any coverage from
    // external clusters.
    FlushCoverage();

    LOG(INFO) << "Killing " << exe_ << " with pid " << process_->pid();
    ignore_result(process_->Kill(SIGKILL));
  }
  int ret;
  WARN_NOT_OK(process_->Wait(&ret), "Waiting on " + exe_);
  process_.reset();
}

void ExternalDaemon::FlushCoverage() {
#ifndef COVERAGE_BUILD
  return;
#else
  LOG(INFO) << "Attempting to flush coverage for " << exe_ << " pid " << process_->pid();
  server::GenericServiceProxy proxy(messenger_, bound_rpc_addr());

  server::FlushCoverageRequestPB req;
  server::FlushCoverageResponsePB resp;
  rpc::RpcController rpc;

  // Set a reasonably short timeout, since some of our tests kill servers which
  // are kill -STOPed.
  rpc.set_timeout(MonoDelta::FromMilliseconds(100));
  Status s = proxy.FlushCoverage(req, &resp, &rpc);
  if (s.ok() && !resp.success()) {
    s = Status::RemoteError("Server does not appear to be running a coverage build");
  }
  WARN_NOT_OK(s, Substitute("Unable to flush coverage on $0 pid $1", exe_, process_->pid()));
#endif
}

HostPort ExternalDaemon::bound_rpc_hostport() const {
  CHECK(status_);
  CHECK_GE(status_->bound_rpc_addresses_size(), 1);
  HostPort ret;
  CHECK_OK(HostPortFromPB(status_->bound_rpc_addresses(0), &ret));
  return ret;
}

Sockaddr ExternalDaemon::bound_rpc_addr() const {
  HostPort hp = bound_rpc_hostport();
  vector<Sockaddr> addrs;
  CHECK_OK(hp.ResolveAddresses(&addrs));
  CHECK(!addrs.empty());
  return addrs[0];
}

HostPort ExternalDaemon::bound_http_hostport() const {
  CHECK(status_);
  CHECK_GE(status_->bound_http_addresses_size(), 1);
  HostPort ret;
  CHECK_OK(HostPortFromPB(status_->bound_http_addresses(0), &ret));
  return ret;
}

void ExternalDaemon::StartTailerThread(std::string line_prefix, int child_fd, ostream* out) {
  // Synchronize tailing output from all external daemons for simplicity.
  static std::mutex tailer_output_mutex;

  std::thread tailer_thread([line_prefix, child_fd, out] {
    FILE* fp = fdopen(child_fd, "rb");
    char buf[65536];
    while (!feof(fp) && fgets(buf, sizeof(buf), fp) != nullptr) {
      size_t l = strlen(buf);
      const char* maybe_end_of_line = l > 0 && buf[l - 1] == '\n' ? "" : "\n";
      std::lock_guard<std::mutex> lock(tailer_output_mutex);
      // Make sure we always output an end-of-line character.
      *out << line_prefix << " " << buf << maybe_end_of_line;
    }
    fclose(fp);
  });
  tailer_thread.detach();
}

const NodeInstancePB& ExternalDaemon::instance_id() const {
  CHECK(status_);
  return status_->node_instance();
}

const string& ExternalDaemon::uuid() const {
  CHECK(status_);
  return status_->node_instance().permanent_uuid();
}

Status ExternalDaemon::GetInt64Metric(const MetricEntityPrototype* entity_proto,
                                      const char* entity_id,
                                      const MetricPrototype* metric_proto,
                                      const char* value_field,
                                      int64_t* value) const {
  // Fetch metrics whose name matches the given prototype.
  string url = Substitute(
      "http://$0/jsonmetricz?metrics=$1",
      bound_http_hostport().ToString(),
      metric_proto->name());
  EasyCurl curl;
  faststring dst;
  RETURN_NOT_OK(curl.FetchURL(url, &dst));

  // Parse the results, beginning with the top-level entity array.
  JsonReader r(dst.ToString());
  RETURN_NOT_OK(r.Init());
  vector<const Value*> entities;
  RETURN_NOT_OK(r.ExtractObjectArray(r.root(), NULL, &entities));
  for (const Value* entity : entities) {
    // Find the desired entity.
    string type;
    RETURN_NOT_OK(r.ExtractString(entity, "type", &type));
    if (type != entity_proto->name()) {
      continue;
    }
    if (entity_id) {
      string id;
      RETURN_NOT_OK(r.ExtractString(entity, "id", &id));
      if (id != entity_id) {
        continue;
      }
    }

    // Find the desired metric within the entity.
    vector<const Value*> metrics;
    RETURN_NOT_OK(r.ExtractObjectArray(entity, "metrics", &metrics));
    for (const Value* metric : metrics) {
      string name;
      RETURN_NOT_OK(r.ExtractString(metric, "name", &name));
      if (name != metric_proto->name()) {
        continue;
      }
      RETURN_NOT_OK(r.ExtractInt64(metric, value_field, value));
      return Status::OK();
    }
  }
  string msg;
  if (entity_id) {
    msg = Substitute("Could not find metric $0.$1 for entity $2",
                     entity_proto->name(), metric_proto->name(),
                     entity_id);
  } else {
    msg = Substitute("Could not find metric $0.$1",
                     entity_proto->name(), metric_proto->name());
  }
  return Status::NotFound(msg);
}

//------------------------------------------------------------
// ScopedResumeExternalDaemon
//------------------------------------------------------------

ScopedResumeExternalDaemon::ScopedResumeExternalDaemon(ExternalDaemon* daemon)
    : daemon_(CHECK_NOTNULL(daemon)) {
}

ScopedResumeExternalDaemon::~ScopedResumeExternalDaemon() {
  daemon_->Resume();
}

//------------------------------------------------------------
// ExternalMaster
//------------------------------------------------------------
ExternalMaster::ExternalMaster(
    int master_index,
    const std::shared_ptr<rpc::Messenger>& messenger,
    const string& exe,
    const string& data_dir,
    const std::vector<string>& extra_flags,
    const string& rpc_bind_address,
    const string& master_addrs)
    : ExternalDaemon(Substitute("m-$0", master_index), messenger, exe, data_dir, extra_flags),
      rpc_bind_address_(std::move(rpc_bind_address)),
      master_addrs_(std::move(master_addrs)) {
}

ExternalMaster::~ExternalMaster() {
}

Status ExternalMaster::Start(bool shell_mode) {
  vector<string> flags;
  flags.push_back("--fs_wal_dir=" + data_dir_);
  flags.push_back("--fs_data_dirs=" + data_dir_);
  flags.push_back("--rpc_bind_addresses=" + rpc_bind_address_);
  flags.push_back("--webserver_interface=localhost");
  flags.push_back("--webserver_port=0");
  // On first start, we need to tell the masters what their list of expected peers is and set the
  // create_cluster flag. For 'shell' master, there is no create flag or master addresses needed.
  if (!shell_mode) {
    flags.push_back("--create_cluster=true");
    flags.push_back("--master_addresses=" + master_addrs_);
  }
  RETURN_NOT_OK(StartProcess(flags));
  return Status::OK();
}

Status ExternalMaster::Restart() {
  // We store the addresses on shutdown so make sure we did that first.
  if (bound_rpc_.port() == 0) {
    return Status::IllegalState("Master cannot be restarted. Must call Shutdown() first.");
  }
  vector<string> flags;
  flags.push_back("--fs_wal_dir=" + data_dir_);
  flags.push_back("--fs_data_dirs=" + data_dir_);
  flags.push_back("--rpc_bind_addresses=" + bound_rpc_.ToString());
  flags.push_back("--webserver_interface=localhost");
  flags.push_back(Substitute("--webserver_port=$0", bound_http_.port()));
  RETURN_NOT_OK(StartProcess(flags));
  return Status::OK();
}


//------------------------------------------------------------
// ExternalTabletServer
//------------------------------------------------------------

ExternalTabletServer::ExternalTabletServer(
    int tablet_server_index,
    const std::shared_ptr<rpc::Messenger>& messenger, const string& exe,
    const string& data_dir, string bind_host,
    const vector<HostPort>& master_addrs, const vector<string>& extra_flags)
  : ExternalDaemon(Substitute("ts-$0", tablet_server_index), messenger, exe, data_dir, extra_flags),
    master_addrs_(HostPort::ToCommaSeparatedString(master_addrs)),
    bind_host_(std::move(bind_host)) {}

ExternalTabletServer::~ExternalTabletServer() {
}

Status ExternalTabletServer::Start() {
  vector<string> flags;
  flags.push_back("--fs_wal_dir=" + data_dir_);
  flags.push_back("--fs_data_dirs=" + data_dir_);
  flags.push_back(Substitute("--rpc_bind_addresses=$0:0",
                             bind_host_));
  flags.push_back(Substitute("--local_ip_for_outbound_sockets=$0",
                             bind_host_));
  flags.push_back(Substitute("--webserver_interface=$0",
                             bind_host_));
  flags.push_back("--webserver_port=0");
  flags.push_back("--tserver_master_addrs=" + master_addrs_);
  RETURN_NOT_OK(StartProcess(flags));
  return Status::OK();
}

Status ExternalTabletServer::Restart() {
  // We store the addresses on shutdown so make sure we did that first.
  if (bound_rpc_.port() == 0) {
    return Status::IllegalState("Tablet server cannot be restarted. Must call Shutdown() first.");
  }
  vector<string> flags;
  flags.push_back("--fs_wal_dir=" + data_dir_);
  flags.push_back("--fs_data_dirs=" + data_dir_);
  flags.push_back("--rpc_bind_addresses=" + bound_rpc_.ToString());
  flags.push_back(Substitute("--local_ip_for_outbound_sockets=$0",
                             bind_host_));
  flags.push_back(Substitute("--webserver_port=$0", bound_http_.port()));
  flags.push_back(Substitute("--webserver_interface=$0",
                             bind_host_));
  flags.push_back("--tserver_master_addrs=" + master_addrs_);
  RETURN_NOT_OK(StartProcess(flags));
  return Status::OK();
}


} // namespace yb
