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

#include "yb/integration-tests/mini_cluster.h"

#include <algorithm>
#include <string>

#include "yb/client/client.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/entity_ids_types.h"
#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus.pb.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_admin.pb.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_cluster.pb.h"
#include "yb/master/mini_master.h"
#include "yb/master/scoped_leader_shared_lock.h"
#include "yb/master/ts_manager.h"

#include "yb/rocksdb/db/db_impl.h"
#include "yb/rocksdb/rate_limiter.h"

#include "yb/rpc/messenger.h"

#include "yb/server/hybrid_clock.h"
#include "yb/server/skewed_clock.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_flags.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/debug/long_operation_tracker.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/path_util.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

using namespace std::literals;
using strings::Substitute;

DEFINE_NON_RUNTIME_string(mini_cluster_base_dir, "", "Directory for master/ts data");
DEFINE_NON_RUNTIME_bool(mini_cluster_reuse_data, false, "Reuse data of mini cluster");
DEFINE_test_flag(int32, mini_cluster_registration_wait_time_sec, 45 * yb::kTimeMultiplier,
                 "Time to wait for tservers to register to master.");

DECLARE_string(fs_data_dirs);
DECLARE_int32(memstore_size_mb);
DECLARE_int32(replication_factor);
DECLARE_int64(rocksdb_compact_flush_rate_limit_bytes_per_sec);
DECLARE_string(use_private_ip);
DECLARE_int32(load_balancer_initial_delay_secs);
DECLARE_int32(transaction_table_num_tablets);

namespace yb {

using client::YBClientBuilder;
using master::MiniMaster;
using master::TabletLocationsPB;
using master::TSDescriptor;
using std::shared_ptr;
using std::string;
using std::vector;
using tserver::MiniTabletServer;
using master::GetMasterClusterConfigResponsePB;
using master::ChangeMasterClusterConfigRequestPB;
using master::ChangeMasterClusterConfigResponsePB;
using master::SysClusterConfigEntryPB;

namespace {

const int kMasterLeaderElectionWaitTimeSeconds = 20 * kTimeMultiplier;
const int kTabletReportWaitTimeSeconds = 5;

std::string GetClusterDataDirName(const MiniClusterOptions& options) {
  std::string cluster_name = "minicluster-data";
  if (options.cluster_id == "") {
    return cluster_name;
  }
  return Format("$0-$1", cluster_name, options.cluster_id);
}

std::string GetFsRoot(const MiniClusterOptions& options) {
  if (!options.data_root.empty()) {
    return options.data_root;
  }
  if (!FLAGS_mini_cluster_base_dir.empty()) {
    return FLAGS_mini_cluster_base_dir;
  }
  return JoinPathSegments(GetTestDataDirectory(), GetClusterDataDirName(options));
}

template <typename PeersGetter>
Status WaitAllReplicasRunning(MiniCluster* cluster, MonoDelta timeout, PeersGetter peers_getter) {
  return LoggedWaitFor([cluster, list_peers = std::move(peers_getter)] {
    std::unordered_set<std::string> tablet_ids;
    auto peers = list_peers();
    for (const auto& peer : peers) {
      if (peer->state() != tablet::RaftGroupStatePB::RUNNING) {
        return false;
      }
      tablet_ids.insert(peer->tablet_id());
    }
    auto replication_factor = cluster->num_tablet_servers();
    return tablet_ids.size() * replication_factor == peers.size();
  }, timeout, "Wait all replicas to be ready");
}

bool IsActive(tablet::TabletDataState tablet_data_state) {
  return tablet_data_state != tablet::TabletDataState::TABLET_DATA_SPLIT_COMPLETED &&
         tablet_data_state != tablet::TabletDataState::TABLET_DATA_TOMBSTONED &&
         tablet_data_state != tablet::TabletDataState::TABLET_DATA_DELETED;
}

bool IsActive(const tablet::TabletPeer& peer) {
  return IsActive(peer.tablet_metadata()->tablet_data_state());
}

bool IsForTable(const tablet::TabletPeer& peer, const TableId& table_id) {
  return peer.tablet_metadata()->table_id() == table_id;
}

} // namespace

MiniCluster::MiniCluster(const MiniClusterOptions& options)
    : options_(options),
      fs_root_(GetFsRoot(options)) {
  mini_masters_.resize(options_.num_masters);
}

MiniCluster::~MiniCluster() {
  Shutdown();
}

Status MiniCluster::StartAsync(
    const std::vector<tserver::TabletServerOptions>& extra_tserver_options) {
  CHECK(!fs_root_.empty()) << "No Fs root was provided";
  CHECK(!running_);

  EnsurePortsAllocated();

  if (!options_.master_env->FileExists(fs_root_)) {
    RETURN_NOT_OK(options_.master_env->CreateDir(fs_root_));
  }

  // Limit number of transaction table tablets to help avoid timeouts.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_table_num_tablets) =
      NumTabletsPerTransactionTable(options_);

  // We are testing public/private IPs using mini cluster. So set mode to 'cloud'.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_private_ip) = "cloud";

  // This dictates the RF of newly created tables.
  SetAtomicFlag(options_.num_tablet_servers >= 3 ? 3 : 1, &FLAGS_replication_factor);
  FLAGS_memstore_size_mb = 16;
  // Default master args to make sure we don't wait to trigger new LB tasks upon master leader
  // failover.
  FLAGS_load_balancer_initial_delay_secs = 0;

  // start the masters
  RETURN_NOT_OK_PREPEND(StartMasters(),
                        "Couldn't start distributed masters");

  if (!extra_tserver_options.empty() &&
      extra_tserver_options.size() != options_.num_tablet_servers) {
    return STATUS_SUBSTITUTE(InvalidArgument, "num tserver options: $0 doesn't match with num "
        "tservers: $1", extra_tserver_options.size(), options_.num_tablet_servers);
  }

  if (mini_tablet_servers_.empty()) {
    for (size_t i = 0; i < options_.num_tablet_servers; i++) {
      if (!extra_tserver_options.empty()) {
        RETURN_NOT_OK_PREPEND(
            AddTabletServer(extra_tserver_options[i]), Substitute("Error adding TS $0", i));
      } else {
        RETURN_NOT_OK_PREPEND(AddTabletServer(), Substitute("Error adding TS $0", i));
      }
    }
  } else {
    for (const shared_ptr<MiniTabletServer>& tablet_server : mini_tablet_servers_) {
      RETURN_NOT_OK(tablet_server->Start());
    }
  }

  string ts_data_dirs;
  for (const shared_ptr<MiniTabletServer>& ts : mini_tablet_servers_) {
    for (const string& dir : ts->options()->fs_opts.data_paths) {
      ts_data_dirs += string(ts_data_dirs.empty() ? "" : ",") +  dir;
    }
  }
  // All TSes have the same following g-flags, because this is all-in-one-process MiniCluster.
  // Use ExternalMiniCluster if you need independent values.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_fs_data_dirs) = ts_data_dirs;

  running_ = true;
  rpc::MessengerBuilder builder("minicluster-messenger");
  builder.set_num_reactors(1);
  messenger_ = VERIFY_RESULT(builder.Build());
  proxy_cache_ = std::make_unique<rpc::ProxyCache>(messenger_.get());
  return Status::OK();
}

Status MiniCluster::StartYbControllerServers() {
  for (auto ts : mini_tablet_servers_) {
    RETURN_NOT_OK(AddYbControllerServer(ts));
  }
  return Status::OK();
}

Status MiniCluster::AddYbControllerServer(const std::shared_ptr<tserver::MiniTabletServer> ts) {
  // Return if we already have a Yb Controller for the given ts
  for (auto ybController : yb_controller_servers_) {
    if (ybController->GetServerAddress() == ts->bound_http_addr().address().to_string()) {
      return Status::OK();
    }
  }

  size_t idx = yb_controller_servers_.size() + 1;

  // All yb controller servers need to be on the same port.
  uint16_t server_port;
  if (idx == 1) {
    server_port = port_picker_.AllocateFreePort();
  } else {
    server_port = yb_controller_servers_[0]->GetServerPort();
  }

  const auto yb_controller_log_dir = JoinPathSegments(GetYbControllerServerFsRoot(idx), "logs");
  const auto yb_controller_tmp_dir = JoinPathSegments(GetYbControllerServerFsRoot(idx), "tmp");
  RETURN_NOT_OK(Env::Default()->CreateDirs(yb_controller_log_dir));
  RETURN_NOT_OK(Env::Default()->CreateDirs(yb_controller_tmp_dir));
  const auto server_address = ts->bound_http_addr().address().to_string();
  scoped_refptr<ExternalYbController> yb_controller = new ExternalYbController(
      idx, yb_controller_log_dir, yb_controller_tmp_dir, server_address, GetToolPath("yb-admin"),
      GetToolPath("../../../bin", "yb-ctl"), GetToolPath("../../../bin", "ycqlsh"),
      GetPgToolPath("ysql_dump"), GetPgToolPath("ysql_dumpall"), GetPgToolPath("ysqlsh"),
      server_port, master_web_ports_[0], tserver_web_ports_[idx - 1], server_address,
      GetYbcToolPath("yb-controller-server"),
      /*extra_flags*/ {});

  RETURN_NOT_OK_PREPEND(
      yb_controller->Start(), "Failed to start YB Controller at index " + std::to_string(idx));
  yb_controller_servers_.push_back(yb_controller);
  return Status::OK();
}

Status MiniCluster::StartMasters() {
  CHECK_GE(master_rpc_ports_.size(), options_.num_masters);
  EnsurePortsAllocated();

  LOG(INFO) << "Creating distributed mini masters. RPC ports: "
            << JoinInts(master_rpc_ports_, ", ");

  if (mini_masters_.size() < options_.num_masters) {
    mini_masters_.resize(options_.num_masters);
  }

  bool started = false;
  auto se = ScopeExit([this, &started] {
    if (!started) {
      for (const auto& master : mini_masters_) {
        if (master) {
          master->Shutdown();
        }
      }
    }
  });

  for (size_t i = 0; i < options_.num_masters; i++) {
    mini_masters_[i] = std::make_shared<MiniMaster>(
        options_.master_env, GetMasterFsRoot(i), master_rpc_ports_[i], master_web_ports_[i], i);
    auto status = mini_masters_[i]->StartDistributedMaster(master_rpc_ports_);
    LOG_IF(INFO, !status.ok()) << "Failed to start master: " << status;
    RETURN_NOT_OK_PREPEND(status, Substitute("Couldn't start follower $0", i));
    VLOG(1) << "Started MiniMaster with UUID " << mini_masters_[i]->permanent_uuid()
            << " at index " << i;
  }

  int i = 0;
  string master_addresses;
  for (const shared_ptr<MiniMaster>& master : mini_masters_) {
    LOG(INFO) << "Waiting to initialize catalog manager on master " << i;
    RETURN_NOT_OK_PREPEND(master->WaitForCatalogManagerInit(),
                          Substitute("Could not initialize catalog manager on master $0", i));
    master_addresses += string(i++ == 0 ? "" : ",") + master->bound_rpc_addr().ToString();
  }

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_master_addrs) = master_addresses;

