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

#pragma once

#include <string.h>
#include <sys/types.h>

#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest_prod.h>
#include <rapidjson/document.h>

#include "yb/common/entity_ids_types.h"

#include "yb/consensus/consensus_fwd.h"
#include "yb/consensus/consensus_types.pb.h"
#include "yb/consensus/metadata.pb.h"

#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/integration-tests/mini_cluster_base.h"

#include "yb/server/server_fwd.h"

#include "yb/tserver/tserver_fwd.h"
#include "yb/tserver/tserver_types.pb.h"

#include "yb/util/curl_util.h"
#include "yb/util/jsonreader.h"
#include "yb/util/metrics.h"
#include "yb/util/status_fwd.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"
#include "yb/util/status.h"
#include "yb/util/tsan_util.h"

namespace yb {

using rapidjson::Value;
using strings::Substitute;

class ExternalDaemon;
class ExternalMaster;
class ExternalTabletServer;
class HostPort;
class OpIdPB;
class NodeInstancePB;
class Subprocess;

namespace rpc {
class SecureContext;
}

namespace server {
class ServerStatusPB;
}  // namespace server

using yb::consensus::ChangeConfigType;

struct ExternalMiniClusterOptions {

  // Number of masters to start.
  size_t num_masters = 1;

  // Number of TS to start.
  size_t num_tablet_servers = 1;

  // Number of drives to use on TS.
  int num_drives = 1;

  // If more than one master is specified, list of ports for the masters in a consensus
  // configuration. Port at index 0 is used for the leader master.
  // Default: one entry as num_masters defaults to 1. Value 0 implies, a free port
  //          is picked at runtime.
  std::vector<uint16_t> master_rpc_ports = { 0 };

  static constexpr bool kDefaultStartCqlProxy = true;
#if defined(__APPLE__)
  static constexpr bool kBindToUniqueLoopbackAddress = true; // Older Mac OS may need
                                                             // to set this to false.
#else
  static constexpr bool kBindToUniqueLoopbackAddress = true;
#endif

  bool enable_ysql = false;

  // Directory in which to store data.
  // Default: "", which auto-generates a unique path for this cluster.
  std::string data_root{};

  // Set data_root_counter to non-negative number if your test run need to create an new and empty
  // cluster every time ExternalMiniCluster() is constructed.
  // - During a test run, data_root will stay the same until the run is finished, so recreating a
  //   brand new cluster from scratch is not possible because the database location stays the same.
  // - When data_root_counter is non-negative, a new "data_root" is generated every time
  //   ExternalMiniCluster() is constructed.
  int data_root_counter = -1;

  // If true, binds each tablet server to a different loopback address.  This affects the server's
  // RPC server, and also forces the server to only use this IP address for outgoing socket
  // connections as well.  This allows the use of iptables on the localhost to simulate network
  // partitions.
  //
  // The addressed used are 127.<A>.<B>.<C> where:
  // - <A,B> are the high and low bytes of the pid of the process running the minicluster (not the
  //   daemon itself).
  // - <C> is the index of the server within this minicluster.
  //
  // This requires that the system is set up such that processes may bind to any IP address in the
  // localhost netblock (127.0.0.0/8). This seems to be the case on common Linux distributions. You
  // can verify by running 'ip addr | grep 127.0.0.1' and checking that the address is listed as
  // '127.0.0.1/8'.
  //
  // This option is disabled by default on OS X.
  // Enabling of this option on OS X means usage of default IPs: 127.0.0.x.
  bool bind_to_unique_loopback_addresses = kBindToUniqueLoopbackAddress;

  // If true, second and other TSes will use the same ports as the first TS uses.
  // Else every TS will allocate unique ports for itself.
  // The option is applicable ONLY with bind_to_unique_loopback_addresses == true.
  bool use_same_ts_ports = false;

  // The path where the yb daemons should be run from.
  // Default: "../bin", which points to the path where non-test executables are located.
  // This works for unit tests, since they all end up in build/latest/test-<subproject_name>.
  std::string daemon_bin_path{};

  // Extra flags for tablet servers and masters respectively.
  //
  // In these flags, you may use the special string '${index}' which will
  // be substituted with the index of the tablet server or master.
  std::vector<std::string> extra_tserver_flags;
  std::vector<std::string> extra_master_flags;

