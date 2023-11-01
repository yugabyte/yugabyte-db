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
#ifndef YB_TSERVER_TABLET_SERVER_H_
#define YB_TSERVER_TABLET_SERVER_H_

#include <future>
#include <memory>
#include <string>
#include <vector>

#include "yb/consensus/metadata.pb.h"
#include "yb/cdc/cdc_consumer.fwd.h"
#include "yb/client/client_fwd.h"

#include "yb/encryption/encryption_fwd.h"

#include "yb/gutil/atomicops.h"
#include "yb/gutil/macros.h"
#include "yb/master/master_fwd.h"
#include "yb/server/webserver_options.h"
#include "yb/tserver/db_server_base.h"
#include "yb/tserver/tserver_shared_mem.h"
#include "yb/tserver/tablet_server_interface.h"
#include "yb/tserver/tablet_server_options.h"

#include "yb/util/locks.h"
#include "yb/util/net/net_util.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/status_fwd.h"

namespace rocksdb {
class Env;
}

namespace yb {

class Env;
class MaintenanceManager;

namespace tserver {

class PgClientServiceImpl;

class TabletServer : public DbServerBase, public TabletServerIf {
 public:
  // TODO: move this out of this header, since clients want to use this
  // constant as well.
  static const uint16_t kDefaultPort = 9100;
  static const uint16_t kDefaultWebPort = 9000;

  // Default tserver and consensus RPC queue length per service.
  static constexpr uint32_t kDefaultSvcQueueLength = 5000;

  explicit TabletServer(const TabletServerOptions& opts);
  ~TabletServer();

  // Initializes the tablet server, including the bootstrapping of all
  // existing tablets.
  // Some initialization tasks are asynchronous, such as the bootstrapping
  // of tablets. Caller can block, waiting for the initialization to fully
  // complete by calling WaitInited().
  Status Init() override;

  Status GetRegistration(ServerRegistrationPB* reg,
    server::RpcOnly rpc_only = server::RpcOnly::kFalse) const override;

  // Waits for the tablet server to complete the initialization.
  Status WaitInited();

  Status Start() override;
  void Shutdown() override;

  std::string ToString() const override;

  TSTabletManager* tablet_manager() override { return tablet_manager_.get(); }
  TabletPeerLookupIf* tablet_peer_lookup() override;

  Heartbeater* heartbeater() { return heartbeater_.get(); }

  MetricsSnapshotter* metrics_snapshotter() { return metrics_snapshotter_.get(); }

  void set_fail_heartbeats_for_tests(bool fail_heartbeats_for_tests) {
    base::subtle::NoBarrier_Store(&fail_heartbeats_for_tests_, fail_heartbeats_for_tests);
  }

  bool fail_heartbeats_for_tests() const {
    return base::subtle::NoBarrier_Load(&fail_heartbeats_for_tests_);
  }

  MaintenanceManager* maintenance_manager() {
    return maintenance_manager_.get();
  }

  int64_t GetCurrentMasterIndex() { return master_config_index_; }

  void SetCurrentMasterIndex(int64_t index) { master_config_index_ = index; }

  // Update in-memory list of master addresses that this tablet server pings to.
  // If the update is from master leader, we use that list directly. If not, we
  // merge the existing in-memory master list with the provided config list.
  Status UpdateMasterAddresses(const consensus::RaftConfigPB& new_config,
                                       bool is_master_leader);

  server::Clock* Clock() override { return clock(); }

  void SetClockForTests(server::ClockPtr clock) { clock_ = std::move(clock); }

  const scoped_refptr<MetricEntity>& MetricEnt() const override { return metric_entity(); }

  tserver::TServerSharedData& SharedObject() override {
    return shared_object();
  }

  Status PopulateLiveTServers(const master::TSHeartbeatResponsePB& heartbeat_resp);

  Status GetLiveTServers(
      std::vector<master::TSInformationPB> *live_tservers) const override;

  Status GetTabletStatus(const GetTabletStatusRequestPB* req,
                                 GetTabletStatusResponsePB* resp) const override;

  bool LeaderAndReady(const TabletId& tablet_id, bool allow_stale = false) const override;

  const std::string& permanent_uuid() const override { return fs_manager_->uuid(); }

  // Returns the proxy to call this tablet server locally.
  const std::shared_ptr<TabletServerServiceProxy>& proxy() const { return proxy_; }

  const TabletServerOptions& options() const { return opts_; }

  void set_cluster_uuid(const std::string& cluster_uuid);

  std::string cluster_uuid() const;

  scoped_refptr<Histogram> GetMetricsHistogram(TabletServerServiceRpcMethodIndexes metric);

