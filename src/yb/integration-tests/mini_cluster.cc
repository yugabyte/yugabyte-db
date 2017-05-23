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
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/path_util.h"
#include "yb/util/status.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_util.h"

using strings::Substitute;

DECLARE_int32(master_svc_num_threads);
DECLARE_int32(master_consensus_svc_num_threads);
DECLARE_int32(master_remote_bootstrap_svc_num_threads);
DECLARE_int32(generic_svc_num_threads);
DECLARE_int32(tablet_server_svc_num_threads);
DECLARE_int32(ts_admin_svc_num_threads);
DECLARE_int32(ts_consensus_svc_num_threads);
DECLARE_int32(ts_remote_bootstrap_svc_num_threads);
DECLARE_int32(default_num_replicas);

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

static const std::vector<uint16_t> EMPTY_MASTER_RPC_PORTS = {};

MiniClusterOptions::MiniClusterOptions()
  : num_masters(1),
    num_tablet_servers(1) {
}

MiniCluster::MiniCluster(Env* env, const MiniClusterOptions& options)
    : running_(false),
      is_creating_(true),
      env_(env),
      fs_root_(!options.data_root.empty()
                   ? options.data_root
                   : JoinPathSegments(GetTestDataDirectory(), "minicluster-data")),
      num_masters_initial_(options.num_masters),
      num_ts_initial_(options.num_tablet_servers) {
  mini_masters_.resize(num_masters_initial_);
}

MiniCluster::~MiniCluster() {
  CHECK(!running_);
}

Status MiniCluster::Start() {
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

  FLAGS_default_num_replicas = num_masters_initial_;

  // start the masters
  RETURN_NOT_OK_PREPEND(StartMasters(),
                        "Couldn't start distributed masters");

  for (int i = 0; i < num_ts_initial_; i++) {
    RETURN_NOT_OK_PREPEND(AddTabletServer(),
                          Substitute("Error adding TS $0", i));
  }

  RETURN_NOT_OK_PREPEND(WaitForTabletServerCount(num_ts_initial_),
                        "Waiting for tablet servers to start");

  running_ = true;
  is_creating_ = false;
  return Status::OK();
}

Status MiniCluster::StartMasters() {
  CHECK_GE(master_rpc_ports_.size(), num_masters_initial_);
  EnsurePortsAllocated();

  LOG(INFO) << "Creating distributed mini masters. RPC ports: "
            << JoinInts(master_rpc_ports_, ", ");

  for (int i = 0; i < num_masters_initial_; i++) {
    gscoped_ptr<MiniMaster> mini_master(
        new MiniMaster(env_, GetMasterFsRoot(i), master_rpc_ports_[i], master_web_ports_[i],
                       is_creating_));
    RETURN_NOT_OK_PREPEND(mini_master->StartDistributedMaster(master_rpc_ports_),
                          Substitute("Couldn't start follower $0", i));
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
  return Status::OK();
}

Status MiniCluster::AddTabletServer() {
  if (mini_masters_.empty()) {
    return STATUS(IllegalState, "Master not yet initialized");
  }
  int new_idx = mini_tablet_servers_.size();

  EnsurePortsAllocated(0 /* num_masters (will pick default) */, new_idx + 1);
  const uint16_t ts_rpc_port = tserver_rpc_ports_[new_idx];
  gscoped_ptr<MiniTabletServer> tablet_server(
    new MiniTabletServer(GetTabletServerFsRoot(new_idx), ts_rpc_port));

  // set the master addresses
  auto master_addr = std::make_shared<vector<HostPort>>();
  for (const shared_ptr<MiniMaster>& master : mini_masters_) {
    master_addr->push_back(HostPort(master->bound_rpc_addr()));
  }
  tablet_server->options()->SetMasterAddresses(master_addr);
  tablet_server->options()->webserver_opts.port = tserver_web_ports_[new_idx];
  RETURN_NOT_OK(tablet_server->Start());
  mini_tablet_servers_.push_back(shared_ptr<MiniTabletServer>(tablet_server.release()));
  return Status::OK();
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
  for (const shared_ptr<MiniTabletServer>& tablet_server : mini_tablet_servers_) {
    tablet_server->Shutdown();
  }
  mini_tablet_servers_.clear();
  for (shared_ptr<MiniMaster>& master_server : mini_masters_) {
    master_server->Shutdown();
    master_server.reset();
  }
  running_ = false;
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
    leader_mini_master()->master()->ts_manager()->GetAllDescriptors(descs);
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

Sockaddr MiniCluster::DoGetLeaderMasterBoundRpcAddr() {
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
  AllocatePortsForDaemonType("master", new_num_masters, "RPC", &master_rpc_ports_);
  AllocatePortsForDaemonType("master", new_num_masters, "web", &master_web_ports_);

  if (new_num_tservers == 0) {
    new_num_tservers = std::max(num_ts_initial_, num_tablet_servers());
  }
  AllocatePortsForDaemonType("tablet server", new_num_tservers, "RPC", &tserver_rpc_ports_);
  AllocatePortsForDaemonType("tablet server", new_num_tservers, "web", &tserver_web_ports_);
}

}  // namespace yb