  // Default timeout for operations involving RPC's, when none provided in the API.
  // Default : 10sec
  MonoDelta timeout = MonoDelta::FromSeconds(10);

  // If true logs will be written in both stderr and file
  bool log_to_file = false;

  // Use even IPs for cluster, like we have for MiniCluster.
  // So it could be used with test certificates.
  bool use_even_ips = false;

  // Cluster id used to create fs path when we create tests with multiple clusters.
  std::string cluster_id;

  // By default, we create max(2, num_tablet_servers) tablets per transaction table. If this is
  // set to a non-zero value, this value is used instead.
  int transaction_table_num_tablets = 0;

  Status RemovePort(const uint16_t port);
  Status AddPort(const uint16_t port);

  // Make sure we have the correct number of master RPC ports specified.
  void AdjustMasterRpcPorts();
};

// A mini-cluster made up of subprocesses running each of the daemons separately. This is useful for
// black-box or grey-box failure testing purposes -- it provides the ability to forcibly kill or
// stop particular cluster participants, which isn't feasible in the normal MiniCluster.  On the
// other hand, there is little access to inspect the internal state of the daemons.
class ExternalMiniCluster : public MiniClusterBase {
 public:
  typedef ExternalMiniClusterOptions Options;

  // Mode to which node types a certain action (like Shutdown()) should apply.
  enum NodeSelectionMode {
    TS_ONLY,
    ALL
  };

  // Threshold of the number of retries for master related rpc calls.
  static const int kMaxRetryIterations = 100;

  explicit ExternalMiniCluster(const ExternalMiniClusterOptions& opts);
  ~ExternalMiniCluster();

  // Start the cluster.
  Status Start(rpc::Messenger* messenger = nullptr);

  // Restarts the cluster. Requires that it has been Shutdown() first.
  // TODO: #14902 Shutdown the process if it is running.
  Status Restart();

  // Like the previous method but performs initialization synchronously, i.e.  this will wait for
  // all TS's to be started and initialized. Tests should use this if they interact with tablets
  // immediately after Start();
  Status StartSync();

  // Add a new TS to the cluster. The new TS is started.  Requires that the master is already
  // running.
  Status AddTabletServer(
      bool start_cql_proxy = ExternalMiniClusterOptions::kDefaultStartCqlProxy,
      const std::vector<std::string>& extra_flags = {},
      int num_drives = -1);

  void UpdateMasterAddressesOnTserver();

  // Shuts down the whole cluster or part of it, depending on the selected 'mode'.  Currently, this
  // uses SIGKILL on each daemon for a non-graceful shutdown.
  void Shutdown(NodeSelectionMode mode = ALL);

  // Waits for the master to finishing running initdb.
  Status WaitForInitDb();

  // Return the IP address that the tablet server with the given index will bind to.  If
  // options.bind_to_unique_loopback_addresses is false, this will be 127.0.0.1 Otherwise, it is
  // another IP in the local netblock.
  std::string GetBindIpForTabletServer(size_t index) const;

  // Return a pointer to the running leader master. This may be NULL
  // if the cluster is not started.
  // WARNING: If leader master is not elected after kMaxRetryIterations, first available master
  // will be returned.
  ExternalMaster* GetLeaderMaster();

  // Perform an RPC to determine the leader of the external mini cluster.  Set 'index' to the leader
  // master's index (for calls to to master() below).
  //
  // NOTE: if a leader election occurs after this method is executed, the last result may not be
  // valid.
  Result<size_t> GetLeaderMasterIndex();

  // Return a non-leader master index
  Result<size_t> GetFirstNonLeaderMasterIndex();

  Result<size_t> GetTabletLeaderIndex(const yb::TableId& tablet_id);

  // The comma separated string of the master adresses host/ports from current list of masters.
  std::string GetMasterAddresses() const;

  std::string GetTabletServerAddresses() const;

  std::string GetTabletServerHTTPAddresses() const;

  // Start a new master with `peer_addrs` as the master_addresses parameter.
  Result<ExternalMaster *> StartMasterWithPeers(const std::string& peer_addrs);

  // Send a ping request to the rpc port of the master. Return OK() only if it is reachable.
  Status PingMaster(const ExternalMaster* master) const;

  // Add a Tablet Server to the blacklist.
  Status AddTServerToBlacklist(ExternalMaster* master, ExternalTabletServer* ts);