  // Trigger an election to avoid an unnecessary 3s wait on every minicluster startup.
  if (!mini_masters_.empty()) {
    auto consensus = VERIFY_RESULT(RandomElement(mini_masters_)->tablet_peer()->GetConsensus());
    consensus::LeaderElectionData data { .must_be_committed_opid = OpId() };
    RETURN_NOT_OK(consensus->StartElection(data));
  }

  started = true;
  return Status::OK();
}

Status MiniCluster::Start(const std::vector<tserver::TabletServerOptions>& extra_tserver_options) {
  RETURN_NOT_OK(StartAsync(extra_tserver_options));
  return WaitForAllTabletServers();
}

Status MiniCluster::RestartSync() {
  LOG(INFO) << string(80, '-');
  LOG(INFO) << __FUNCTION__;
  LOG(INFO) << string(80, '-');

  LOG(INFO) << "Restart tablet server(s)...";
  for (auto& tablet_server : mini_tablet_servers_) {
    CHECK_OK(tablet_server->Restart());
    CHECK_OK(tablet_server->WaitStarted());
  }
  LOG(INFO) << "Restart master server(s)...";
  for (auto& master_server : mini_masters_) {
    LOG(INFO) << "Restarting master " << master_server->permanent_uuid();
    LongOperationTracker long_operation_tracker("Master restart", 5s);
    CHECK_OK(master_server->Restart());
    LOG(INFO) << "Waiting for catalog manager at " << master_server->permanent_uuid();
    CHECK_OK(master_server->WaitForCatalogManagerInit());
  }

  if (UseYbController()) {
    LOG(INFO) << "Restart YB Controller server(s)...";
    for (const auto& yb_controller : yb_controller_servers_) {
      CHECK_OK(yb_controller->Restart());
    }
  }

  LOG(INFO) << string(80, '-');
  LOG(INFO) << __FUNCTION__ << " done";
  LOG(INFO) << string(80, '-');

  RETURN_NOT_OK_PREPEND(WaitForAllTabletServers(),
                        "Waiting for tablet servers to start");
  running_ = true;
  return Status::OK();
}

Status MiniCluster::AddTabletServer(const tserver::TabletServerOptions& extra_opts) {
  if (mini_masters_.empty()) {
    return STATUS(IllegalState, "Master not yet initialized");
  }
  auto new_idx = mini_tablet_servers_.size();

  EnsurePortsAllocated(0 /* num_masters (will pick default) */, new_idx + 1);
  const uint16_t ts_rpc_port = tserver_rpc_ports_[new_idx];

  std::shared_ptr<MiniTabletServer> tablet_server;
  if (options_.num_drives == 1) {
    tablet_server = std::make_shared<MiniTabletServer>(
          GetTabletServerFsRoot(new_idx), ts_rpc_port, extra_opts, new_idx);
  } else {
    std::vector<std::string> dirs;
    for (int i = 0; i < options_.num_drives; ++i) {
      dirs.push_back(GetTabletServerDrive(new_idx, i));
    }
    tablet_server = std::make_shared<MiniTabletServer>(
          dirs, dirs, ts_rpc_port, extra_opts, new_idx);
  }

  // set the master addresses
  auto master_addr = std::make_shared<server::MasterAddresses>();
  for (const shared_ptr<MiniMaster>& master : mini_masters_) {
    master_addr->push_back({HostPort(master->bound_rpc_addr())});
    for (const auto& hp : master->master()->opts().broadcast_addresses) {
      master_addr->back().push_back(hp);
    }
  }

  tablet_server->options()->master_addresses_flag = server::MasterAddressesToString(*master_addr);
  tablet_server->options()->SetMasterAddresses(master_addr);
  tablet_server->options()->webserver_opts.port = tserver_web_ports_[new_idx];
  if (options_.ts_env) {
    tablet_server->options()->env = options_.ts_env;
  }

  RETURN_NOT_OK(tablet_server->Start(tserver::WaitTabletsBootstrapped::kFalse));

  mini_tablet_servers_.push_back(tablet_server);
  return Status::OK();
}

Status MiniCluster::AddTabletServer() {
  auto options = tserver::TabletServerOptions::CreateTabletServerOptions();
  RETURN_NOT_OK(options);
  return AddTabletServer(*options);
}

