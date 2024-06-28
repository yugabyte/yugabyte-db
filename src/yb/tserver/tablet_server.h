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

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "yb/common/common_util.h"
#include "yb/common/pg_catversions.h"
#include "yb/consensus/metadata.pb.h"
#include "yb/cdc/xrepl_types.h"
#include "yb/cdc/cdc_consumer.fwd.h"
#include "yb/client/client_fwd.h"
#include "yb/rpc/rpc_fwd.h"

#include "yb/encryption/encryption_fwd.h"

#include "yb/gutil/atomicops.h"
#include "yb/gutil/macros.h"
#include "yb/master/master_fwd.h"
#include "yb/master/master_heartbeat.pb.h"
#include "yb/server/webserver_options.h"
#include "yb/tserver/db_server_base.h"
#include "yb/tserver/pg_client_service.h"
#include "yb/tserver/pg_mutation_counter.h"
#include "yb/tserver/remote_bootstrap_service.h"
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

namespace cdc {

class CDCServiceImpl;

}

namespace cdc {

class CDCServiceImpl;

}

namespace stateful_service {
class PgCronLeaderService;
}  // namespace stateful_service

namespace tserver {

class TserverAutoFlagsManager;
class TserverXClusterContext;
class TserverXClusterContextIf;
class PgClientServiceImpl;
class XClusterConsumerIf;

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

  virtual Status InitAutoFlags(rpc::Messenger* messenger) override;

  virtual bool ShouldExportLocalCalls() override {
    return true;
  }

  Status GetRegistration(ServerRegistrationPB* reg,
    server::RpcOnly rpc_only = server::RpcOnly::kFalse) const override;

  // Waits for the tablet server to complete the initialization.
  Status WaitInited();

  Status Start() override;
  void Shutdown() override;

  std::string ToString() const override;

  uint32_t GetAutoFlagConfigVersion() const override;
  void HandleMasterHeartbeatResponse(
      HybridTime heartbeat_sent_time, std::optional<AutoFlagsConfigPB> new_config);

  Result<uint32> ValidateAndGetAutoFlagsConfigVersion() const;

  AutoFlagsConfigPB TEST_GetAutoFlagConfig() const;

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

  Status PopulateLiveTServers(const master::TSHeartbeatResponsePB& heartbeat_resp) EXCLUDES(lock_);

  Status GetLiveTServers(
      std::vector<master::TSInformationPB> *live_tservers) const EXCLUDES(lock_) override;

  // Returns connection info of all live tservers available at this tserver. The information about
  // live tservers is refreshed by the master heartbeat.
  virtual Result<std::vector<client::internal::RemoteTabletServerPtr>>
      GetRemoteTabletServers() const EXCLUDES(lock_) override;

  // Returns connection info for the passed in 'ts_uuids', if available. If unavailable,
  // returns a bad status. The information about live tservers is refreshed by the master heartbeat.
  virtual Result<std::vector<client::internal::RemoteTabletServerPtr>> GetRemoteTabletServers(
      const std::unordered_set<std::string>& ts_uuids) const EXCLUDES(lock_) override;

  Status GetTabletStatus(const GetTabletStatusRequestPB* req,
                         GetTabletStatusResponsePB* resp) const override;

  bool LeaderAndReady(const TabletId& tablet_id, bool allow_stale = false) const override;

  const std::string& permanent_uuid() const override { return fs_manager_->uuid(); }

  bool has_faulty_drive() const { return fs_manager_->has_faulty_drive(); }

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

  void SetYsqlCatalogVersion(uint64_t new_version, uint64_t new_breaking_version);
  void SetYsqlDBCatalogVersions(const master::DBCatalogVersionDataPB& db_catalog_version_data);

  void get_ysql_catalog_version(uint64_t* current_version,
                                uint64_t* last_breaking_version) const override {
    std::lock_guard l(lock_);
    if (current_version) {
      *current_version = ysql_catalog_version_;
    }
    if (last_breaking_version) {
      *last_breaking_version = ysql_last_breaking_catalog_version_;
    }
  }