  const std::shared_ptr<MemTracker>& mem_tracker() const override;

  void SetPublisher(rpc::Publisher service) override;

  rpc::Publisher* GetPublisher() override {
    return publish_service_ptr_.get();
  }

  void SetYSQLCatalogVersion(uint64_t new_version, uint64_t new_breaking_version);

  void get_ysql_catalog_version(uint64_t* current_version,
                                uint64_t* last_breaking_version) const override {
    std::lock_guard<simple_spinlock> l(lock_);
    if (current_version) {
      *current_version = ysql_catalog_version_;
    }
    if (last_breaking_version) {
      *last_breaking_version = ysql_last_breaking_catalog_version_;
    }
  }

  void UpdateTransactionTablesVersion(uint64_t new_version);

  virtual Env* GetEnv();

  virtual rocksdb::Env* GetRocksDBEnv();

  void SetUniverseKeys(const encryption::UniverseKeysPB& universe_keys);

  virtual Status SetUniverseKeyRegistry(
      const encryption::UniverseKeyRegistryPB& universe_key_registry);

  void GetUniverseKeyRegistrySync();

  uint64_t GetSharedMemoryPostgresAuthKey();

  // Currently only used by cdc.
  virtual int32_t cluster_config_version() const {
    return std::numeric_limits<int32_t>::max();
  }

  client::TransactionPool& TransactionPool() override;

  const std::shared_future<client::YBClient*>& client_future() const override;

  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  const HostPort& pgsql_proxy_bind_address() const { return pgsql_proxy_bind_address_; }

  client::LocalTabletFilter CreateLocalTabletFilter() override;

  void RegisterCertificateReloader(CertificateReloader reloader) override {}

 protected:
  virtual Status RegisterServices();

  friend class TabletServerTestBase;

  Status DisplayRpcIcons(std::stringstream* output) override;

  Status ValidateMasterAddressResolution() const;

  MonoDelta default_client_timeout() override;
  void SetupAsyncClientInit(client::AsyncClientInitialiser* async_client_init) override;

  std::atomic<bool> initted_{false};

  // If true, all heartbeats will be seen as failed.
  Atomic32 fail_heartbeats_for_tests_;

  // The options passed at construction time, and will be updated if master config changes.
  TabletServerOptions opts_;

  // Manager for tablets which are available on this server.
  std::unique_ptr<TSTabletManager> tablet_manager_;

  // Used to forward redis pub/sub messages to the redis pub/sub handler
  yb::AtomicUniquePtr<rpc::Publisher> publish_service_ptr_;

  // Thread responsible for heartbeating to the master.
  std::unique_ptr<Heartbeater> heartbeater_;

  std::unique_ptr<client::UniverseKeyClient> universe_key_client_;

  // Thread responsible for collecting metrics snapshots for native storage.
  std::unique_ptr<MetricsSnapshotter> metrics_snapshotter_;

  // Webserver path handlers
  std::unique_ptr<TabletServerPathHandlers> path_handlers_;

  // The maintenance manager for this tablet server
  std::shared_ptr<MaintenanceManager> maintenance_manager_;

  // Index at which master sent us the last config
  int64_t master_config_index_;

  // List of tservers that are alive from the master's perspective.
  std::vector<master::TSInformationPB> live_tservers_;

  // Lock to protect live_tservers_, cluster_uuid_.
  mutable simple_spinlock lock_;

  // Proxy to call this tablet server locally.
  std::shared_ptr<TabletServerServiceProxy> proxy_;

  // Cluster uuid. This is sent by the master leader during the first heartbeat.
  std::string cluster_uuid_;

  // Latest known version from the YSQL catalog (as reported by last heartbeat response).
  uint64_t ysql_catalog_version_ = 0;
  uint64_t ysql_last_breaking_catalog_version_ = 0;

  // An instance to tablet server service. This pointer is no longer valid after RpcAndWebServerBase
  // is shut down.
  std::weak_ptr<TabletServiceImpl> tablet_server_service_;

  // An instance to pg client service. This pointer is no longer valid after RpcAndWebServerBase
  // is shut down.
  std::weak_ptr<PgClientServiceImpl> pg_client_service_;

 private:
  // Auto initialize some of the service flags that are defaulted to -1.
  void AutoInitServiceFlags();

  std::string log_prefix_;

  // Bind address of postgres proxy under this tserver.
  HostPort pgsql_proxy_bind_address_;

  DISALLOW_COPY_AND_ASSIGN(TabletServer);
};

} // namespace tserver
} // namespace yb
#endif // YB_TSERVER_TABLET_SERVER_H_