Status MiniCluster::AddTServerToBlacklist(const MiniTabletServer& ts) {
  RETURN_NOT_OK(ChangeClusterConfig([&ts](SysClusterConfigEntryPB* config) {
        // Add tserver to blacklist.
        HostPortPB* blacklist_host_pb = config->mutable_server_blacklist()->mutable_hosts()->Add();
        blacklist_host_pb->set_host(ts.bound_rpc_addr().address().to_string());
        blacklist_host_pb->set_port(ts.bound_rpc_addr().port());
      }));

  LOG(INFO) << "TServer " << ts.server()->permanent_uuid() << " at "
            << ts.bound_rpc_addr().address().to_string() << ":" << ts.bound_rpc_addr().port()
            << " was added to the blacklist";

  return Status::OK();
}

Status MiniCluster::AddTServerToLeaderBlacklist(const MiniTabletServer& ts) {
  RETURN_NOT_OK(
      ChangeClusterConfig([&ts](SysClusterConfigEntryPB* config) {
        // Add tserver to blacklist.
        HostPortPB* blacklist_host_pb = config->mutable_leader_blacklist()->mutable_hosts()->Add();
        blacklist_host_pb->set_host(ts.bound_rpc_addr().address().to_string());
        blacklist_host_pb->set_port(ts.bound_rpc_addr().port());
      }));

  LOG(INFO) << "TServer " << ts.server()->permanent_uuid() << " at "
            << ts.bound_rpc_addr().address().to_string() << ":" << ts.bound_rpc_addr().port()
            << " was added to the leader blacklist";

  return Status::OK();
}

Status MiniCluster::ClearBlacklist() {
  RETURN_NOT_OK(
      ChangeClusterConfig([](SysClusterConfigEntryPB* config) {
        config->mutable_server_blacklist()->Clear();
        config->mutable_leader_blacklist()->Clear();
      }));

  LOG(INFO) << "Blacklist has been cleared";

  return Status::OK();
}

string MiniCluster::GetMasterAddresses() const {
  string peer_addrs = "";
  for (const auto& master : mini_masters_) {
    if (!peer_addrs.empty()) {
      peer_addrs += ",";
    }
    peer_addrs += master->bound_rpc_addr_str();
  }
  return peer_addrs;
}

string MiniCluster::GetTserverHTTPAddresses() const {
  string peer_addrs = "";
  for (const auto& tserver : mini_tablet_servers_) {
    if (!peer_addrs.empty()) {
      peer_addrs += ",";
    }
    peer_addrs += tserver->bound_http_addr_str();
  }
  return peer_addrs;
}

ssize_t MiniCluster::LeaderMasterIdx() {
  Stopwatch sw;
  sw.start();
  while (sw.elapsed().wall_seconds() < kMasterLeaderElectionWaitTimeSeconds) {
    for (size_t i = 0; i < mini_masters_.size(); i++) {
      MiniMaster* master = mini_master(i);
      if (master->master() == nullptr || master->master()->IsShutdown()) {
        continue;
      }
      SCOPED_LEADER_SHARED_LOCK(l, master->master()->catalog_manager_impl());
      if (l.IsInitializedAndIsLeader()) {
        return i;
      }
    }
    SleepFor(MonoDelta::FromMilliseconds(1));
  }
  LOG(ERROR) << "No leader master elected after " << kMasterLeaderElectionWaitTimeSeconds
             << " seconds.";
  return -1;
}

Result<MiniMaster*> MiniCluster::GetLeaderMiniMaster() {
  const auto idx = LeaderMasterIdx();
  if (idx == -1) {
    return STATUS(TimedOut, "No leader master has been elected");
  }
  return mini_master(idx);
}

Result<TabletServerId> MiniCluster::StepDownMasterLeader(const std::string& new_leader_uuid) {
  auto* leader_mini_master = VERIFY_RESULT(GetLeaderMiniMaster());

  std::string actual_new_leader_uuid = new_leader_uuid;
  // Pick an arbitrary other leader to step down to, if one was not passed in.
  if (new_leader_uuid.empty()) {
    for (size_t i = 0; i < num_masters(); ++i) {
      actual_new_leader_uuid = mini_master(i)->permanent_uuid();
      if (actual_new_leader_uuid != leader_mini_master->permanent_uuid()) {
        break;
      }
    }
  }

  RETURN_NOT_OK(StepDown(leader_mini_master->tablet_peer(), new_leader_uuid, ForceStepDown::kTrue));
  return actual_new_leader_uuid;
}


void MiniCluster::StopSync() {
  if (!running_) {
    LOG(INFO) << "MiniCluster is not running.";
    return;
  }

  for (const shared_ptr<MiniTabletServer>& tablet_server : mini_tablet_servers_) {
    CHECK(tablet_server);
    tablet_server->Shutdown();
  }

  for (shared_ptr<MiniMaster>& master_server : mini_masters_) {
    CHECK(master_server);
    master_server->Shutdown();
  }

  messenger_->Shutdown();

  running_ = false;
}

void MiniCluster::Shutdown() {
  if (!running_) {
    // It's possible for the cluster to not be running on shutdown when there's a bad status during
    // startup.  We don't know how much initialization has been done, so continue with cleanup
    // assuming the worst (that a lot of initialization was done).
    LOG(INFO) << "Shutdown when mini cluster is not running (did mini cluster startup fail?)";
  }

  for (const shared_ptr<MiniTabletServer>& tablet_server : mini_tablet_servers_) {
    if (tablet_server) {
      tablet_server->Shutdown();
    } else {
      // Unlike mini masters (below), it is not expected for this vector to have an uninitialized
      // mini tserver.
      LOG(ERROR) << "Found uninitialized mini tserver";
    }
  }
  mini_tablet_servers_.clear();

  for (shared_ptr<MiniMaster>& master_server : mini_masters_) {
    if (master_server) {
      master_server->Shutdown();
      master_server.reset();
    } else {
      // It's possible for a mini master in the vector to be uninitialized because the constructor
      // initializes the vector with num_masters size and shutdown could happen before each mini
      // master is initialized in case of a bad status during startup.
      LOG(INFO) << "Found uninitialized mini master";
    }
  }
  mini_masters_.clear();

  for (const auto& yb_controller : yb_controller_servers_) {
    yb_controller->Shutdown();
  }
  yb_controller_servers_.clear();

  messenger_->Shutdown();

  running_ = false;
}

Status MiniCluster::FlushTablets(tablet::FlushMode mode, tablet::FlushFlags flags) {
  for (const auto& tablet_server : mini_tablet_servers_) {
    RETURN_NOT_OK(tablet_server->FlushTablets(mode, flags));
  }
  return Status::OK();
}

Status MiniCluster::CompactTablets(docdb::SkipFlush skip_flush) {
  for (const auto& tablet_server : mini_tablet_servers_) {
    RETURN_NOT_OK(tablet_server->CompactTablets(skip_flush));
  }
  return Status::OK();
}

Status MiniCluster::SwitchMemtables() {
  for (const auto& tablet_server : mini_tablet_servers_) {
    RETURN_NOT_OK(tablet_server->SwitchMemtables());
  }
  return Status::OK();
}

Status MiniCluster::CleanTabletLogs() {
  for (const auto& tablet_server : mini_tablet_servers_) {
    RETURN_NOT_OK(tablet_server->CleanTabletLogs());
  }
  return Status::OK();
}

MiniMaster* MiniCluster::mini_master(size_t idx) {
  CHECK_GE(idx, 0) << "Master idx must be >= 0";
  CHECK_LT(idx, mini_masters_.size()) << "Master idx must be < num masters started";
  return mini_masters_[idx].get();
}

MiniTabletServer* MiniCluster::mini_tablet_server(size_t idx) {
  CHECK_GE(idx, 0) << "TabletServer idx must be >= 0";
  CHECK_LT(idx, mini_tablet_servers_.size()) << "TabletServer idx must be < 'num_ts_started_'";
  return mini_tablet_servers_[idx].get();
}

