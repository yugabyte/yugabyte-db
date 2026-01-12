// Copyright (c) YugaByte, Inc.
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

#include <future>

#include "yb/tserver/pg_txn_snapshot_manager.h"
#include "yb/tserver/tablet_peer_lookup.h"
#include "yb/tserver/tablet_server_interface.h"
#include "yb/tserver/ts_local_lock_manager.h"
#include "yb/tserver/tserver_fwd.h"

#include "yb/util/metrics_fwd.h"

namespace yb::master {

class Master;

// Master's version of a TabletServer which is required to support virtual tables in the Master.
// This isn't really an actual server and is just a nice way of overriding the default tablet
// server interface to support virtual tables.
class MasterTabletServer : public tserver::TabletServerIf,
                           public tserver::TabletPeerLookupIf {
 public:
  MasterTabletServer(Master* master, scoped_refptr<MetricEntity> metric_entity);
  tserver::TSTabletManager* tablet_manager() const override;
  tserver::TabletPeerLookupIf* tablet_peer_lookup() override;
  tserver::TSLocalLockManagerPtr ts_local_lock_manager() const override;

  server::Clock* Clock() override;
  const scoped_refptr<MetricEntity>& MetricEnt() const override;
  rpc::Publisher* GetPublisher() override { return nullptr; }

  Result<tablet::TabletPeerPtr> GetServingTablet(const TabletId& tablet_id) const override;
  Result<tablet::TabletPeerPtr> GetServingTablet(const Slice& tablet_id) const override;

  Status GetTabletStatus(const tserver::GetTabletStatusRequestPB* req,
                         tserver::GetTabletStatusResponsePB* resp) const override;

  bool LeaderAndReady(const TabletId& tablet_id, bool allow_stale = false) const override;

  const NodeInstancePB& NodeInstance() const override;

  Status GetRegistration(ServerRegistrationPB* reg) const override;

  Status StartRemoteBootstrap(const consensus::StartRemoteBootstrapRequestPB& req) override;

  uint32_t get_oid_cache_invalidations_count() const override { return 0; }

  // Get the global catalog versions.
  void get_ysql_catalog_version(uint64_t* current_version,
                                uint64_t* last_breaking_version) const override;
  // Get the per-db catalog versions for database db_oid.
  void get_ysql_db_catalog_version(uint32_t db_oid,
                                   uint64_t* current_version,
                                   uint64_t* last_breaking_version) const override;
  Status get_ysql_db_oid_to_cat_version_info_map(
      const tserver::GetTserverCatalogVersionInfoRequestPB& req,
      tserver::GetTserverCatalogVersionInfoResponsePB *resp) const override;

  Status GetTserverCatalogMessageLists(
      const tserver::GetTserverCatalogMessageListsRequestPB& req,
      tserver::GetTserverCatalogMessageListsResponsePB *resp) const override;

  Status SetTserverCatalogMessageList(
      uint32_t db_oid, bool is_breaking_change, uint64_t new_catalog_version,
      const std::optional<std::string>& message_list) override;

  client::TransactionPool& TransactionPool() override;

  ConcurrentPointerReference<tserver::TServerSharedData> SharedObject() override;

  docdb::ObjectLockSharedStateManager* ObjectLockSharedStateManager() const override {
    return nullptr;
  }

  const std::shared_future<client::YBClient*>& client_future() const override;

  Status GetLiveTServers(
      std::vector<master::TSInformationPB> *live_tservers) const override;

  virtual Result<std::vector<client::internal::RemoteTabletServerPtr>>
      GetRemoteTabletServers() const override;

  virtual Result<std::vector<client::internal::RemoteTabletServerPtr>>
      GetRemoteTabletServers(const std::unordered_set<std::string>& ts_uuids) const override;

  const std::shared_ptr<MemTracker>& mem_tracker() const override;

  void SetPublisher(rpc::Publisher service) override;

  void SetCQLServer(yb::server::RpcAndWebServerBase* server,
      server::YCQLServerExternalInterface* cql_server_if) override {
    LOG_WITH_FUNC(FATAL) << "should not be called on the master";
  }

  void RegisterCertificateReloader(tserver::CertificateReloader reloader) override {}

  rpc::Messenger* GetMessenger(ash::Component component) const override;

  std::shared_ptr<cdc::CDCServiceImpl> GetCDCService() const override {
    return cdc_service_;
  }

  rpc::ServiceIfPtr CreateCDCService(
      const scoped_refptr<MetricEntity>& metric_entity,
      const std::shared_future<client::YBClient*>& client_future, MetricRegistry* metric_registry);

  void EnableCDCService();

  void ClearAllMetaCachesOnServer() override;

  Status ClearMetacache(const std::string& namespace_id) override;

  Status YCQLStatementStats(const tserver::PgYCQLStatementStatsRequestPB& req,
      tserver::PgYCQLStatementStatsResponsePB* resp) const override;

  Status ClearYCQLMetaDataCache() override;

  virtual Result<std::vector<tablet::TabletStatusPB>> GetLocalTabletsMetadata() const override;

  virtual Result<std::vector<TserverMetricsInfoPB>> GetMetrics() const override;

  bool SkipCatalogVersionChecks() override;

  void SetYsqlDBCatalogVersions(
      const tserver::DBCatalogVersionDataPB& db_catalog_version_data) override {}

  Result<tserver::YSQLLeaseInfo> GetYSQLLeaseInfo() const override;
  Status RestartPG() const override {
    return STATUS(NotSupported, "RestartPG not implemented for masters");
  }
  Status KillPg() const override {
    return STATUS(NotSupported, "KillPg not implemented for masters");
  }
  const std::string& permanent_uuid() const override;

  Result<tserver::PgTxnSnapshot> GetLocalPgTxnSnapshot(
        const tserver::PgTxnSnapshotLocalId& snapshot_id) override;

  Result<std::string> GetUniverseUuid() const override;

 private:
  Result<pgwrapper::PGConn> CreateInternalPGConn(
      const std::string& database_name, const std::optional<CoarseTimePoint>& deadline) override;

  Master* master_ = nullptr;
  scoped_refptr<MetricEntity> metric_entity_;

  // CDC service.
  std::shared_ptr<cdc::CDCServiceImpl> cdc_service_;
};

} // namespace yb::master
