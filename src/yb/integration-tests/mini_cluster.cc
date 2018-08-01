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

#include "yb/client/client.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/mini_master.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"
#include "yb/rpc/messenger.h"
#include "yb/server/skewed_clock.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/path_util.h"
#include "yb/util/status.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_util.h"

using strings::Substitute;


DEFINE_string(mini_cluster_base_dir, "", "Directory for master/ts data");
DEFINE_bool(mini_cluster_reuse_data, false, "Reuse data of mini cluster");
DEFINE_int32(mini_cluster_base_port, 0, "Allocate RPC ports starting from this one");
DECLARE_int32(master_svc_num_threads);
DECLARE_int32(memstore_size_mb);
DECLARE_int32(master_consensus_svc_num_threads);
DECLARE_int32(master_remote_bootstrap_svc_num_threads);
DECLARE_int32(generic_svc_num_threads);
DECLARE_int32(tablet_server_svc_num_threads);
DECLARE_int32(ts_admin_svc_num_threads);
DECLARE_int32(ts_consensus_svc_num_threads);
DECLARE_int32(ts_remote_bootstrap_svc_num_threads);
DECLARE_int32(replication_factor);
DECLARE_string(use_private_ip);

namespace yb {

using client::YBClient;
using client::YBClientBuilder;
using master::CatalogManager;
using master::MiniMaster;
using master::TabletLocationsPB;
using master::TSDescriptor;
using std::shared_ptr;
using std::string;
using std::vector;
using tserver::MiniTabletServer;
using tserver::TabletServer;

namespace {

const std::vector<uint16_t> EMPTY_MASTER_RPC_PORTS = {};
const int kMasterLeaderElectionWaitTimeSeconds = NonTsanVsTsan(20, 60);

std::string GetFsRoot(const MiniClusterOptions& options) {
  if (!options.data_root.empty()) {
    return options.data_root;
  }
  if (!FLAGS_mini_cluster_base_dir.empty()) {
    return FLAGS_mini_cluster_base_dir;
  }
  return JoinPathSegments(GetTestDataDirectory(), "minicluster-data");
}

} // namespace

MiniClusterOptions::MiniClusterOptions()
    : num_masters(1),
      num_tablet_servers(1) {
}

MiniCluster::MiniCluster(Env* env, const MiniClusterOptions& options)
    : running_(false),
      env_(env),
      fs_root_(GetFsRoot(options)),
      num_masters_initial_(options.num_masters),
      num_ts_initial_(options.num_tablet_servers),
      next_port_(FLAGS_mini_cluster_base_port) {
  mini_masters_.resize(num_masters_initial_);
}

MiniCluster::~MiniCluster() {
  Shutdown();
}

Status MiniCluster::Start(const std::vector<tserver::TabletServerOptions>& extra_tserver_options) {
  CHECK(!fs_root_.empty()) << "No Fs root was provided";
  CHECK(!running_);

  EnsurePortsAllocated();

  if (!env_->FileExists(fs_root_)) {
    RETURN_NOT_OK(env_->CreateDir(fs_root_));
  }

  // Use conservative number of threads for the mini cluster for unit test env
  // where several unit tests tend to run in parallel.
  // To get default number of threads - try to find SERVICE_POOL_OPTIONS macro usage.
  FLAGS_master_svc_num_threads = 2;
  FLAGS_master_consensus_svc_num_threads = 2;
  FLAGS_master_remote_bootstrap_svc_num_threads = 2;
  FLAGS_generic_svc_num_threads = 2;

  FLAGS_tablet_server_svc_num_threads = 8;
  FLAGS_ts_admin_svc_num_threads = 2;
  FLAGS_ts_consensus_svc_num_threads = 8;
  FLAGS_ts_remote_bootstrap_svc_num_threads = 2;

  // We are testing public/private IPs using mini cluster. So set mode to 'cloud'.
  FLAGS_use_private_ip = "cloud";

  // This dictates the RF of newly created tables.
  if (num_ts_initial_ >= 3) {
    FLAGS_replication_factor = 3;
  } else {
    FLAGS_replication_factor = 1;
  }
  FLAGS_memstore_size_mb = 16;

  // start the masters
  RETURN_NOT_OK_PREPEND(StartMasters(),
                        "Couldn't start distributed masters");

  if (!extra_tserver_options.empty() && extra_tserver_options.size() != num_ts_initial_) {
    return STATUS_SUBSTITUTE(InvalidArgument, "num tserver options: $0 doesn't match with num "
        "tservers: $1", extra_tserver_options.size(), num_ts_initial_);
  }

  for (int i = 0; i < num_ts_initial_; i++) {
    if (!extra_tserver_options.empty()) {
      RETURN_NOT_OK_PREPEND(AddTabletServer(extra_tserver_options[i]),
                            Substitute("Error adding TS $0", i));
    } else {
      RETURN_NOT_OK_PREPEND(AddTabletServer(),
                            Substitute("Error adding TS $0", i));
    }

  }

  RETURN_NOT_OK_PREPEND(WaitForTabletServerCount(num_ts_initial_),
                        "Waiting for tablet servers to start");

  running_ = true;
  return Status::OK();
}

Status MiniCluster::StartMasters() {
  CHECK_GE(master_rpc_ports_.size(), num_masters_initial_);
  EnsurePortsAllocated();

  LOG(INFO) << "Creating distributed mini masters. RPC ports: "
            << JoinInts(master_rpc_ports_, ", ");

  if (mini_masters_.size() < num_masters_initial_) {
    mini_masters_.resize(num_masters_initial_);
  }
  for (int i = 0; i < num_masters_initial_; i++) {
    gscoped_ptr<MiniMaster> mini_master(
        new MiniMaster(env_, GetMasterFsRoot(i), master_rpc_ports_[i], master_web_ports_[i], i));
    auto status = mini_master->StartDistributedMaster(master_rpc_ports_);
    LOG_IF(INFO, !status.ok()) << "Failed to start master: " << status;
    RETURN_NOT_OK_PREPEND(status, Substitute("Couldn't start follower $0", i));
    VLOG(1) << "Started MiniMaster with UUID " << mini_master->permanent_uuid()
            << " at index " << i;
    mini_masters_[i] = shared_ptr<MiniMaster>(mini_master.release());
  }
  int i = 0;
  for (const shared_ptr<MiniMaster>& master : mini_masters_) {
    LOG(INFO) << "Waiting to initialize catalog manager on master " << i++;
    RETURN_NOT_OK_PREPEND(master->WaitForCatalogManagerInit(),
                          Substitute("Could not initialize catalog manager on master $0", i));
  }
  return Status::OK();
}

Status MiniCluster::StartSync() {
  RETURN_NOT_OK(Start());
  int count = 0;
  for (const shared_ptr<MiniTabletServer>& tablet_server : mini_tablet_servers_) {
    RETURN_NOT_OK_PREPEND(tablet_server->WaitStarted(),
                          Substitute("TabletServer $0 failed to start.", count));
    count++;
  }
  return Status::OK();
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
    CHECK_OK(master_server->Restart());
    CHECK_OK(master_server->WaitForCatalogManagerInit());
  }