MiniTabletServer* MiniCluster::find_tablet_server(const std::string& uuid) {
  for (const auto& server : mini_tablet_servers_) {
    if (!server->server()) {
      continue;
    }
    if (server->server()->instance_pb().permanent_uuid() == uuid) {
      return server.get();
    }
  }
  return nullptr;
}

string MiniCluster::GetMasterFsRoot(size_t idx) {
  return JoinPathSegments(fs_root_, Substitute("master-$0-root", idx + 1));
}

string MiniCluster::GetTabletServerFsRoot(size_t idx) {
  return JoinPathSegments(fs_root_, Substitute("ts-$0-root", idx + 1));
}

string MiniCluster::GetYbControllerServerFsRoot(size_t idx) {
  return JoinPathSegments(fs_root_, Substitute("ybc-$0-root", idx + 1));
}

string MiniCluster::GetTabletServerDrive(size_t idx, int drive_index) {
  if (options_.num_drives == 1) {
    return GetTabletServerFsRoot(idx);
  }
  return JoinPathSegments(fs_root_, Substitute("ts-$0-drive-$1", idx + 1, drive_index + 1));
}

tserver::TSTabletManager* MiniCluster::GetTabletManager(size_t idx) {
  return mini_tablet_server(idx)->server()->tablet_manager();
}

std::vector<std::shared_ptr<tablet::TabletPeer>> MiniCluster::GetTabletPeers(size_t idx) {
  return GetTabletManager(idx)->GetTabletPeers();
}

Status MiniCluster::WaitForReplicaCount(const TableId& tablet_id,
                                        int expected_count,
                                        TabletLocationsPB* locations) {
  Stopwatch sw;
  sw.start();
  while (sw.elapsed().wall_seconds() < kTabletReportWaitTimeSeconds) {
    auto leader_mini_master = GetLeaderMiniMaster();
    if (!leader_mini_master.ok()) {
      continue;
    }
    locations->Clear();
    Status s = (*leader_mini_master)
                   ->master()
                   ->catalog_manager()
                   ->GetTabletLocations(tablet_id, locations);
    if (s.ok() && ((locations->stale() && expected_count == 0) ||
        (!locations->stale() && locations->replicas_size() == expected_count))) {
      return Status::OK();
    }

    SleepFor(MonoDelta::FromMilliseconds(1));
  }
  return STATUS(TimedOut, Substitute("Tablet $0 never reached expected replica count $1",
                                     tablet_id, expected_count));
}

Status MiniCluster::WaitForAllTabletServers() {
  for (const shared_ptr<MiniTabletServer>& tablet_server : mini_tablet_servers_) {
    RETURN_NOT_OK_PREPEND(
        tablet_server->WaitStarted(), Format("TabletServer $0 failed to start.", tablet_server));
  }
  // Wait till all tablet servers are registered with master.
  return ResultToStatus(WaitForTabletServerCount(num_tablet_servers()));
}

Result<std::vector<std::shared_ptr<master::TSDescriptor>>> MiniCluster::WaitForTabletServerCount(
    size_t count, bool live_only) {
  Stopwatch sw;
  sw.start();
  while (sw.elapsed().wall_seconds() < FLAGS_TEST_mini_cluster_registration_wait_time_sec) {
    auto leader = GetLeaderMiniMaster();
    if (leader.ok()) {
      auto descs = (*leader)->ts_manager().GetAllDescriptors();
      if (live_only || descs.size() == count) {
        // GetAllDescriptors() may return servers that are no longer online.
        // Do a second step of verification to verify that the descs that we got
        // are aligned (same uuid/seqno) with the TSs that we have in the cluster.
        size_t match_count = 0;
        for (const shared_ptr<TSDescriptor>& desc : descs) {
          for (auto mini_tablet_server : mini_tablet_servers_) {
            auto ts = mini_tablet_server->server();
            if (ts->instance_pb().permanent_uuid() == desc->permanent_uuid() &&
                ts->instance_pb().instance_seqno() == desc->latest_seqno()) {
              if (!live_only || desc->IsLive()) {
                match_count++;
              }
              break;
            }
          }
        }

        if (match_count == count) {
          LOG(INFO) << count << " TS(s) registered with Master after "
                    << sw.elapsed().wall_seconds() << "s";
          return descs;
        }
      }

      YB_LOG_EVERY_N_SECS(INFO, 5) << "Registered: " << AsString(descs);
    }

    SleepFor(MonoDelta::FromMilliseconds(1));
  }
  return STATUS(TimedOut, Substitute("$0 TS(s) never registered with master", count));
}

void MiniCluster::ConfigureClientBuilder(YBClientBuilder* builder) {
  CHECK_NOTNULL(builder);
  builder->clear_master_server_addrs();
  for (const shared_ptr<MiniMaster>& master : mini_masters_) {
    CHECK(master);
    builder->add_master_server_addr(master->bound_rpc_addr_str());
  }
}

Result<HostPort> MiniCluster::DoGetLeaderMasterBoundRpcAddr() {
  return VERIFY_RESULT(GetLeaderMiniMaster())->bound_rpc_addr();
}

void MiniCluster::AllocatePortsForDaemonType(
    const string daemon_type,
    const size_t num_daemons,
    const string port_type,
    std::vector<uint16_t>* ports) {
  const size_t old_size = ports->size();
  if (ports->size() < num_daemons) {
    ports->resize(num_daemons, 0 /* default value */);
  }
  for (auto i = old_size; i < num_daemons; ++i) {
    if ((*ports)[i] == 0) {
      const uint16_t new_port = port_picker_.AllocateFreePort();
      (*ports)[i] = new_port;
      LOG(INFO) << "Using auto-assigned port " << new_port << " for a " << daemon_type
                << " " << port_type << " port";
    }
  }
}

void MiniCluster::EnsurePortsAllocated(size_t new_num_masters, size_t new_num_tservers) {
  if (new_num_masters == 0) {
    new_num_masters = std::max(options_.num_masters, num_masters());
  }
  AllocatePortsForDaemonType("master", new_num_masters, "RPC", &master_rpc_ports_);
  AllocatePortsForDaemonType("master", new_num_masters, "web", &master_web_ports_);

  if (new_num_tservers == 0) {
    new_num_tservers = std::max(options_.num_tablet_servers, num_tablet_servers());
  }
  AllocatePortsForDaemonType("tablet server", new_num_tservers, "RPC", &tserver_rpc_ports_);
  AllocatePortsForDaemonType("tablet server", new_num_tservers, "web", &tserver_web_ports_);
}

Status MiniCluster::ChangeClusterConfig(
    std::function<void(SysClusterConfigEntryPB*)> config_changer) {
  auto proxy = master::MasterClusterProxy(
      proxy_cache_.get(), VERIFY_RESULT(DoGetLeaderMasterBoundRpcAddr()));
  rpc::RpcController rpc;
  master::GetMasterClusterConfigRequestPB get_req;
  master::GetMasterClusterConfigResponsePB get_resp;
  RETURN_NOT_OK(proxy.GetMasterClusterConfig(get_req, &get_resp, &rpc));
  ChangeMasterClusterConfigRequestPB change_req;
  change_req.mutable_cluster_config()->Swap(get_resp.mutable_cluster_config());
  config_changer(change_req.mutable_cluster_config());
  rpc.Reset();
  ChangeMasterClusterConfigResponsePB change_resp;
  return proxy.ChangeMasterClusterConfig(change_req, &change_resp, &rpc);
}