  // Returns the min_num_replicas corresponding to a PlacementBlockPB.
  Status GetMinReplicaCountForPlacementBlock(
    ExternalMaster* master,
    const std::string& cloud, const std::string& region, const std::string& zone,
    int* min_num_replicas);
  // Add a Tablet Server to the leader blacklist.
  Status AddTServerToLeaderBlacklist(ExternalMaster* master, ExternalTabletServer* ts);

  // Empty blacklist.
  Status ClearBlacklist(ExternalMaster* master);

  // Starts a new master and returns the handle of the new master object on success.  Not thread
  // safe for now. We could move this to a static function outside External Mini Cluster, but
  // keeping it here for now as it is currently used only in conjunction with EMC.  If there are any
  // errors and if a new master could not be spawned, it will crash internally.
  void StartShellMaster(ExternalMaster** new_master);

  // Performs an add or remove from the existing config of this EMC, of the given master.
  // When use_hostport is true, the master is deemed as dead and its UUID is not used.
  Status ChangeConfig(ExternalMaster* master,
      ChangeConfigType type,
      consensus::PeerMemberType member_type = consensus::PeerMemberType::PRE_VOTER,
      bool use_hostport = false);

  // Performs an RPC to the given master to get the number of masters it is tracking in-memory.
  Status GetNumMastersAsSeenBy(ExternalMaster* master, int* num_peers);

  // Get the last committed opid for the current leader master.
  Status GetLastOpIdForLeader(OpIdPB* opid);

  // The leader master sometimes does not commit the config in time on first setup, causing
  // CheckHasCommittedOpInCurrentTermUnlocked check - that the current term should have had at least
  // one commit - to fail. This API waits for the leader's commit term to move ahead by one.
  Status WaitForLeaderCommitTermAdvance();

  // This API waits for the commit indices of all the master peers to reach the target index.
  Status WaitForMastersToCommitUpTo(int64_t target_index);

  // This API waits for the commit indices of the given master peers to reach the target index.
  Status WaitForMastersToCommitUpTo(
      int64_t target_index, const std::vector<ExternalMaster*>& masters,
      MonoDelta timeout = MonoDelta());

  Status WaitForAllIntentsApplied(const MonoDelta& timeout);

  Status WaitForAllIntentsApplied(ExternalTabletServer* ts, const MonoDelta& timeout);

  Status WaitForAllIntentsApplied(ExternalTabletServer* ts, const MonoTime& deadline);

  // If this cluster is configured for a single non-distributed master, return the single master or
  // NULL if the master is not started. Exits with a CHECK failure if there are multiple masters.
  ExternalMaster* master() const;

  // Return master at 'idx' or NULL if the master at 'idx' has not been started.
  ExternalMaster* master(size_t idx) const;

  ExternalTabletServer* tablet_server(size_t idx) const;

  // Return ExternalTabletServer given its UUID. If not found, returns NULL.
  ExternalTabletServer* tablet_server_by_uuid(const std::string& uuid) const;

  // Return the index of the ExternalTabletServer that has the given 'uuid', or -1 if no such UUID
  // can be found.
  int tablet_server_index_by_uuid(const std::string& uuid) const;

  // Return all masters.
  std::vector<ExternalMaster*> master_daemons() const;

  // Return all tablet servers and masters.
  std::vector<ExternalDaemon*> daemons() const;

  // Return all tablet servers.
  std::vector<ExternalTabletServer*> tserver_daemons() const;

  // Get tablet server host.
  HostPort pgsql_hostport(int node_index) const;

  size_t num_tablet_servers() const {
    return tablet_servers_.size();
  }

  size_t num_masters() const {
    return masters_.size();
  }

  // Return the client messenger used by the ExternalMiniCluster.
  rpc::Messenger* messenger();

  rpc::ProxyCache& proxy_cache() {
    return *proxy_cache_;
  }

  // Get the master leader consensus proxy.
  consensus::ConsensusServiceProxy GetLeaderConsensusProxy();

  // Get the given master's consensus proxy.
  consensus::ConsensusServiceProxy GetConsensusProxy(ExternalDaemon* daemon);

  template <class T>
  T GetProxy(const ExternalDaemon* daemon);

  template <class T>
  T GetTServerProxy(size_t i) {
    return GetProxy<T>(tablet_server(i));
  }

  template <class T>
  T GetMasterProxy() {
    CHECK_EQ(masters_.size(), 1);
    return GetMasterProxy<T>(0);
  }