  void get_ysql_db_catalog_version(uint32_t db_oid,
                                   uint64_t* current_version,
                                   uint64_t* last_breaking_version) const override {
    std::lock_guard l(lock_);
    auto it = ysql_db_catalog_version_map_.find(db_oid);
    bool not_found = it == ysql_db_catalog_version_map_.end();
    // If db_oid represents a newly created database, it may not yet exist in
    // ysql_db_catalog_version_map_ because the latter is updated via tserver to master
    // heartbeat response which has a delay. Return 0 as if it were a stale version.
    // Note that even if db_oid is found in ysql_db_catalog_version_map_ the catalog version
    // can also be stale due to the heartbeat delay.
    if (current_version) {
      *current_version = not_found ? 0UL : it->second.current_version;
    }
    if (last_breaking_version) {
      *last_breaking_version = not_found ? 0UL : it->second.last_breaking_version;
    }
  }

  std::optional<bool> catalog_version_table_in_perdb_mode() const {
    std::lock_guard l(lock_);
    return catalog_version_table_in_perdb_mode_;
  }

  Status get_ysql_db_oid_to_cat_version_info_map(
      const tserver::GetTserverCatalogVersionInfoRequestPB& req,
      tserver::GetTserverCatalogVersionInfoResponsePB* resp) const override;

  void UpdateTransactionTablesVersion(uint64_t new_version);

  rpc::Messenger* GetMessenger(ash::Component component) const override;

  void SetCQLServer(yb::server::RpcAndWebServerBase* server,
      server::YCQLStatementStatsProvider* stmt_provider) override;

  virtual Env* GetEnv();

  virtual rocksdb::Env* GetRocksDBEnv();

  virtual Status SetUniverseKeyRegistry(
      const encryption::UniverseKeyRegistryPB& universe_key_registry);

  uint64_t GetSharedMemoryPostgresAuthKey();

  SchemaVersion GetMinXClusterSchemaVersion(const TableId& table_id,
      const ColocationId& colocation_id) const;

  // Currently only used by cdc.
  virtual int32_t cluster_config_version() const;

  Result<uint32_t> XClusterConfigVersion() const;

  Status SetPausedXClusterProducerStreams(
      const ::google::protobuf::Map<::std::string, bool>& paused_producer_stream_ids,
      uint32_t xcluster_config_version);

  client::TransactionPool& TransactionPool() override;

  const std::shared_future<client::YBClient*>& client_future() const override;

  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  const HostPort& pgsql_proxy_bind_address() const { return pgsql_proxy_bind_address_; }

  client::LocalTabletFilter CreateLocalTabletFilter() override;

  void RegisterCertificateReloader(CertificateReloader reloader) override;

  const TserverXClusterContextIf& GetXClusterContext() const;

  PgMutationCounter& GetPgNodeLevelMutationCounter();

  Status ListMasterServers(const ListMasterServersRequestPB* req,
                           ListMasterServersResponsePB* resp) const;

  encryption::UniverseKeyManager* GetUniverseKeyManager();

  Status XClusterHandleMasterHeartbeatResponse(const master::TSHeartbeatResponsePB& resp);

  Status ValidateAndMaybeSetUniverseUuid(const UniverseUuid& universe_uuid);

  Status ClearUniverseUuid();

  XClusterConsumerIf* GetXClusterConsumer() const;

  // Mark the CDC service as enabled via heartbeat.
  Status SetCDCServiceEnabled();

  Status ReloadKeysAndCertificates() override;
  std::string GetCertificateDetails() override;

  PgClientServiceImpl* TEST_GetPgClientService() {
    auto holder = pg_client_service_.lock();
    return holder ? &holder->impl : nullptr;
  }

  PgClientServiceMockImpl* TEST_GetPgClientServiceMock() {
    auto holder = pg_client_service_.lock();
    return holder && holder->mock.has_value() ? &holder->mock.value() : nullptr;
  }