Status MiniCluster::WaitForLoadBalancerToStabilize(MonoDelta timeout) {
  auto master_leader = VERIFY_RESULT(GetLeaderMiniMaster())->master();
  const auto start_time = MonoTime::Now();
  const auto deadline = start_time + timeout;
  RETURN_NOT_OK(Wait(
      [&]() -> Result<bool> {
        return master_leader->catalog_manager_impl()->LastLoadBalancerRunTime() > start_time;
      },
      deadline, "LoadBalancer did not evaluate the cluster load"));

  master::IsLoadBalancerIdleRequestPB req;
  master::IsLoadBalancerIdleResponsePB resp;
  return Wait(
      [&]() -> Result<bool> {
        return master_leader->catalog_manager_impl()->IsLoadBalancerIdle(&req, &resp).ok();
      },
      deadline, "IsLoadBalancerIdle");
}

server::SkewedClockDeltaChanger JumpClock(
    server::RpcServerBase* server, std::chrono::milliseconds delta) {
  auto* hybrid_clock = down_cast<server::HybridClock*>(server->clock());
  DCHECK(hybrid_clock);
  auto skewed_clock =
      std::dynamic_pointer_cast<server::SkewedClock>(hybrid_clock->physical_clock());
  DCHECK(skewed_clock)
      << ": Server physical clock is not a SkewedClock; did you forget to set --time_source?";
  return server::SkewedClockDeltaChanger(delta, skewed_clock);
}

server::SkewedClockDeltaChanger JumpClock(
    tserver::MiniTabletServer* server, std::chrono::milliseconds delta) {
  return JumpClock(server->server(), delta);
}

std::vector<server::SkewedClockDeltaChanger> SkewClocks(
    MiniCluster* cluster, std::chrono::milliseconds clock_skew) {
  std::vector<server::SkewedClockDeltaChanger> delta_changers;
  for (size_t i = 0; i != cluster->num_tablet_servers(); ++i) {
    delta_changers.push_back(JumpClock(cluster->mini_tablet_server(i)->server(), i * clock_skew));
  }
  return delta_changers;
}

std::vector<server::SkewedClockDeltaChanger> JumpClocks(
    MiniCluster* cluster, std::chrono::milliseconds delta) {
  std::vector<server::SkewedClockDeltaChanger> delta_changers;
  auto num_masters = cluster->num_masters();
  auto num_tservers = cluster->num_tablet_servers();
  delta_changers.reserve(num_masters + num_tservers);
  for (size_t i = 0; i != num_masters; ++i) {
    delta_changers.push_back(JumpClock(cluster->mini_master(i)->master(), delta));
  }
  for (size_t i = 0; i != num_tservers; ++i) {
    delta_changers.push_back(JumpClock(cluster->mini_tablet_server(i)->server(), delta));
  }
  return delta_changers;
}

void StepDownAllTablets(MiniCluster* cluster) {
  for (size_t i = 0; i != cluster->num_tablet_servers(); ++i) {
    for (const auto& peer : cluster->GetTabletPeers(i)) {
      consensus::LeaderStepDownRequestPB req;
      req.set_tablet_id(peer->tablet_id());
      consensus::LeaderStepDownResponsePB resp;
      ASSERT_OK(ASSERT_RESULT(peer->GetConsensus())->StepDown(&req, &resp));
    }
  }
}

void StepDownRandomTablet(MiniCluster* cluster) {
  auto peers = ListTabletPeers(cluster, ListPeersFilter::kLeaders);
  if (!peers.empty()) {
    auto peer = RandomElement(peers);

    consensus::LeaderStepDownRequestPB req;
    req.set_tablet_id(peer->tablet_id());
    consensus::LeaderStepDownResponsePB resp;
    ASSERT_OK(ASSERT_RESULT(peer->GetConsensus())->StepDown(&req, &resp));
  }
}

std::unordered_set<string> ListTabletIdsForTable(MiniCluster* cluster, const string& table_id) {
  std::unordered_set<string> tablet_ids;
  for (auto peer : ListTabletPeers(cluster, ListPeersFilter::kAll)) {
    if (IsForTable(*peer, table_id)) {
      tablet_ids.insert(peer->tablet_id());
    }
  }
  return tablet_ids;
}

std::unordered_set<string> ListActiveTabletIdsForTable(
    MiniCluster* cluster, const string& table_id) {
  std::unordered_set<string> tablet_ids;
  for (auto peer : ListTableActiveTabletPeers(cluster, table_id)) {
    tablet_ids.insert(peer->tablet_id());
  }
  return tablet_ids;
}

std::vector<tablet::TabletPeerPtr> ListTabletPeers(
    MiniCluster* cluster, ListPeersFilter filter,
    IncludeTransactionStatusTablets include_transaction_status_tablets) {
  auto filter_transaction_status_tablets = [include_transaction_status_tablets](const auto& peer) {
    if (include_transaction_status_tablets) {
      return true;
    }
    auto tablet = peer->shared_tablet();
    return tablet && tablet->table_type() != TableType::TRANSACTION_STATUS_TABLE_TYPE;
  };

  switch (filter) {
    case ListPeersFilter::kAll:
      return ListTabletPeers(cluster, [filter_transaction_status_tablets](const auto& peer) {
        return filter_transaction_status_tablets(peer);
      });
    case ListPeersFilter::kLeaders:
      return ListTabletPeers(cluster, [filter_transaction_status_tablets](const auto& peer) {
        if (!filter_transaction_status_tablets(peer)) {
          return false;
        }
        auto consensus_result = peer->GetConsensus();
        return consensus_result &&
               consensus_result.get()->GetLeaderStatus() != consensus::LeaderStatus::NOT_LEADER;
      });
    case ListPeersFilter::kNonLeaders:
      return ListTabletPeers(cluster, [filter_transaction_status_tablets](const auto& peer) {
        if (!filter_transaction_status_tablets(peer)) {
          return false;
        }
        auto consensus_result = peer->GetConsensus();
        return consensus_result &&
               consensus_result.get()->GetLeaderStatus() == consensus::LeaderStatus::NOT_LEADER;
      });
  }

  FATAL_INVALID_ENUM_VALUE(ListPeersFilter, filter);
}

std::vector<tablet::TabletPeerPtr> ListTabletPeers(
    MiniCluster* cluster, TabletPeerFilter filter) {
  std::vector<tablet::TabletPeerPtr> result;

  for (size_t i = 0; i != cluster->num_tablet_servers(); ++i) {
    auto server = cluster->mini_tablet_server(i)->server();
    if (!server) { // Server is shut down.
      continue;
    }
    auto peers = server->tablet_manager()->GetTabletPeers();
    for (const auto& peer : peers) {
      WARN_NOT_OK(
          // Checking for consensus is not enough here, we also need to not wait for peers
          // which are being shut down -- these peers are being filtered out by next statement.
          WaitFor(
              [peer] { return peer->GetConsensus() || peer->IsShutdownStarted(); },
              5s,
              Format("Waiting peer T $0 P $1 ready", peer->tablet_id(), peer->permanent_uuid())),
          "List tablet peers failure");
      if (peer->GetConsensus() && filter(peer)) {
        result.push_back(peer);
      }
    }
  }

  return result;
}

Result<std::vector<tablet::TabletPeerPtr>> ListTabletPeers(
    MiniCluster* cluster, const TabletId& tablet_id, TabletPeerFilter filter) {
  auto peers = ListTabletPeers(
      cluster, [&tablet_id, filter = std::move(filter)](const auto& peer) {
        return (peer->tablet_id() == tablet_id) && (!filter || filter(peer));
      });

  // Sanity check to make sure table is the same accross all peers.
  TableId table_id;
  std::string table_name;
  for (const auto& peer : peers) {
    const auto& metadata = *peer->tablet_metadata();
    if (table_id.empty()) {
      table_id = metadata.table_id();
      table_name = metadata.table_name();
    } else if (table_id != metadata.table_id()) {
      return STATUS_FORMAT(
          IllegalState,
          "Tablet $0 peer $1 table $2 ($3) does not match expected table $4 ($5).",
          tablet_id, peer->permanent_uuid(), metadata.table_id(),
          metadata.table_name(), table_name, table_id);
    }
  }

  return peers;
}

