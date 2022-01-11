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
#include <thread>
#include <unordered_set>
#include <vector>

#include "yb/client/client_fwd.h"

#include "yb/gutil/macros.h"

#include "yb/integration-tests/mini_cluster_base.h"

#include "yb/master/master_fwd.h"
#include "yb/master/master_client.fwd.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/tserver/tserver_fwd.h"
#include "yb/tserver/tablet_server_options.h"

#include "yb/util/env.h"
#include "yb/util/port_picker.h"

namespace yb {

namespace master {
class MiniMaster;
}

namespace server {
class SkewedClockDeltaChanger;
}

namespace tserver {
class MiniTabletServer;
}

struct MiniClusterOptions {
  // Number of master servers.
  size_t num_masters = 1;

  // Number of TS to start.
  size_t num_tablet_servers = 1;

  // Number of drives to use on TS.
  int num_drives = 1;

  Env* master_env = Env::Default();

  // Custom Env and rocksdb::Env to be used by MiniTabletServer,
  // otherwise MiniTabletServer will use own Env and rocksdb::Env.
  Env* ts_env = nullptr;
  rocksdb::Env* ts_rocksdb_env = nullptr;

  // Directory in which to store data.
  // Default: empty string, which auto-generates a unique path for this cluster.
  // The default may only be used from a gtest unit test.
  std::string data_root{};

  // Cluster id used to create fs path when we create tests with multiple clusters.
  std::string cluster_id{};
};

// An in-process cluster with a MiniMaster and a configurable
// number of MiniTabletServers for use in tests.
class MiniCluster : public MiniClusterBase {
 public:
  typedef std::vector<std::shared_ptr<master::MiniMaster> > MiniMasters;
  typedef std::vector<std::shared_ptr<tserver::MiniTabletServer> > MiniTabletServers;
  typedef std::vector<uint16_t> Ports;
  typedef MiniClusterOptions Options;

  explicit MiniCluster(const MiniClusterOptions& options);
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
  CHECKED_STATUS CompactTablets();
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

  CHECKED_STATUS AddTServerToBlacklist(const tserver::MiniTabletServer& ts);
  CHECKED_STATUS ClearBlacklist();

  // If this cluster is configured for a single non-distributed
  // master, return the single master. Exits with a CHECK failure if
  // there are multiple masters.
  master::MiniMaster* mini_master() {
    CHECK_EQ(mini_masters_.size(), 1);
    return mini_master(0);
  }

  // Returns the leader Master for this MiniCluster or error if none can be
  // elected within kMasterLeaderElectionWaitTimeSeconds. May block until a leader Master is ready.
  Result<master::MiniMaster*> GetLeaderMiniMaster();

  int LeaderMasterIdx();

  // Returns the Master at index 'idx' for this MiniCluster.
  master::MiniMaster* mini_master(size_t idx);

  // Return number of mini masters.
  size_t num_masters() const { return mini_masters_.size(); }

  // Returns the TabletServer at index 'idx' of this MiniCluster.
  // 'idx' must be between 0 and 'num_tablet_servers' -1.
  tserver::MiniTabletServer* mini_tablet_server(size_t idx);

  tserver::MiniTabletServer* find_tablet_server(const std::string& uuid);

  const MiniTabletServers& mini_tablet_servers() { return mini_tablet_servers_; }

  size_t num_tablet_servers() const { return mini_tablet_servers_.size(); }

  const Ports& tserver_web_ports() const { return tserver_web_ports_; }

  std::string GetMasterFsRoot(size_t indx);

  std::string GetTabletServerFsRoot(size_t idx);

  std::string GetTabletServerDrive(size_t idx, int drive_index);

  // The comma separated string of the master adresses host/ports from current list of masters.
  std::string GetMasterAddresses() const;

    // The comma separated string of the tserver adresses host/ports from current list of tservers.
  std::string GetTserverHTTPAddresses() const;

  std::vector<std::shared_ptr<tablet::TabletPeer>> GetTabletPeers(size_t idx);

  tserver::TSTabletManager* GetTabletManager(size_t idx);

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
  CHECKED_STATUS WaitForTabletServerCount(size_t count);
  CHECKED_STATUS WaitForTabletServerCount(
      size_t count, std::vector<std::shared_ptr<master::TSDescriptor>>* descs);

  // Wait for all tablet servers to be registered. Returns Status::TimedOut if the desired count is
  // not achieved within kRegistrationWaitTimeSeconds.
  CHECKED_STATUS WaitForAllTabletServers();

  uint16_t AllocateFreePort() {
    return port_picker_.AllocateFreePort();
  }

 private:

  void ConfigureClientBuilder(client::YBClientBuilder* builder) override;

  Result<HostPort> DoGetLeaderMasterBoundRpcAddr() override;

  // Allocates ports for the given daemon type and saves them to the ports vector. Does not
  // overwrite values in the ports vector that are non-zero already.
  void AllocatePortsForDaemonType(std::string daemon_type,
                                  size_t num_daemons,
                                  std::string port_type,
                                  std::vector<uint16_t>* ports);

  // Picks free ports for the necessary number of masters / tservers and saves those ports in
  // {master,tserver}_{rpc,web}_ports_ vectors. Values of 0 for the number of masters / tservers
  // mean we pick the maximum number of masters/tservers that we already know we'll need.
  void EnsurePortsAllocated(size_t new_num_masters = 0, size_t num_tservers = 0);

  const MiniClusterOptions options_;
  const std::string fs_root_;