  RETURN_NOT_OK_PREPEND(WaitForTabletServerCount(num_tablet_servers()),
                        "Waiting for tablet servers to start");

  return Status::OK();
}

Status MiniCluster::AddTabletServer(const tserver::TabletServerOptions& extra_opts) {
  if (mini_masters_.empty()) {
    return STATUS(IllegalState, "Master not yet initialized");
  }
  int new_idx = mini_tablet_servers_.size();

  EnsurePortsAllocated(0 /* num_masters (will pick default) */, new_idx + 1);
  const uint16_t ts_rpc_port = tserver_rpc_ports_[new_idx];
  gscoped_ptr<MiniTabletServer> tablet_server(
    new MiniTabletServer(GetTabletServerFsRoot(new_idx), ts_rpc_port, extra_opts, new_idx));

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
  RETURN_NOT_OK(tablet_server->Start());
  mini_tablet_servers_.push_back(shared_ptr<MiniTabletServer>(tablet_server.release()));
  return Status::OK();
}

Status MiniCluster::AddTabletServer() {
  auto options = tserver::TabletServerOptions::CreateTabletServerOptions();
  RETURN_NOT_OK(options);
  return AddTabletServer(*options);
}

MiniMaster* MiniCluster::leader_mini_master() {
  Stopwatch sw;
  sw.start();
  while (sw.elapsed().wall_seconds() < kMasterLeaderElectionWaitTimeSeconds) {
    for (int i = 0; i < mini_masters_.size(); i++) {
      MiniMaster* master = mini_master(i);
      if (master->master()->IsShutdown()) {
        continue;
      }
      CatalogManager::ScopedLeaderSharedLock l(master->master()->catalog_manager());
      if (l.catalog_status().ok() && l.leader_status().ok()) {
        return master;
      }
    }
    SleepFor(MonoDelta::FromMilliseconds(1));
  }
  LOG(ERROR) << "No leader master elected after " << kMasterLeaderElectionWaitTimeSeconds
             << " seconds.";
  return nullptr;
}