  template <class T>
  T GetMasterProxy(size_t idx) {
    CHECK_LT(idx, masters_.size());
    return GetProxy<T>(master(idx));
  }

  template <class T>
  T GetLeaderMasterProxy() {
    return GetProxy<T>(GetLeaderMaster());
  }

  // Returns an generic proxy to the master at 'idx'. Requires that the master at 'idx' is running.
  std::shared_ptr<server::GenericServiceProxy> master_generic_proxy(int idx) const;
  // Same as above, using the bound rpc address.
  std::shared_ptr<server::GenericServiceProxy> master_generic_proxy(const HostPort& bound_addr)
      const;

  // Wait until the number of registered tablet servers reaches the given count on at least one of
  // the running masters.  Returns Status::TimedOut if the desired count is not achieved with the
  // given timeout.
  Status WaitForTabletServerCount(size_t count, const MonoDelta& timeout);

  // Runs gtest assertions that no servers have crashed.
  void AssertNoCrashes();

  // Wait until all tablets on the given tablet server are in 'RUNNING'
  // state.
  Status WaitForTabletsRunning(ExternalTabletServer* ts, const MonoDelta& timeout);

  Result<tserver::ListTabletsResponsePB> ListTablets(ExternalTabletServer* ts);

  Result<std::vector<tserver::ListTabletsForTabletServerResponsePB_Entry>> GetTablets(
      ExternalTabletServer* ts);

  Result<std::vector<TabletId>> GetTabletIds(ExternalTabletServer* ts);

  Result<size_t> GetSegmentCounts(ExternalTabletServer* ts);

  Result<tserver::GetTabletStatusResponsePB> GetTabletStatus(
      const ExternalTabletServer& ts, const yb::TabletId& tablet_id);

  Result<tserver::GetSplitKeyResponsePB> GetSplitKey(const yb::TabletId& tablet_id);
  Result<tserver::GetSplitKeyResponsePB> GetSplitKey(const ExternalTabletServer& ts,
      const yb::TabletId& tablet_id, bool fail_on_response_error = true);

  Status FlushTabletsOnSingleTServer(
      ExternalTabletServer* ts, const std::vector<yb::TabletId> tablet_ids,
      bool is_compaction);

  Status WaitForTSToCrash(const ExternalTabletServer* ts,
                          const MonoDelta& timeout = MonoDelta::FromSeconds(60));

  Status WaitForTSToCrash(
      size_t index, const MonoDelta& timeout = MonoDelta::FromSeconds(60));

  // Sets the given flag on the given daemon, which must be running.
  //
  // This uses the 'force' flag on the RPC so that, even if the flag is considered unsafe to change
  // at runtime, it is changed.
  Status SetFlag(ExternalDaemon* daemon,
                 const std::string& flag,
                 const std::string& value);

  // Sets the given flag on all masters.
  Status SetFlagOnMasters(const std::string& flag, const std::string& value);
  // Sets the given flag on all tablet servers.
  Status SetFlagOnTServers(const std::string& flag, const std::string& value);

  // Allocates a free port and stores a file lock guarding access to that port into an internal
  // array of file locks.
  uint16_t AllocateFreePort();

  // Step down the master leader. error_code tracks rpc error info that can be used by the caller.
  Status StepDownMasterLeader(
      tserver::TabletServerErrorPB::Code* error_code, const std::string& new_leader_uuid = "");

  // Step down the master leader and wait for a new leader to be elected.
  Status StepDownMasterLeaderAndWaitForNewLeader(const std::string& new_leader_uuid = "");

  // Find out if the master service considers itself ready. Return status OK() implies it is ready.
  Status GetIsMasterLeaderServiceReady(ExternalMaster* master);

  // Timeout to be used for rpc operations.
  MonoDelta timeout() {
    return opts_.timeout;
  }

  // Start a leader election on this master.
  Status StartElection(ExternalMaster* master);

  bool running() const { return running_; }

  std::string data_root() const { return data_root_; }

  // Return true if the tserver has been marked as DEAD by master leader.
  Result<bool> is_ts_stale(
      int ts_idx, MonoDelta deadline = MonoDelta::FromSeconds(120) * kTimeMultiplier);

  Status WaitForMasterToMarkTSAlive(
      int ts_idx, MonoDelta deadline = MonoDelta::FromSeconds(120) * kTimeMultiplier);