  RemoteBootstrapServiceImpl* GetRemoteBootstrapService() {
    if (auto service_ptr = remote_bootstrap_service_.lock()) {
      return service_ptr.get();
    }
    return nullptr;
  }

  std::optional<uint64_t> GetCatalogVersionsFingerprint() const {
    return catalog_versions_fingerprint_.load(std::memory_order_acquire);
  }

  std::shared_ptr<cdc::CDCServiceImpl> GetCDCService() const override { return cdc_service_; }

  key_t GetYsqlConnMgrStatsShmemKey() { return ysql_conn_mgr_stats_shmem_key_; }
  void SetYsqlConnMgrStatsShmemKey(key_t shmem_key) { ysql_conn_mgr_stats_shmem_key_ = shmem_key; }
  Status YCQLStatementStats(const tserver::PgYCQLStatementStatsRequestPB& req,
      tserver::PgYCQLStatementStatsResponsePB* resp) const override;

  void WriteServerMetaCacheAsJson(JsonWriter* writer) override;

  void ClearAllMetaCachesOnServer() override;

  Result<std::vector<tablet::TabletStatusPB>> GetLocalTabletsMetadata() const override;

  void TEST_SetIsCronLeader(bool is_cron_leader);

  struct PgClientServiceHolder {
    template<class... Args>
    explicit PgClientServiceHolder(Args&&... args) : impl(std::forward<Args>(args)...) {}

    PgClientServiceImpl impl;
    std::optional<PgClientServiceMockImpl> mock;
  };

 protected:
  virtual Status RegisterServices();

  friend class TabletServerTestBase;

  Status DisplayRpcIcons(std::stringstream* output) override;

  Status ValidateMasterAddressResolution() const;

  MonoDelta default_client_timeout() override;
  void SetupAsyncClientInit(client::AsyncClientInitializer* async_client_init) override;

  Status SetupMessengerBuilder(rpc::MessengerBuilder* builder) override;

  Result<std::unordered_set<std::string>> GetAvailableAutoFlagsForServer() const override;

  void SetCronLeaderLease(MonoTime cron_leader_lease_end);

  std::atomic<bool> initted_{false};

  // If true, all heartbeats will be seen as failed.
  Atomic32 fail_heartbeats_for_tests_;

  // The options passed at construction time, and will be updated if master config changes.
  TabletServerOptions opts_;

  std::unique_ptr<TserverAutoFlagsManager> auto_flags_manager_;

  // Manager for tablets which are available on this server.
  std::unique_ptr<TSTabletManager> tablet_manager_;

  // Used to forward redis pub/sub messages to the redis pub/sub handler
  yb::AtomicUniquePtr<rpc::Publisher> publish_service_ptr_;

  // Thread responsible for heartbeating to the master.
  std::unique_ptr<Heartbeater> heartbeater_;

  std::unique_ptr<client::UniverseKeyClient> universe_key_client_;

  // Thread responsible for collecting metrics snapshots for native storage.
  std::unique_ptr<MetricsSnapshotter> metrics_snapshotter_;

  // Thread responsible for sending aggregated table mutations to the auto analyzer service
  std::unique_ptr<TableMutationCountSender> pg_table_mutation_count_sender_;

  // Webserver path handlers
  std::unique_ptr<TabletServerPathHandlers> path_handlers_;

  // The maintenance manager for this tablet server
  std::shared_ptr<MaintenanceManager> maintenance_manager_;

  // Index at which master sent us the last config
  int64_t master_config_index_;

  // Map of tserver connection info, keyed by the tserver uuid, that are alive from the master's
  // perspective. It is refreshed on every successfull heartbeat exchange between the current
  // tserver and the master leader.
  std::unordered_map<std::string, master::TSInformationPB> live_tservers_ GUARDED_BY(lock_);