  Ports master_rpc_ports_;
  Ports master_web_ports_;
  Ports tserver_rpc_ports_;
  Ports tserver_web_ports_;

  MiniMasters mini_masters_;
  MiniTabletServers mini_tablet_servers_;

  PortPicker port_picker_;
};

MUST_USE_RESULT std::vector<server::SkewedClockDeltaChanger> SkewClocks(
    MiniCluster* cluster, std::chrono::milliseconds clock_skew);

MUST_USE_RESULT std::vector<server::SkewedClockDeltaChanger> JumpClocks(
    MiniCluster* cluster, std::chrono::milliseconds delta);

void StepDownAllTablets(MiniCluster* cluster);
void StepDownRandomTablet(MiniCluster* cluster);

YB_DEFINE_ENUM(ListPeersFilter, (kAll)(kLeaders)(kNonLeaders));

std::unordered_set<std::string> ListTabletIdsForTable(
    MiniCluster* cluster, const std::string& table_id);

std::unordered_set<std::string> ListActiveTabletIdsForTable(
    MiniCluster* cluster, const std::string& table_id);

std::vector<std::shared_ptr<tablet::TabletPeer>> ListTabletPeers(
    MiniCluster* cluster, ListPeersFilter filter);

std::vector<std::shared_ptr<tablet::TabletPeer>> ListTabletPeers(
    MiniCluster* cluster,
    const std::function<bool(const std::shared_ptr<tablet::TabletPeer>&)>& filter);

std::vector<tablet::TabletPeerPtr> ListTableTabletPeers(
    MiniCluster* cluster, const TableId& table_id);

// By active tablet here we mean tablet is ready or going to be ready to serve read/write requests,
// i.e. not yet completed split or deleted (tombstoned).
std::vector<tablet::TabletPeerPtr> ListTableActiveTabletLeadersPeers(
    MiniCluster* cluster, const TableId& table_id);

std::vector<tablet::TabletPeerPtr> ListTableActiveTabletPeers(
    MiniCluster* cluster, const TableId& table_id);
std::vector<tablet::TabletPeerPtr> ListTableInactiveSplitTabletPeers(
    MiniCluster* cluster, const TableId& table_id);

std::vector<tablet::TabletPeerPtr> ListActiveTabletLeadersPeers(
    MiniCluster* cluster);

CHECKED_STATUS WaitUntilTabletHasLeader(
    MiniCluster* cluster, const std::string& tablet_id, MonoTime deadline);

CHECKED_STATUS WaitForLeaderOfSingleTablet(
    MiniCluster* cluster, tablet::TabletPeerPtr leader, MonoDelta duration,
    const std::string& description);

CHECKED_STATUS WaitUntilMasterHasLeader(MiniCluster* cluster, MonoDelta deadline);

YB_STRONGLY_TYPED_BOOL(ForceStepDown);

CHECKED_STATUS StepDown(
    tablet::TabletPeerPtr leader, const std::string& new_leader_uuid,
    ForceStepDown force_step_down);

// Waits until all tablet peers of the specified cluster are in the Running state.
// And total number of those peers equals to the number of tablet servers for each known tablet.
CHECKED_STATUS WaitAllReplicasReady(MiniCluster* cluster, MonoDelta timeout);

// Waits until all tablet peers of specified cluster have the specified index in their log.
// And total number of those peers equals to the number of tablet servers for each known tablet.
CHECKED_STATUS WaitAllReplicasHaveIndex(MiniCluster* cluster, int64_t index, MonoDelta timeout);

std::thread RestartsThread(
    MiniCluster* cluster, CoarseDuration interval, std::atomic<bool>* stop_flag);

std::vector<rocksdb::DB*> GetAllRocksDbs(MiniCluster* cluster, bool include_intents = true);

int NumTotalRunningCompactions(MiniCluster* cluster);

int NumRunningFlushes(MiniCluster* cluster);

Result<scoped_refptr<master::TableInfo>> FindTable(
    MiniCluster* cluster, const client::YBTableName& table_name);

CHECKED_STATUS WaitForInitDb(MiniCluster* cluster);

using TabletPeerFilter = std::function<bool(const tablet::TabletPeer*)>;
size_t CountIntents(MiniCluster* cluster, const TabletPeerFilter& filter = TabletPeerFilter());

tserver::MiniTabletServer* FindTabletLeader(MiniCluster* cluster, const TabletId& tablet_id);

void ShutdownAllTServers(MiniCluster* cluster);
CHECKED_STATUS StartAllTServers(MiniCluster* cluster);
void ShutdownAllMasters(MiniCluster* cluster);
CHECKED_STATUS StartAllMasters(MiniCluster* cluster);

YB_DEFINE_ENUM(Connectivity, (kOn)(kOff));

CHECKED_STATUS BreakConnectivity(MiniCluster* cluster, size_t idx1, size_t idx2);
CHECKED_STATUS SetupConnectivity(
    MiniCluster* cluster, size_t idx1, size_t idx2, Connectivity connectivity);
Result<int> ServerWithLeaders(MiniCluster* cluster);

// Sets FLAGS_rocksdb_compact_flush_rate_limit_bytes_per_sec and also adjusts rate limiter
// for already created tablets.
void SetCompactFlushRateLimitBytesPerSec(MiniCluster* cluster, size_t bytes_per_sec);

CHECKED_STATUS WaitAllReplicasSynchronizedWithLeader(
    MiniCluster* cluster, CoarseTimePoint deadline);

}  // namespace yb

#endif /* YB_INTEGRATION_TESTS_MINI_CLUSTER_H_ */