  Status WaitForMasterToMarkTSDead(
      int ts_idx, MonoDelta deadline = MonoDelta::FromSeconds(120) * kTimeMultiplier);

  // Return a pointer to the flags used for master.  Modifying these flags will only
  // take effect on new master creation.
  std::vector<std::string>* mutable_extra_master_flags() { return &opts_.extra_master_flags; }

  // Return a pointer to the flags used for tserver.  Modifying these flags will only
  // take effect on new tserver creation.
  std::vector<std::string>* mutable_extra_tserver_flags() { return &opts_.extra_tserver_flags; }

 protected:
  FRIEND_TEST(MasterFailoverTest, TestKillAnyMaster);

  void ConfigureClientBuilder(client::YBClientBuilder* builder) override;

  Result<HostPort> DoGetLeaderMasterBoundRpcAddr() override;

  Status StartMasters();

  std::string GetBinaryPath(const std::string& binary) const;
  std::string GetDataPath(const std::string& daemon_id) const;

  Status DeduceBinRoot(std::string* ret);
  Status HandleOptions();

  std::string GetClusterDataDirName() const;

  // Helper function to get a leader or (random) follower index
  Result<size_t> GetPeerMasterIndex(bool is_leader);

  // API to help update the cluster state (rpc ports)
  Status AddMaster(ExternalMaster* master);
  Status RemoveMaster(ExternalMaster* master);

  // Get the index of this master in the vector of masters. This might not be the insertion order as
  // we might have removed some masters within the vector.
  int GetIndexOfMaster(const ExternalMaster* master) const;

  // Checks that the masters_ list and opts_ match in terms of the number of elements.
  Status CheckPortAndMasterSizes() const;

  // Return the list of opid's for all master's in this cluster.
  Status GetLastOpIdForMasterPeers(
      const MonoDelta& timeout,
      consensus::OpIdType opid_type,
      std::vector<OpIdPB>* op_ids,
      const std::vector<ExternalMaster*>& masters);

  // Ensure that the leader server is allowed to process a config change (by having at least one
  // commit in the current term as leader).
  Status WaitForLeaderToAllowChangeConfig(
      const std::string& uuid,
      consensus::ConsensusServiceProxy* leader_proxy);

  // Return master address for specified port.
  std::string MasterAddressForPort(uint16_t port) const;

  ExternalMiniClusterOptions opts_;

  // The root for binaries.
  std::string daemon_bin_path_;

  std::string data_root_;

  // This variable is incremented every time a new master is spawned (either in shell mode or create
  // mode). Avoids reusing an index of a killed/removed master. Useful for master side logging.
  size_t add_new_master_at_;

  std::vector<scoped_refptr<ExternalMaster> > masters_;
  std::vector<scoped_refptr<ExternalTabletServer> > tablet_servers_;

  rpc::Messenger* messenger_ = nullptr;
  std::unique_ptr<rpc::Messenger> messenger_holder_;
  std::unique_ptr<rpc::ProxyCache> proxy_cache_;

  std::vector<std::unique_ptr<FileLock>> free_port_file_locks_;
  std::atomic<bool> running_{false};

 private:
  DISALLOW_COPY_AND_ASSIGN(ExternalMiniCluster);
};

YB_STRONGLY_TYPED_BOOL(SafeShutdown);

class ExternalDaemon : public RefCountedThreadSafe<ExternalDaemon> {
 public:
  class StringListener {
   public:
    virtual void Handle(const GStringPiece& s) = 0;
    virtual ~StringListener() {}
  };

  ExternalDaemon(
      std::string daemon_id,
      rpc::Messenger* messenger,
      rpc::ProxyCache* proxy_cache,
      const std::string& exe,
      const std::string& root_dir,
      const std::vector<std::string>& data_dirs,
      const std::vector<std::string>& extra_flags);

  HostPort bound_rpc_hostport() const;
  HostPort bound_rpc_addr() const;
  HostPort bound_http_hostport() const;
  const NodeInstancePB& instance_id() const;
  const std::string& uuid() const;

  // Return the pid of the running process.  Causes a CHECK failure if the process is not running.
  pid_t pid() const;

  const std::string& id() const { return daemon_id_; }

  // Sends a SIGSTOP signal to the daemon.
  Status Pause();

