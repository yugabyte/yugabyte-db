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

#include "kudu/integration-tests/mini_cluster.h"


#include "kudu/client/client.h"
#include "kudu/gutil/strings/join.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/master/catalog_manager.h"
#include "kudu/master/master.h"
#include "kudu/master/mini_master.h"
#include "kudu/master/ts_descriptor.h"
#include "kudu/master/ts_manager.h"
#include "kudu/rpc/messenger.h"
#include "kudu/tserver/mini_tablet_server.h"
#include "kudu/tserver/tablet_server.h"
#include "kudu/util/path_util.h"
#include "kudu/util/status.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/test_util.h"

using strings::Substitute;

namespace kudu {

using client::KuduClient;
using client::KuduClientBuilder;
using master::MiniMaster;
using master::TabletLocationsPB;
using master::TSDescriptor;
using std::shared_ptr;
using tserver::MiniTabletServer;
using tserver::TabletServer;

MiniClusterOptions::MiniClusterOptions()
 :  num_masters(1),
    num_tablet_servers(1) {
}

MiniCluster::MiniCluster(Env* env, const MiniClusterOptions& options)
  : running_(false),
    env_(env),
    fs_root_(!options.data_root.empty() ? options.data_root :
                JoinPathSegments(GetTestDataDirectory(), "minicluster-data")),
    num_masters_initial_(options.num_masters),
    num_ts_initial_(options.num_tablet_servers),
    master_rpc_ports_(options.master_rpc_ports),
    tserver_rpc_ports_(options.tserver_rpc_ports) {
  mini_masters_.resize(num_masters_initial_);
}

MiniCluster::~MiniCluster() {
  CHECK(!running_);
}

Status MiniCluster::Start() {
  CHECK(!fs_root_.empty()) << "No Fs root was provided";
  CHECK(!running_);

  if (num_masters_initial_ > 1) {
    CHECK_GE(master_rpc_ports_.size(), num_masters_initial_);
  }

  if (!env_->FileExists(fs_root_)) {
    RETURN_NOT_OK(env_->CreateDir(fs_root_));
  }

  // start the masters
  if (num_masters_initial_ > 1) {
    RETURN_NOT_OK_PREPEND(StartDistributedMasters(),
                          "Couldn't start distributed masters");
  } else {
    RETURN_NOT_OK_PREPEND(StartSingleMaster(), "Couldn't start the single master");
  }

  for (int i = 0; i < num_ts_initial_; i++) {
    RETURN_NOT_OK_PREPEND(AddTabletServer(),
                          Substitute("Error adding TS $0", i));
  }

  RETURN_NOT_OK_PREPEND(WaitForTabletServerCount(num_ts_initial_),
                        "Waiting for tablet servers to start");

  running_ = true;
  return Status::OK();
}

Status MiniCluster::StartDistributedMasters() {
  CHECK_GE(master_rpc_ports_.size(), num_masters_initial_);
  CHECK_GT(master_rpc_ports_.size(), 1);

  LOG(INFO) << "Creating distributed mini masters. Ports: "
            << JoinInts(master_rpc_ports_, ", ");

  for (int i = 0; i < num_masters_initial_; i++) {
    gscoped_ptr<MiniMaster> mini_master(
        new MiniMaster(env_, GetMasterFsRoot(i), master_rpc_ports_[i]));
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

Status MiniCluster::StartSingleMaster() {
  // If there's a single master, 'mini_masters_' must be size 1.
  CHECK_EQ(mini_masters_.size(), 1);
  CHECK_LE(master_rpc_ports_.size(), 1);
  uint16_t master_rpc_port = 0;
  if (master_rpc_ports_.size() == 1) {
    master_rpc_port = master_rpc_ports_[0];
  }

  // start the master (we need the port to set on the servers).
  gscoped_ptr<MiniMaster> mini_master(
      new MiniMaster(env_, GetMasterFsRoot(0), master_rpc_port));
  RETURN_NOT_OK_PREPEND(mini_master->Start(), "Couldn't start master");
  RETURN_NOT_OK(mini_master->master()->
      WaitUntilCatalogManagerIsLeaderAndReadyForTests(MonoDelta::FromSeconds(5)));
  mini_masters_[0] = shared_ptr<MiniMaster>(mini_master.release());
  return Status::OK();
}

Status MiniCluster::AddTabletServer() {
  if (mini_masters_.empty()) {
    return Status::IllegalState("Master not yet initialized");
  }
  int new_idx = mini_tablet_servers_.size();

  uint16_t ts_rpc_port = 0;
  if (tserver_rpc_ports_.size() > new_idx) {
    ts_rpc_port = tserver_rpc_ports_[new_idx];
  }
  gscoped_ptr<MiniTabletServer> tablet_server(
    new MiniTabletServer(GetTabletServerFsRoot(new_idx), ts_rpc_port));

  // set the master addresses
  tablet_server->options()->master_addresses.clear();
  for (const shared_ptr<MiniMaster>& master : mini_masters_) {
    tablet_server->options()->master_addresses.push_back(HostPort(master->bound_rpc_addr()));
  }
  RETURN_NOT_OK(tablet_server->Start())
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
      if (master->master()->catalog_manager()->IsInitialized() &&
          master->master()->catalog_manager()->CheckIsLeaderAndReady().ok()) {
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
                                        int expected_count) {
  TabletLocationsPB locations;
  return WaitForReplicaCount(tablet_id, expected_count, &locations);
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
  return Status::TimedOut(Substitute("Tablet $0 never reached expected replica count $1",
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
  return Status::TimedOut(Substitute("$0 TS(s) never registered with master", count));
}

Status MiniCluster::CreateClient(KuduClientBuilder* builder,
                                 client::sp::shared_ptr<KuduClient>* client) {
  KuduClientBuilder default_builder;
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

} // namespace kudu
