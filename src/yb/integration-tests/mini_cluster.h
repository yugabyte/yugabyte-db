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

#ifndef YB_INTEGRATION_TESTS_MINI_CLUSTER_H_
#define YB_INTEGRATION_TESTS_MINI_CLUSTER_H_

#include <memory>
#include <string>
#include <vector>

#include "yb/integration-tests/mini_cluster_base.h"
#include "yb/gutil/macros.h"
#include "yb/tablet/tablet.h"
#include "yb/tserver/tablet_server_options.h"
#include "yb/util/env.h"
#include "yb/util/port_picker.h"
#include "yb/util/tsan_util.h"

namespace yb {

namespace client {
class YBClient;
class YBClientBuilder;
}

namespace master {
class MiniMaster;
class TSDescriptor;
class TabletLocationsPB;
}

namespace server {
class SkewedClockDeltaChanger;
}

namespace tablet {
class TabletPeer;
}

namespace tserver {
class MiniTabletServer;
}

struct MiniClusterOptions {
  MiniClusterOptions();

  // Number of master servers.
  // Default: 1
  int num_masters;

  // Number of TS to start.
  // Default: 1
  int num_tablet_servers;

  // Directory in which to store data.
  // Default: "", which auto-generates a unique path for this cluster.
  // The default may only be used from a gtest unit test.
  std::string data_root;
};

// An in-process cluster with a MiniMaster and a configurable
// number of MiniTabletServers for use in tests.
class MiniCluster : public MiniClusterBase {
 public:
  typedef std::vector<std::shared_ptr<master::MiniMaster> > MiniMasters;
  typedef std::vector<std::shared_ptr<tserver::MiniTabletServer> > MiniTabletServers;
  typedef std::vector<uint16_t> Ports;

  MiniCluster(Env* env, const MiniClusterOptions& options);
  ~MiniCluster();

  // Start a cluster with a Master and 'num_tablet_servers' TabletServers.
  // All servers run on the loopback interface with ephemeral ports.
  CHECKED_STATUS Start(
      const std::vector<tserver::TabletServerOptions>& extra_tserver_options =
      std::vector<tserver::TabletServerOptions>());

  // Like the previous method but performs initialization synchronously, i.e.
  // this will wait for all TS's to be started and initialized. Tests should
  // use this if they interact with tablets immediately after Start();
  CHECKED_STATUS StartSync();

  // Stop and restart the mini cluster synchronously. The cluster's persistent state will be kept.
  CHECKED_STATUS RestartSync();

  void Shutdown();
  CHECKED_STATUS FlushTablets(
      tablet::FlushMode mode = tablet::FlushMode::kSync,
      tablet::FlushFlags flags = tablet::FlushFlags::kAll);
  CHECKED_STATUS SwitchMemtables();
  CHECKED_STATUS CleanTabletLogs();

  // Shuts down masters only.
  void ShutdownMasters();

  // Setup a consensus configuration of distributed masters, with count specified in
  // 'options'. Requires that a reserve RPC port is specified in
  // 'options' for each master.
  CHECKED_STATUS StartMasters();

  // Add a new TS to the cluster. The new TS is started.
  // Requires that the master is already running.
  CHECKED_STATUS AddTabletServer(const tserver::TabletServerOptions& extra_opts);

  // Same as above, but get options from flags.
  CHECKED_STATUS AddTabletServer();

  // If this cluster is configured for a single non-distributed
  // master, return the single master. Exits with a CHECK failure if
  // there are multiple masters.
  master::MiniMaster* mini_master() {
    CHECK_EQ(mini_masters_.size(), 1);
    return mini_master(0);
  }

  // Returns the leader Master for this MiniCluster or NULL if none can be
  // found. May block until a leader Master is ready.
  master::MiniMaster* leader_mini_master();

  // Returns the Master at index 'idx' for this MiniCluster.
  master::MiniMaster* mini_master(int idx);

  // Return number of mini masters.
  int num_masters() const { return mini_masters_.size(); }

  // Returns the TabletServer at index 'idx' of this MiniCluster.
  // 'idx' must be between 0 and 'num_tablet_servers' -1.
  tserver::MiniTabletServer* mini_tablet_server(int idx);

  tserver::MiniTabletServer* find_tablet_server(const std::string& uuid);

  const MiniTabletServers& mini_tablet_servers() { return mini_tablet_servers_; }

  int num_tablet_servers() const { return mini_tablet_servers_.size(); }

  const Ports& tserver_web_ports() const { return tserver_web_ports_; }

  std::string GetMasterFsRoot(int indx);

  std::string GetTabletServerFsRoot(int idx);

  std::vector<std::shared_ptr<tablet::TabletPeer>> GetTabletPeers(int idx);

  // Wait for the given tablet to have 'expected_count' replicas
  // reported on the master. Returns the locations in '*locations'.
  // Requires that the master has started;
  // Returns a bad Status if the tablet does not reach the required count
  // within kTabletReportWaitTimeSeconds.
  CHECKED_STATUS WaitForReplicaCount(const std::string& tablet_id,
                             int expected_count,
                             master::TabletLocationsPB* locations);

  // Wait until the number of registered tablet servers reaches the given
  // count. Returns Status::TimedOut if the desired count is not achieved
  // within kRegistrationWaitTimeSeconds.
  CHECKED_STATUS WaitForTabletServerCount(int count);
  CHECKED_STATUS WaitForTabletServerCount(int count,
                                  std::vector<std::shared_ptr<master::TSDescriptor> >* descs);

 private:

  enum {
    kTabletReportWaitTimeSeconds = 5,
    kRegistrationWaitTimeSeconds = NonTsanVsTsan(30, 60)
  };

  // Create a client configured to talk to this cluster. Builder may contain
  // override options for the client. The master address will be overridden to
  // talk to the running master. If 'builder' is NULL, default options will be
  // used.
  //
  // REQUIRES: the cluster must have already been Start()ed.
  virtual CHECKED_STATUS DoCreateClient(client::YBClientBuilder* builder,
      std::shared_ptr<client::YBClient>* client);

  virtual HostPort DoGetLeaderMasterBoundRpcAddr();

  // Allocates ports for the given daemon type and saves them to the ports vector. Does not
  // overwrite values in the ports vector that are non-zero already.
  void AllocatePortsForDaemonType(std::string daemon_type,
                                  int num_daemons,
                                  std::string port_type,
                                  std::vector<uint16_t>* ports);

  // Picks free ports for the necessary number of masters / tservers and saves those ports in
  // {master,tserver}_{rpc,web}_ports_ vectors. Values of 0 for the number of masters / tservers
  // mean we pick the maximum number of masters/tservers that we already know we'll need.
  void EnsurePortsAllocated(int new_num_masters = 0, int num_tservers = 0);

  std::atomic<bool> running_ { false };

  Env* const env_ = nullptr;
  const std::string fs_root_;
  const int num_masters_initial_;
  const int num_ts_initial_;

  Ports master_rpc_ports_;
  Ports master_web_ports_;
  Ports tserver_rpc_ports_;
  Ports tserver_web_ports_;
  uint16_t next_port_ = 0;

  MiniMasters mini_masters_;
  MiniTabletServers mini_tablet_servers_;

  PortPicker port_picker_;
};

MUST_USE_RESULT std::vector<server::SkewedClockDeltaChanger> SkewClocks(
    MiniCluster* cluster, std::chrono::milliseconds clock_skew);

}  // namespace yb

#endif /* YB_INTEGRATION_TESTS_MINI_CLUSTER_H_ */