  // Sends a SIGCONT signal to the daemon.
  Status Resume();

  Status Kill(int signal);

  // Return true if we have explicitly shut down the process.
  bool IsShutdown() const;

  // Was SIGKILL used to shutdown the process?
  bool WasUnsafeShutdown() const;

  // Return true if the process is still running.  This may return false if the process crashed,
  // even if we didn't explicitly call Shutdown().
  bool IsProcessAlive() const;

  bool IsProcessPaused() const;

  virtual void Shutdown(SafeShutdown safe_shutdown = SafeShutdown::kFalse);

  std::vector<std::string> GetDataDirs() const { return data_dirs_; }

  const std::string& exe() const { return exe_; }

  const std::string& GetRootDir() const { return root_dir_; }

  // Return a pointer to the flags used for this server on restart.  Modifying these flags will only
  // take effect on the next restart.
  std::vector<std::string>* mutable_flags() { return &extra_flags_; }

  // Retrieve the value of a given type metric from this server.
  //
  // 'value_field' represents the particular field of the metric to be read.  For example, for a
  // counter or gauge, this should be 'value'. For a histogram, it might be 'total_count' or 'mean'.
  //
  // 'entity_id' may be NULL, in which case the first entity of the same type as 'entity_proto' will
  // be matched.

  template <class ValueType>
  Result<ValueType> GetMetric(const MetricEntityPrototype* entity_proto,
                              const char* entity_id,
                              const MetricPrototype* metric_proto,
                              const char* value_field) const {
    return GetMetricFromHost<ValueType>(
        bound_http_hostport(), entity_proto, entity_id, metric_proto, value_field);
  }

  template <class ValueType>
  Result<ValueType> GetMetric(const char* entity_proto_name,
                              const char* entity_id,
                              const char* metric_proto_name,
                              const char* value_field) const {
    return GetMetricFromHost<ValueType>(
        bound_http_hostport(), entity_proto_name, entity_id, metric_proto_name, value_field);
  }

  std::string LogPrefix();

  void SetLogListener(StringListener* listener);

  void RemoveLogListener(StringListener* listener);

  template <class ValueType>
  static Result<ValueType> GetMetricFromHost(const HostPort& hostport,
                                             const MetricEntityPrototype* entity_proto,
                                             const char* entity_id,
                                             const MetricPrototype* metric_proto,
                                             const char* value_field) {
    return GetMetricFromHost<ValueType>(hostport, entity_proto->name(), entity_id,
                                        metric_proto->name(), value_field);
  }

  template <class ValueType>
  static Result<ValueType> GetMetricFromHost(const HostPort& hostport,
                                             const char* entity_proto_name,
                                             const char* entity_id,
                                             const char* metric_proto_name,
                                             const char* value_field) {
    // Fetch metrics whose name matches the given prototype.
    std::string url = Substitute(
        "http://$0/jsonmetricz?metrics=$1",
        hostport.ToString(),
        metric_proto_name);
    EasyCurl curl;
    faststring dst;
    RETURN_NOT_OK(curl.FetchURL(url, &dst));

    // Parse the results, beginning with the top-level entity array.
    JsonReader r(dst.ToString());
    RETURN_NOT_OK(r.Init());
    std::vector<const Value*> entities;
    RETURN_NOT_OK(r.ExtractObjectArray(r.root(), NULL, &entities));
    for (const Value* entity : entities) {
      // Find the desired entity.
      std::string type;
      RETURN_NOT_OK(r.ExtractString(entity, "type", &type));
      if (type != entity_proto_name) {
        continue;
      }
      if (entity_id) {
        std::string id;
        RETURN_NOT_OK(r.ExtractString(entity, "id", &id));
        if (id != entity_id) {
          continue;
        }
      }

      // Find the desired metric within the entity.
      std::vector<const Value*> metrics;
      RETURN_NOT_OK(r.ExtractObjectArray(entity, "metrics", &metrics));
      for (const Value* metric : metrics) {
        std::string name;
        RETURN_NOT_OK(r.ExtractString(metric, "name", &name));
        if (name != metric_proto_name) {
          continue;
        }
        return ExtractMetricValue<ValueType>(r, metric, value_field);
      }
    }
    std::string msg;
    if (entity_id) {
      msg = Substitute("Could not find metric $0.$1 for entity $2",
                       entity_proto_name, metric_proto_name,
                       entity_id);
    } else {
      msg = Substitute("Could not find metric $0.$1",
                       entity_proto_name, metric_proto_name);
    }
    return STATUS(NotFound, msg);
  }