Result<std::vector<tablet::TabletPeerPtr>> ListTabletActivePeers(
    MiniCluster* cluster, const TabletId& tablet_id) {
  return ListTabletPeers(cluster, tablet_id, [](const auto& peer) {
    return IsActive(*peer);
  });
}

std::vector<tablet::TabletPeerPtr> ListTableActiveTabletLeadersPeers(
    MiniCluster* cluster, const TableId& table_id) {
  return ListTabletPeers(cluster, [&table_id](const auto& peer) {
    return peer->tablet_metadata() && IsForTable(*peer, table_id) &&
           peer->tablet_metadata()->tablet_data_state() !=
               tablet::TabletDataState::TABLET_DATA_SPLIT_COMPLETED &&
           CHECK_RESULT(peer->GetConsensus())->GetLeaderStatus() !=
               consensus::LeaderStatus::NOT_LEADER;
  });
}

std::vector<tablet::TabletPeerPtr> ListTableTabletPeers(
      MiniCluster* cluster, const TableId& table_id) {
  return ListTabletPeers(cluster, [&table_id](const tablet::TabletPeerPtr& peer) {
    return IsForTable(*peer, table_id);
  });
}

std::vector<tablet::TabletPeerPtr> ListTableActiveTabletPeers(
    MiniCluster* cluster, const TableId& table_id) {
  return ListTabletPeers(cluster, [&table_id](const tablet::TabletPeerPtr& peer) {
    return IsForTable(*peer, table_id) && IsActive(*peer);
  });
}

std::vector<tablet::TabletPeerPtr> ListActiveTabletLeadersPeers(MiniCluster* cluster) {
  return ListTabletPeers(cluster, [](const auto& peer) {
    const auto tablet_meta = peer->tablet_metadata();
    const auto consensus = CHECK_RESULT(peer->GetConsensus());
    return tablet_meta && tablet_meta->table_type() != TableType::TRANSACTION_STATUS_TABLE_TYPE &&
           IsActive(tablet_meta->tablet_data_state()) &&
           consensus->GetLeaderStatus() != consensus::LeaderStatus::NOT_LEADER;
  });
}

std::vector<tablet::TabletPeerPtr> ListTableInactiveSplitTabletPeers(
    MiniCluster* cluster, const TableId& table_id) {
  std::vector<tablet::TabletPeerPtr> result;
  for (auto peer : ListTableTabletPeers(cluster, table_id)) {
    auto tablet = peer->shared_tablet();
    if (tablet &&
        tablet->metadata()->tablet_data_state() ==
            tablet::TabletDataState::TABLET_DATA_SPLIT_COMPLETED) {
      result.push_back(peer);
    }
  }
  return result;
}

tserver::MiniTabletServer* GetLeaderForTablet(
    MiniCluster* cluster, const std::string& tablet_id, size_t* leader_idx) {
  for (size_t i = 0; i < cluster->num_tablet_servers(); i++) {
    if (!cluster->mini_tablet_server(i)->is_started()) {
      continue;
    }

    if (cluster->mini_tablet_server(i)->server()->LeaderAndReady(tablet_id)) {
      if (leader_idx) {
        *leader_idx = i;
      }
      return cluster->mini_tablet_server(i);
    }
  }
  return nullptr;
}

Result<tablet::TabletPeerPtr> GetLeaderPeerForTablet(
    MiniCluster* cluster, const std::string& tablet_id) {
  auto leaders = ListTabletPeers(cluster, [&tablet_id](const auto& peer) {
    auto consensus_result = peer->GetConsensus();
    return tablet_id == peer->tablet_id() && consensus_result &&
           consensus_result.get()->GetLeaderStatus() == consensus::LeaderStatus::LEADER_AND_READY;
  });

  SCHECK_EQ(
      leaders.size(), 1, IllegalState,
      Format("Expected exactly one leader for tablet $0", tablet_id));

  return std::move(leaders[0]);
}

Result<std::vector<tablet::TabletPeerPtr>> WaitForTableActiveTabletLeadersPeers(
    MiniCluster* cluster, const TableId& table_id,
    const size_t num_active_leaders, const MonoDelta timeout) {
  SCHECK_NOTNULL(cluster);

  std::vector<tablet::TabletPeerPtr> active_leaders_peers;
  RETURN_NOT_OK(LoggedWaitFor([&] {
    active_leaders_peers = ListTableActiveTabletLeadersPeers(cluster, table_id);
    LOG(INFO) << "active_leader_peers.size(): " << active_leaders_peers.size();
    return active_leaders_peers.size() == num_active_leaders;
  }, timeout, "Waiting for leaders ..."));
  return active_leaders_peers;
}

Status WaitUntilTabletHasLeader(
    MiniCluster* cluster, const TabletId& tablet_id, CoarseTimePoint deadline) {
  return Wait(
      [cluster, &tablet_id] {
        auto tablet_peers = ListTabletPeers(cluster, [&tablet_id](auto peer) {
          auto consensus_result = peer->GetConsensus();
          return peer->tablet_id() == tablet_id && consensus_result &&
                 consensus_result.get()->GetLeaderStatus() != consensus::LeaderStatus::NOT_LEADER;
        });
        return tablet_peers.size() == 1;
      },
      deadline, "Waiting for election in tablet " + tablet_id);
}

Status WaitForTableLeaders(
    MiniCluster* cluster, const TableId& table_id, CoarseTimePoint deadline) {
  for (const auto& tablet_id : ListTabletIdsForTable(cluster, table_id)) {
    RETURN_NOT_OK(WaitUntilTabletHasLeader(cluster, tablet_id, deadline));
  }
  return Status::OK();
}

Status WaitForTableLeaders(
    MiniCluster* cluster, const TableId& table_id, CoarseDuration timeout) {
  return WaitForTableLeaders(cluster, table_id, CoarseMonoClock::Now() + timeout);
}

Status WaitUntilMasterHasLeader(MiniCluster* cluster, MonoDelta timeout) {
  return WaitFor([cluster] {
    for (size_t i = 0; i != cluster->num_masters(); ++i) {
      auto tablet_peer = cluster->mini_master(i)->tablet_peer();
      if (tablet_peer->LeaderStatus() == consensus::LeaderStatus::LEADER_AND_READY) {
        return true;
      }
    }
    return false;
  }, timeout, "Waiting for master leader");
}

Status WaitForLeaderOfSingleTablet(
    MiniCluster* cluster, tablet::TabletPeerPtr leader, MonoDelta duration,
    const std::string& description) {
  return WaitFor([cluster, &leader] {
    auto new_leaders = ListTabletPeers(cluster, ListPeersFilter::kLeaders);
    return new_leaders.size() == 1 && new_leaders[0] == leader;
  }, duration, description);
}

Status StepDown(
    tablet::TabletPeerPtr leader, const std::string& new_leader_uuid,
    ForceStepDown force_step_down, MonoDelta timeout) {
  consensus::LeaderStepDownRequestPB req;
  req.set_tablet_id(leader->tablet_id());
  req.set_new_leader_uuid(new_leader_uuid);
  if (force_step_down) {
    req.set_force_step_down(true);
  }
  return WaitFor([&]() -> Result<bool> {
    consensus::LeaderStepDownResponsePB resp;
    RETURN_NOT_OK(VERIFY_RESULT(leader->GetConsensus())->StepDown(&req, &resp));
    if (resp.has_error()) {
      if (resp.error().code() == tserver::TabletServerErrorPB::LEADER_NOT_READY_TO_STEP_DOWN) {
        LOG(INFO) << "Leader not ready to step down";
        return false;
      }
      return STATUS_FORMAT(RuntimeError, "Step down failed: $0", resp);
    }
    return true;
  }, timeout, Format("Waiting for step down to $0", new_leader_uuid));
  return Status::OK();
}