  // Map of 'RemoteTablerServer' shared ptrs keyed by the corresponding tsever uuid. This is used
  // to obtain a proxy handle to the local/remote tserver. It is updated synchronously with
  // 'live_tservers_'.
  std::unordered_map<std::string, client::internal::RemoteTabletServerPtr>
      remote_tservers_ GUARDED_BY(lock_);

  // Lock to protect live_tservers_, cluster_uuid_.
  mutable rw_spinlock lock_;

  // Proxy to call this tablet server locally.
  std::shared_ptr<TabletServerServiceProxy> proxy_;

  // Cluster uuid. This is sent by the master leader during the first heartbeat.
  std::string cluster_uuid_;

  // Latest known version from the YSQL catalog (as reported by last heartbeat response).
  uint64_t ysql_catalog_version_ = 0;
  uint64_t ysql_last_breaking_catalog_version_ = 0;
  tserver::DbOidToCatalogVersionInfoMap ysql_db_catalog_version_map_;
  // See same variable comments in CatalogManager.
  std::optional<bool> catalog_version_table_in_perdb_mode_{std::nullopt};

  // Fingerprint of the catalog versions map.
  std::atomic<std::optional<uint64_t>> catalog_versions_fingerprint_;

  // If shared memory array db_catalog_versions_ slot is used by a database OID, the
  // corresponding slot in this boolean array is set to true.
  std::unique_ptr<std::array<bool, TServerSharedData::kMaxNumDbCatalogVersions>>
    ysql_db_catalog_version_index_used_;

  // When searching for a free slot in the shared memory array db_catalog_versions_, we start
  // from this index.
  int search_starting_index_ = 0;

  // An instance to tablet server service. This pointer is no longer valid after RpcAndWebServerBase
  // is shut down.
  std::weak_ptr<TabletServiceImpl> tablet_server_service_;

  // An instance to remote bootstrap service. This pointer is no longer valid after
  // RpcAndWebServerBase is shut down.
  std::weak_ptr<RemoteBootstrapServiceImpl> remote_bootstrap_service_;

  // An instance to pg client service. This pointer is no longer valid after RpcAndWebServerBase
  // is shut down.
  std::weak_ptr<PgClientServiceHolder> pg_client_service_;

  // Key to shared memory for ysql connection manager stats
  key_t ysql_conn_mgr_stats_shmem_key_ = 0;

 private:
  // Auto initialize some of the service flags that are defaulted to -1.
  void AutoInitServiceFlags();

  void InvalidatePgTableCache();
  void InvalidatePgTableCache(const std::unordered_set<uint32_t>& db_oids_updated,
                              const std::unordered_set<uint32_t>& db_oids_deleted);

  std::string log_prefix_;

  // Bind address of postgres proxy under this tserver.
  HostPort pgsql_proxy_bind_address_;

  std::unique_ptr<TserverXClusterContext> xcluster_context_;

  PgMutationCounter pg_node_level_mutation_counter_;

  PgConfigReloader pg_config_reloader_;

  Status CreateXClusterConsumer() EXCLUDES(xcluster_consumer_mutex_);

  std::unique_ptr<rpc::SecureContext> secure_context_;
  std::vector<CertificateReloader> certificate_reloaders_;

  // xCluster consumer.
  mutable std::mutex xcluster_consumer_mutex_;
  std::unique_ptr<XClusterConsumerIf> xcluster_consumer_ GUARDED_BY(xcluster_consumer_mutex_);

  // CDC service.
  std::shared_ptr<cdc::CDCServiceImpl> cdc_service_;

  std::unique_ptr<rocksdb::Env> rocksdb_env_;
  std::unique_ptr<encryption::UniverseKeyManager> universe_key_manager_;

  std::atomic<yb::server::RpcAndWebServerBase*> cql_server_{nullptr};
  std::atomic<yb::server::YCQLStatementStatsProvider*> cql_stmt_provider_{nullptr};

  std::unique_ptr<stateful_service::PgCronLeaderService> pg_cron_leader_service_;

  DISALLOW_COPY_AND_ASSIGN(TabletServer);
};

} // namespace tserver
} // namespace yb