  template <class ValueType>
  static Result<ValueType> ExtractMetricValue(const JsonReader& r,
                                              const Value* object,
                                              const char* field);

  // Get the current value of the flag for the given daemon.
  Result<std::string> GetFlag(const std::string& flag);

 protected:
  friend class RefCountedThreadSafe<ExternalDaemon>;
  virtual ~ExternalDaemon();

  Status StartProcess(const std::vector<std::string>& flags);

  virtual Status DeleteServerInfoPaths();


  virtual bool ServerInfoPathsExist();

  virtual Status BuildServerStateFromInfoPath();

  Status BuildServerStateFromInfoPath(
      const std::string& info_path, std::unique_ptr<server::ServerStatusPB>* server_status);

  std::string GetServerInfoPath();

  // In a code-coverage build, try to flush the coverage data to disk.
  // In a non-coverage build, this does nothing.
  void FlushCoverage();

  std::string ProcessNameAndPidStr();

  const std::string daemon_id_;
  rpc::Messenger* messenger_;
  rpc::ProxyCache* proxy_cache_;
  const std::string exe_;
  const std::string root_dir_;
  std::vector<std::string> data_dirs_;
  std::vector<std::string> extra_flags_;

  std::unique_ptr<Subprocess> process_;
  bool is_paused_ = false;
  bool sigkill_used_for_shutdown_ = false;

  std::unique_ptr<server::ServerStatusPB> status_;

  // These capture the daemons parameters and running ports and
  // are used to Restart() the daemon with the same parameters.
  HostPort bound_rpc_;
  HostPort bound_http_;

 private:
  class LogTailerThread;

  std::unique_ptr<LogTailerThread> stdout_tailer_thread_, stderr_tailer_thread_;

  DISALLOW_COPY_AND_ASSIGN(ExternalDaemon);
};

// Utility class for waiting for logging events.
class LogWaiter : public ExternalDaemon::StringListener {
 public:
  LogWaiter(ExternalDaemon* daemon, const std::string& string_to_wait);

  Status WaitFor(MonoDelta timeout);
  bool IsEventOccurred() { return event_occurred_; }

  ~LogWaiter();

 private:
  void Handle(const GStringPiece& s) override;

  ExternalDaemon* daemon_;
  std::atomic<bool> event_occurred_{false};
  std::string string_to_wait_;
};

// Resumes a daemon that was stopped with ExteranlDaemon::Pause() upon
// exiting a scope.
class ScopedResumeExternalDaemon {
 public:
  // 'daemon' must remain valid for the lifetime of a
  // ScopedResumeExternalDaemon object.
  explicit ScopedResumeExternalDaemon(ExternalDaemon* daemon);

  // Resume 'daemon_'.
  ~ScopedResumeExternalDaemon();

 private:
  ExternalDaemon* daemon_;

  DISALLOW_COPY_AND_ASSIGN(ScopedResumeExternalDaemon);
};


class ExternalMaster : public ExternalDaemon {
 public:
  ExternalMaster(
    size_t master_index,
    rpc::Messenger* messenger,
    rpc::ProxyCache* proxy_cache,
    const std::string& exe,
    const std::string& data_dir,
    const std::vector<std::string>& extra_flags,
    const std::string& rpc_bind_address = "127.0.0.1:0",
    uint16_t http_port = 0,
    const std::string& master_addrs = "");

  Status Start(bool shell_mode = false);

  // Restarts the daemon. Requires that it has previously been shutdown.
  Status Restart();

 private:
  friend class RefCountedThreadSafe<ExternalMaster>;
  virtual ~ExternalMaster();

  // used on start to create the cluster; on restart, this should not be used!
  const std::string rpc_bind_address_;
  const std::string master_addrs_;
  const uint16_t http_port_;
};

class ExternalTabletServer : public ExternalDaemon {
 public:
  ExternalTabletServer(
      size_t tablet_server_index, rpc::Messenger* messenger, rpc::ProxyCache* proxy_cache,
      const std::string& exe, const std::string& data_dir, uint16_t num_drives,
      std::string bind_host, uint16_t rpc_port, uint16_t http_port, uint16_t redis_rpc_port,
      uint16_t redis_http_port, uint16_t cql_rpc_port, uint16_t cql_http_port,
      uint16_t pgsql_rpc_port, uint16_t pgsql_http_port,
      const std::vector<HostPort>& master_addrs,
      const std::vector<std::string>& extra_flags);