std::thread RestartsThread(
    MiniCluster* cluster, CoarseDuration interval, std::atomic<bool>* stop_flag) {
  return std::thread([cluster, interval, stop_flag] {
    CDSAttacher attacher;
    SetFlagOnExit set_stop_on_exit(stop_flag);
    int it = 0;
    while (!stop_flag->load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(interval);
      ASSERT_OK(cluster->mini_tablet_server(++it % cluster->num_tablet_servers())->Restart());
    }
  });
}

Status WaitAllReplicasReady(MiniCluster* cluster, MonoDelta timeout) {
  return WaitAllReplicasRunning(
      cluster, timeout,
      [cluster]() {
        return ListTabletPeers(cluster, ListPeersFilter::kAll);
  });
}

Status WaitAllReplicasReady(MiniCluster* cluster, const TableId& table_id, MonoDelta timeout) {
  return WaitAllReplicasRunning(
      cluster, timeout,
      [cluster, &table_id](){
        return ListTableTabletPeers(cluster, table_id);
  });
}

Status WaitAllReplicasHaveIndex(MiniCluster* cluster, int64_t index, MonoDelta timeout) {
  return WaitFor([cluster, index] {
    std::unordered_set<std::string> tablet_ids;
    auto peers = ListTabletPeers(cluster, ListPeersFilter::kAll);
    for (const auto& peer : peers) {
      if (peer->GetLatestLogEntryOpId().index < index) {
        return false;
      }
      tablet_ids.insert(peer->tablet_id());
    }
    auto replication_factor = cluster->num_tablet_servers();
    return tablet_ids.size() * replication_factor == peers.size();
  }, timeout, "Wait for all replicas to have a specific Raft index");
}

template <class Collection>
void PushBackIfNotNull(const typename Collection::value_type& value, Collection* collection) {
  if (value != nullptr) {
    collection->push_back(value);
  }
}

std::vector<rocksdb::DB*> GetAllRocksDbs(MiniCluster* cluster, bool include_intents) {
  std::vector<rocksdb::DB*> dbs;
  for (auto& peer : ListTabletPeers(cluster, ListPeersFilter::kAll)) {
    auto tablet = peer->shared_tablet();
    if (tablet) {
      PushBackIfNotNull(tablet->regular_db(), &dbs);
      if (include_intents) {
        PushBackIfNotNull(tablet->intents_db(), &dbs);
      }
    }
  }
  return dbs;
}

int NumTotalRunningCompactions(MiniCluster* cluster) {
  int compactions = 0;
  for (auto* db : GetAllRocksDbs(cluster)) {
    compactions += down_cast<rocksdb::DBImpl*>(db)->TEST_NumTotalRunningCompactions();
  }
  return compactions;
}

int NumRunningFlushes(MiniCluster* cluster) {
  int flushes = 0;
  for (auto* db : GetAllRocksDbs(cluster)) {
    flushes += down_cast<rocksdb::DBImpl*>(db)->TEST_NumRunningFlushes();
  }
  return flushes;
}

Result<scoped_refptr<master::TableInfo>> FindTable(
    MiniCluster* cluster, const client::YBTableName& table_name) {
  auto& catalog_manager = VERIFY_RESULT(cluster->GetLeaderMiniMaster())->catalog_manager();
  master::TableIdentifierPB identifier;
  table_name.SetIntoTableIdentifierPB(&identifier);
  return catalog_manager.FindTable(identifier);
}

Status WaitForInitDb(MiniCluster* cluster) {
  const auto start_time = CoarseMonoClock::now();
  const auto kTimeout = RegularBuildVsSanitizers(600s, 1800s);
  while (CoarseMonoClock::now() <= start_time + kTimeout) {
    auto leader_mini_master = cluster->GetLeaderMiniMaster();
    if (!leader_mini_master.ok()) {
      continue;
    }
    auto& catalog_manager = (*leader_mini_master)->catalog_manager();
    master::IsInitDbDoneRequestPB req;
    master::IsInitDbDoneResponsePB resp;
    auto status = catalog_manager.IsInitDbDone(&req, &resp);
    if (!status.ok()) {
      LOG(INFO) << "IsInitDbDone failure: " << status;
      continue;
    }
    if (resp.has_initdb_error()) {
      return STATUS_FORMAT(RuntimeError, "Init DB failed: $0", resp.initdb_error());
    }
    if (resp.done()) {
      return Status::OK();
    }
    std::this_thread::sleep_for(500ms);
  }

  return STATUS_FORMAT(TimedOut, "Unable to init db in $0", kTimeout);
}

size_t CountIntents(MiniCluster* cluster, const TabletPeerFilter& filter) {
  size_t result = 0;
  auto peers = ListTabletPeers(cluster, ListPeersFilter::kAll);
  for (const auto &peer : peers) {
    auto tablet = peer->shared_tablet();
    auto participant = tablet ? tablet->transaction_participant() : nullptr;
    if (!participant) {
      continue;
    }
    if (filter && !filter(peer)) {
      continue;
    }
    // TEST_CountIntent return non ok status also means shutdown has started.
    auto intents_count_result = participant->TEST_CountIntents();
    if (intents_count_result.ok() && intents_count_result->num_intents) {
      result += intents_count_result->num_intents;
      LOG(INFO) << Format("T $0 P $1: Intents present: $2, transactions: $3", peer->tablet_id(),
                          peer->permanent_uuid(), intents_count_result->num_intents,
                          intents_count_result->num_transactions);
    }
  }
  return result;
}

MiniTabletServer* FindTabletLeader(MiniCluster* cluster, const TabletId& tablet_id) {
  for (size_t i = 0; i != cluster->num_tablet_servers(); ++i) {
    auto server = cluster->mini_tablet_server(i);
    if (!server->server()) { // Server is shut down.
      continue;
    }
    if (server->server()->LeaderAndReady(tablet_id)) {
      return server;
    }
  }

  return nullptr;
}

void ShutdownAllTServers(MiniCluster* cluster) {
  for (size_t i = 0; i != cluster->num_tablet_servers(); ++i) {
    cluster->mini_tablet_server(i)->Shutdown();
  }
}

Status StartAllTServers(MiniCluster* cluster) {
  for (size_t i = 0; i != cluster->num_tablet_servers(); ++i) {
    RETURN_NOT_OK(cluster->mini_tablet_server(i)->Start(tserver::WaitTabletsBootstrapped::kFalse));
  }

  return Status::OK();
}

void ShutdownAllMasters(MiniCluster* cluster) {
  for (size_t i = 0; i != cluster->num_masters(); ++i) {
    cluster->mini_master(i)->Shutdown();
  }
}

Status StartAllMasters(MiniCluster* cluster) {
  for (size_t i = 0; i != cluster->num_masters(); ++i) {
    RETURN_NOT_OK(cluster->mini_master(i)->Start());
  }

  return Status::OK();
}

void SetupConnectivity(
    rpc::Messenger* messenger, const IpAddress& address, Connectivity connectivity) {
  switch (connectivity) {
    case Connectivity::kOn:
      messenger->RestoreConnectivityTo(address);
      return;
    case Connectivity::kOff:
      messenger->BreakConnectivityTo(address);
      return;
  }
  FATAL_INVALID_ENUM_VALUE(Connectivity, connectivity);
}