void MiniCluster::Shutdown() {
  if (!running_)
    return;

  for (const shared_ptr<MiniTabletServer>& tablet_server : mini_tablet_servers_) {
    tablet_server->Shutdown();
  }
  mini_tablet_servers_.clear();

  for (shared_ptr<MiniMaster>& master_server : mini_masters_) {
    master_server->Shutdown();
    master_server.reset();
  }
  mini_masters_.clear();

  running_ = false;
}

Status MiniCluster::FlushTablets(tablet::FlushFlags flags) {
  for (const auto& tablet_server : mini_tablet_servers_) {
    RETURN_NOT_OK(tablet_server->FlushTablets(flags));
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

void MiniCluster::ShutdownMasters() {
  for (shared_ptr<MiniMaster>& master_server : mini_masters_) {
    master_server->Shutdown();
    master_server.reset();
  }
}

MiniMaster* MiniCluster::mini_master(int idx) {
  CHECK_GE(idx, 0) << "Master idx must be >= 0";
  CHECK_LT(idx, mini_masters_.size()) << "Master idx must be < num masters started";
  return mini_masters_[idx].get();
}

MiniTabletServer* MiniCluster::mini_tablet_server(int idx) {
  CHECK_GE(idx, 0) << "TabletServer idx must be >= 0";
  CHECK_LT(idx, mini_tablet_servers_.size()) << "TabletServer idx must be < 'num_ts_started_'";
  return mini_tablet_servers_[idx].get();
}

MiniTabletServer* MiniCluster::find_tablet_server(const std::string& uuid) {
  for (const auto& server : mini_tablet_servers_) {
    if (server->server()->instance_pb().permanent_uuid() == uuid) {
      return server.get();
    }
  }
  return nullptr;
}

string MiniCluster::GetMasterFsRoot(int idx) {
  return JoinPathSegments(fs_root_, Substitute("master-$0-root", idx));
}

string MiniCluster::GetTabletServerFsRoot(int idx) {
  return JoinPathSegments(fs_root_, Substitute("ts-$0-root", idx));
}

Status MiniCluster::WaitForReplicaCount(const string& tablet_id,
                                        int expected_count,
                                        TabletLocationsPB* locations) {
  Stopwatch sw;
  sw.start();
  while (sw.elapsed().wall_seconds() < kTabletReportWaitTimeSeconds) {
    Status s =
        leader_mini_master()->master()->catalog_manager()->GetTabletLocations(tablet_id, locations);
    if (s.ok() && ((locations->stale() && expected_count == 0) ||
        (!locations->stale() && locations->replicas_size() == expected_count))) {
      return Status::OK();
    }

    SleepFor(MonoDelta::FromMilliseconds(1));
  }
  return STATUS(TimedOut, Substitute("Tablet $0 never reached expected replica count $1",
                                     tablet_id, expected_count));
}

Status MiniCluster::WaitForTabletServerCount(int count) {
  vector<shared_ptr<master::TSDescriptor> > descs;
  return WaitForTabletServerCount(count, &descs);
}

Status MiniCluster::WaitForTabletServerCount(int count,
                                             vector<shared_ptr<TSDescriptor> >* descs) {
  Stopwatch sw;
  sw.start();
  while (sw.elapsed().wall_seconds() < kRegistrationWaitTimeSeconds) {
    auto leader = leader_mini_master();
    if (leader) {
      leader->master()->ts_manager()->GetAllDescriptors(descs);
      if (descs->size() == count) {
        // GetAllDescriptors() may return servers that are no longer online.
        // Do a second step of verification to verify that the descs that we got
        // are aligned (same uuid/seqno) with the TSs that we have in the cluster.
        int match_count = 0;
        for (const shared_ptr<TSDescriptor>& desc : *descs) {
          for (auto mini_tablet_server : mini_tablet_servers_) {
            auto ts = mini_tablet_server->server();
            if (ts->instance_pb().permanent_uuid() == desc->permanent_uuid() &&
                ts->instance_pb().instance_seqno() == desc->latest_seqno()) {
              match_count++;
              break;
            }
          }
        }

        if (match_count == count) {
          LOG(INFO) << count << " TS(s) registered with Master after "
                    << sw.elapsed().wall_seconds() << "s";
          return Status::OK();
        }
      }
    }

    SleepFor(MonoDelta::FromMilliseconds(1));
  }
  return STATUS(TimedOut, Substitute("$0 TS(s) never registered with master", count));
}

Status MiniCluster::DoCreateClient(YBClientBuilder* builder,
                                   std::shared_ptr<YBClient>* client) {
  YBClientBuilder default_builder;
  if (builder == nullptr) {
    builder = &default_builder;
  }
  builder->clear_master_server_addrs();
  for (const shared_ptr<MiniMaster>& master : mini_masters_) {
    CHECK(master);
    builder->add_master_server_addr(master->bound_rpc_addr_str());
  }
  return builder->Build(client);
}

HostPort MiniCluster::DoGetLeaderMasterBoundRpcAddr() {
  return leader_mini_master()->bound_rpc_addr();
}

void MiniCluster::AllocatePortsForDaemonType(
    const string daemon_type,
    const int num_daemons,
    const string port_type,
    std::vector<uint16_t>* ports) {
  const size_t old_size = ports->size();
  if (ports->size() < num_daemons) {
    ports->resize(num_daemons, 0 /* default value */);
  }
  for (int i = old_size; i < num_daemons; ++i) {
    if ((*ports)[i] == 0) {
      const uint16_t new_port = port_picker_.AllocateFreePort();
      (*ports)[i] = new_port;
      LOG(INFO) << "Using auto-assigned port " << new_port << " for a " << daemon_type
                << " " << port_type << " port";
    }
  }
}

void MiniCluster::EnsurePortsAllocated(int new_num_masters, int new_num_tservers) {
  if (new_num_masters == 0) {
    new_num_masters = std::max(num_masters_initial_, num_masters());
  }
  if (next_port_) {
    while (master_rpc_ports_.size() < new_num_masters) {
      master_rpc_ports_.push_back(next_port_++);
    }
  } else {
    AllocatePortsForDaemonType("master", new_num_masters, "RPC", &master_rpc_ports_);
  }
  AllocatePortsForDaemonType("master", new_num_masters, "web", &master_web_ports_);

  if (new_num_tservers == 0) {
    new_num_tservers = std::max(num_ts_initial_, num_tablet_servers());
  }
  if (next_port_) {
    while (tserver_rpc_ports_.size() < new_num_tservers) {
      tserver_rpc_ports_.push_back(next_port_++);
    }
  } else {
    AllocatePortsForDaemonType("tablet server", new_num_tservers, "RPC", &tserver_rpc_ports_);
  }
  AllocatePortsForDaemonType("tablet server", new_num_tservers, "web", &tserver_web_ports_);
}

std::vector<server::SkewedClockDeltaChanger> SkewClocks(
    MiniCluster* cluster, std::chrono::milliseconds clock_skew) {
  std::vector<server::SkewedClockDeltaChanger> delta_changers;
  for (int i = 0; i != cluster->num_tablet_servers(); ++i) {
    auto* tserver = cluster->mini_tablet_server(i)->server();
    auto* hybrid_clock = down_cast<server::HybridClock*>(tserver->clock());
    delta_changers.emplace_back(
        i * clock_skew, std::static_pointer_cast<server::SkewedClock>(hybrid_clock->TEST_clock()));
  }
  return delta_changers;
}

}  // namespace yb
