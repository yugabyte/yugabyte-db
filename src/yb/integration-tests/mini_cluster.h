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

#ifndef YB_INTEGRATION_TESTS_MINI_CLUSTER_H_
#define YB_INTEGRATION_TESTS_MINI_CLUSTER_H_

#include <memory>
#include <string>
#include <vector>

#include "yb/gutil/macros.h"
#include "yb/util/env.h"

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

  // List of RPC ports for the master to run on.
  // Defaults to a list 0 (ephemeral ports).
  std::vector<uint16_t> master_rpc_ports;

  // List of RPC ports for the tservers to run on.
  // Defaults to a list of 0 (ephemeral ports).
  std::vector<uint16_t> tserver_rpc_ports;
};

// An in-process cluster with a MiniMaster and a configurable
// number of MiniTabletServers for use in tests.
class MiniCluster {
 public:
  MiniCluster(Env* env, const MiniClusterOptions& options);
  ~MiniCluster();

  // Start a cluster with a Master and 'num_tablet_servers' TabletServers.
  // All servers run on the loopback interface with ephemeral ports.
  CHECKED_STATUS Start();

  // Like the previous method but performs initialization synchronously, i.e.
  // this will wait for all TS's to be started and initialized. Tests should
  // use this if they interact with tablets immediately after Start();
  CHECKED_STATUS StartSync();

  // Stop and restart the mini cluster synchronously. The cluster's persistent state will be kept.
  CHECKED_STATUS RestartSync();

  void Shutdown();

  // Shuts down masters only.
  void ShutdownMasters();

  // Setup a consensus configuration of distributed masters, with count specified in
  // 'options'. Requires that a reserve RPC port is specified in
  // 'options' for each master.
  CHECKED_STATUS StartDistributedMasters();

  // Add a new standalone master to the cluster. The new master is started.
  CHECKED_STATUS StartSingleMaster();

  // Add a new TS to the cluster. The new TS is started.
  // Requires that the master is already running.
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

  int num_tablet_servers() const { return mini_tablet_servers_.size(); }

  std::string GetMasterFsRoot(int indx);

  std::string GetTabletServerFsRoot(int idx);

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

  // Create a client configured to talk to this cluster. Builder may contain
  // override options for the client. The master address will be overridden to
  // talk to the running master. If 'builder' is NULL, default options will be
  // used.
  //
  // REQUIRES: the cluster must have already been Start()ed.
  CHECKED_STATUS CreateClient(client::YBClientBuilder* builder,
                      std::shared_ptr<client::YBClient>* client);

  // Allocates a free port and stores a file lock guarding access to that port into an internal
  // array of file locks.
  uint16_t AllocateFreePort();

 private:
  enum {
    kTabletReportWaitTimeSeconds = 5,
    kRegistrationWaitTimeSeconds = 30,
    kMasterLeaderElectionWaitTimeSeconds = 10
  };

  bool running_;
  bool is_creating_;

  Env* const env_;
  const std::string fs_root_;
  const int num_masters_initial_;
  const int num_ts_initial_;

  std::vector<uint16_t> master_rpc_ports_;
  const std::vector<uint16_t> tserver_rpc_ports_;

  std::vector<std::shared_ptr<master::MiniMaster> > mini_masters_;
  std::vector<std::shared_ptr<tserver::MiniTabletServer> > mini_tablet_servers_;
  std::vector<std::unique_ptr<FileLock> > free_port_file_locks_;
};

}  // namespace yb

#endif /* YB_INTEGRATION_TESTS_MINI_CLUSTER_H */