Status SetupConnectivity(
    MiniCluster* cluster, size_t idx1, size_t idx2, Connectivity connectivity) {
  for (auto from_idx : {idx1, idx2}) {
    auto to_idx = idx1 ^ idx2 ^ from_idx;
    for (auto type : {server::Private::kFalse, server::Private::kTrue}) {
      // TEST_RpcAddress is 1-indexed; we expect from_idx/to_idx to be 0-indexed.
      auto address = VERIFY_RESULT(HostToAddress(TEST_RpcAddress(to_idx + 1, type)));
      if (from_idx < cluster->num_masters()) {
        SetupConnectivity(
            cluster->mini_master(from_idx)->master()->messenger(), address, connectivity);
      }
      if (from_idx < cluster->num_tablet_servers()) {
        SetupConnectivity(
            cluster->mini_tablet_server(from_idx)->server()->messenger(), address, connectivity);
      }
    }
  }

  return Status::OK();
}

Status BreakConnectivity(MiniCluster* cluster, size_t idx1, size_t idx2) {
  return SetupConnectivity(cluster, idx1, idx2, Connectivity::kOff);
}

Result<size_t> ServerWithLeaders(MiniCluster* cluster) {
  for (size_t i = 0; i != cluster->num_tablet_servers(); ++i) {
    auto* server = cluster->mini_tablet_server(i)->server();
    if (!server) {
      continue;
    }
    auto* ts_manager = server->tablet_manager();
    if (ts_manager->GetLeaderCount() != 0) {
      return i;
    }
  }
  return STATUS(NotFound, "No tablet server with leaders");
}

void SetCompactFlushRateLimitBytesPerSec(MiniCluster* cluster, const size_t bytes_per_sec) {
  LOG(INFO) << "Setting FLAGS_rocksdb_compact_flush_rate_limit_bytes_per_sec to: " << bytes_per_sec
            << " and updating compact/flush rate in existing tablets";
  FLAGS_rocksdb_compact_flush_rate_limit_bytes_per_sec = bytes_per_sec;
  for (auto& tablet_peer : ListTabletPeers(cluster, ListPeersFilter::kAll)) {
    auto tablet_result = tablet_peer->shared_tablet_safe();
    if (!tablet_result.ok()) {
      LOG(WARNING) << "Unable to get tablet: " << tablet_result.status();
      continue;
    }
    auto tablet = *tablet_result;
    for (auto* db : { tablet->regular_db(), tablet->intents_db() }) {
      if (db) {
        db->GetDBOptions().rate_limiter->SetBytesPerSecond(bytes_per_sec);
      }
    }
  }
}

Status WaitAllReplicasSynchronizedWithLeader(
    MiniCluster* cluster, CoarseTimePoint deadline) {
  auto leaders = ListTabletPeers(cluster, ListPeersFilter::kLeaders);
  std::unordered_map<TabletId, int64_t> last_committed_idx;
  for (const auto& peer : leaders) {
    auto idx = VERIFY_RESULT(peer->GetConsensus())->GetLastCommittedOpId().index;
    last_committed_idx.emplace(peer->tablet_id(), idx);
    LOG(INFO) << "Committed op id for " << peer->tablet_id() << ": " << idx;
  }
  auto non_leaders = ListTabletPeers(cluster, ListPeersFilter::kNonLeaders);
  for (const auto& peer : non_leaders) {
    auto it = last_committed_idx.find(peer->tablet_id());
    if (it == last_committed_idx.end()) {
      return STATUS_FORMAT(IllegalState, "Unknown committed op id for $0", peer->tablet_id());
    }
    RETURN_NOT_OK(Wait(
        [idx = it->second, peer]() -> Result<bool> {
          return VERIFY_RESULT(peer->GetConsensus())->GetLastCommittedOpId().index >= idx;
        },
        deadline,
        Format("Wait T $0 P $1 commit $2", peer->tablet_id(), peer->permanent_uuid(), it->second)));
  }
  return Status::OK();
}

Status WaitAllReplicasSynchronizedWithLeader(MiniCluster* cluster, CoarseDuration timeout) {
  return WaitAllReplicasSynchronizedWithLeader(cluster, CoarseMonoClock::Now() + timeout);
}

Status WaitForAnySstFiles(tablet::TabletPeerPtr peer, MonoDelta timeout) {
  CHECK_NOTNULL(peer.get());
  return LoggedWaitFor([peer] {
      auto tablet = peer->shared_tablet();
      if (!tablet)
        return false;
      return tablet->regular_db()->GetCurrentVersionNumSSTFiles() > 0;
    },
    timeout,
    Format("Wait for SST files of peer: $0", peer->permanent_uuid()));
}

Status WaitForAnySstFiles(MiniCluster* cluster, const TabletId& tablet_id, MonoDelta timeout) {
  const auto peers = VERIFY_RESULT(ListTabletActivePeers(cluster, tablet_id));
  SCHECK_EQ(peers.size(), cluster->num_tablet_servers(), IllegalState, "");
  for (const auto& peer : peers) {
    RETURN_NOT_OK(WaitForAnySstFiles(peer, timeout));
  }
  return Status::OK();
}

Status WaitForPeersPostSplitCompacted(
    MiniCluster* cluster, const std::vector<TabletId>& tablet_ids, MonoDelta timeout) {
  std::unordered_set<TabletId> ids(tablet_ids.begin(), tablet_ids.end());
  auto peers = ListTabletPeers(cluster, [&ids](const tablet::TabletPeerPtr& peer) {
    return ids.contains(peer->tablet_id());
  });

  std::stringstream description;
  description << "Waiting for peers [" << CollectionToString(ids) << "] are post split compacted.";

  const auto s = LoggedWaitFor([&peers, &ids](){
    for (size_t n = 0; n < peers.size(); ++n) {
      const auto peer = peers[n];
      if (!peer) {
        continue;
      }
      if (peer->tablet_metadata()->parent_data_compacted()) {
        ids.erase(peer->tablet_id());
        peers[n] = nullptr;
      }
    }
    return ids.empty();
  }, timeout, description.str());

  if (!s.ok() && !ids.empty()) {
    LOG(ERROR) <<
      "Failed to wait for peers [" << CollectionToString(ids) << "] are post split compacted.";
  }
  return s;
}

Status WaitForTableIntentsApplied(
    MiniCluster* cluster, const TableId& table_id, MonoDelta timeout) {
  return WaitFor([cluster, &table_id]() {
    return 0 == CountIntents(cluster, [&table_id](const tablet::TabletPeerPtr &peer) {
      return IsActive(*peer) && IsForTable(*peer, table_id);
    });
  }, timeout, "Did not apply write transactions from intents db in time.");
}

Status WaitForAllIntentsApplied(MiniCluster* cluster, MonoDelta timeout) {
  return WaitForTableIntentsApplied(cluster, /* table_id = */ "", timeout);
}

void ActivateCompactionTimeLogging(MiniCluster* cluster) {
  class CompactionListener : public rocksdb::EventListener {
   public:
    void OnCompactionCompleted(rocksdb::DB* db, const rocksdb::CompactionJobInfo& ci) override {
      LOG(INFO) << "Compaction time: " << ci.stats.elapsed_micros;
    }
  };

  auto listener = std::make_shared<CompactionListener>();

  for (size_t i = 0; i != cluster->num_tablet_servers(); ++i) {
    cluster->GetTabletManager(i)->TEST_tablet_options()->listeners.push_back(listener);
  }
}

void DumpDocDB(MiniCluster* cluster, ListPeersFilter filter) {
  auto peers = ListTabletPeers(cluster, filter, IncludeTransactionStatusTablets::kFalse);
  for (const auto& peer : peers) {
    peer->shared_tablet()->TEST_DocDBDumpToLog(tablet::IncludeIntents::kTrue);
  }
}

std::vector<std::string> DumpDocDBToStrings(MiniCluster* cluster, ListPeersFilter filter) {
  std::vector<std::string> result;
  auto peers = ListTabletPeers(cluster, filter, IncludeTransactionStatusTablets::kFalse);
  for (const auto& peer : peers) {
    result.push_back(peer->shared_tablet()->TEST_DocDBDumpStr());
  }
  return result;
}

}  // namespace yb