  Status Start(
      bool start_cql_proxy = ExternalMiniClusterOptions::kDefaultStartCqlProxy,
      bool set_proxy_addrs = true,
      std::vector<std::pair<std::string, std::string>> extra_flags = {});

  void UpdateMasterAddress(const std::vector<HostPort>& master_addrs);

  // Restarts the daemon. Requires that it has previously been shutdown.
  // TODO: #14902 Shutdown the process if it is running.
  Status Restart(
      bool start_cql_proxy = ExternalMiniClusterOptions::kDefaultStartCqlProxy,
      std::vector<std::pair<std::string, std::string>> flags = {});

  Status SetNumDrives(uint16_t num_drives);

  // IP addresses to bind to.
  const std::string& bind_host() const {
    return bind_host_;
  }

  // Assigned ports.
  uint16_t rpc_port() const {
    return rpc_port_;
  }
  uint16_t http_port() const {
    return http_port_;
  }

  uint16_t pgsql_rpc_port() const {
    return pgsql_rpc_port_;
  }
  uint16_t pgsql_http_port() const {
    return pgsql_http_port_;
  }

  uint16_t redis_rpc_port() const {
    return redis_rpc_port_;
  }
  uint16_t redis_http_port() const {
    return redis_http_port_;
  }

  uint16_t cql_rpc_port() const {
    return cql_rpc_port_;
  }
  uint16_t cql_http_port() const {
    return cql_http_port_;
  }

  Result<int64_t> GetInt64CQLMetric(const MetricEntityPrototype* entity_proto,
                                    const char* entity_id,
                                    const MetricPrototype* metric_proto,
                                    const char* value_field) const;

 protected:
  Status DeleteServerInfoPaths() override;

  bool ServerInfoPathsExist() override;

  Status BuildServerStateFromInfoPath() override;

 private:
  std::string GetCQLServerInfoPath();
  std::string master_addrs_;
  const std::string bind_host_;
  const uint16_t rpc_port_;
  const uint16_t http_port_;
  const uint16_t redis_rpc_port_;
  const uint16_t redis_http_port_;
  const uint16_t pgsql_rpc_port_;
  const uint16_t pgsql_http_port_;
  const uint16_t cql_rpc_port_;
  const uint16_t cql_http_port_;
  uint16_t num_drives_;
  bool start_cql_proxy_ = true;
  std::unique_ptr<server::ServerStatusPB> cqlserver_status_;

  friend class RefCountedThreadSafe<ExternalTabletServer>;
  virtual ~ExternalTabletServer();
};

// Custom functor for predicate based comparison with the master list.
struct MasterComparator {
  explicit MasterComparator(ExternalMaster* master) : master_(master) { }

  // We look for the exact master match. Since it is possible to stop/restart master on a given
  // host/port, we do not want a stale master pointer input to match a newer master.
  bool operator()(const scoped_refptr<ExternalMaster>& other) const {
    return master_ == other.get();
  }

 private:
  const ExternalMaster* master_;
};

template <class T>
T ExternalMiniCluster::GetProxy(const ExternalDaemon* daemon) {
  return T(proxy_cache_.get(), daemon->bound_rpc_addr());
}

Status RestartAllMasters(ExternalMiniCluster* cluster);

Status CompactTablets(
    ExternalMiniCluster* cluster,
    const MonoDelta& timeout = MonoDelta::FromSeconds(60* kTimeMultiplier));

Status FlushAndCompactSysCatalog(ExternalMiniCluster* cluster, const MonoDelta& timeout);

Status CompactSysCatalog(ExternalMiniCluster* cluster, const MonoDelta& timeout);

void StartSecure(
  std::unique_ptr<ExternalMiniCluster>* cluster,
  std::unique_ptr<rpc::SecureContext>* secure_context,
  std::unique_ptr<rpc::Messenger>* messenger,
  const std::vector<std::string>& master_flags = std::vector<std::string>());

Status WaitForTableIntentsApplied(
    ExternalMiniCluster* cluster, const TableId& table_id,
    MonoDelta timeout = MonoDelta::FromSeconds(30));

}  // namespace yb
