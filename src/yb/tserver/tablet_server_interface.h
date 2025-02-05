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

#include "yb/ash/wait_state.h"

#include "yb/cdc/cdc_fwd.h"
#include "yb/client/client_fwd.h"
#include "yb/common/common_types.pb.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/rpc/rpc_fwd.h"
#include "yb/master/master_heartbeat.fwd.h"
#include "yb/server/clock.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/tserver/ts_local_lock_manager.h"
#include "yb/tserver/tserver_fwd.h"
#include "yb/tserver/tserver_util_fwd.h"
#include "yb/tserver/local_tablet_server.h"

namespace yb {

class MemTracker;

namespace pgwrapper {
class PGConn;
} // namespace pgwrapper

namespace server {
class RpcAndWebServerBase;
class YCQLStatementStatsProvider;
}

namespace tserver {
class PgYCQLStatementStatsRequestPB;
class PgYCQLStatementStatsResponsePB;

using CertificateReloader = std::function<Status(void)>;
using PgConfigReloader = std::function<Status(void)>;

class TabletServerIf : public LocalTabletServer {
 public:
  virtual ~TabletServerIf() {}

  virtual TSTabletManager* tablet_manager() = 0;
  virtual TabletPeerLookupIf* tablet_peer_lookup() = 0;
  virtual tablet::TSLocalLockManager* ts_local_lock_manager() const = 0;

  virtual server::Clock* Clock() = 0;
  virtual rpc::Publisher* GetPublisher() = 0;

  virtual void get_ysql_catalog_version(uint64_t* current_version,
                                        uint64_t* last_breaking_version) const = 0;
  virtual void get_ysql_db_catalog_version(uint32_t db_oid,
                                           uint64_t* current_version,
                                           uint64_t* last_breaking_version) const = 0;

  virtual Status get_ysql_db_oid_to_cat_version_info_map(
      const tserver::GetTserverCatalogVersionInfoRequestPB& req,
      tserver::GetTserverCatalogVersionInfoResponsePB *resp) const = 0;

  virtual const scoped_refptr<MetricEntity>& MetricEnt() const = 0;

  virtual client::TransactionPool& TransactionPool() = 0;

  virtual const std::shared_future<client::YBClient*>& client_future() const = 0;

  virtual tserver::TServerSharedData& SharedObject() = 0;

  virtual Status GetLiveTServers(
      std::vector<master::TSInformationPB> *live_tservers) const = 0;

  // Returns connection info of all live tservers available at this server.
  virtual Result<std::vector<client::internal::RemoteTabletServerPtr>>
      GetRemoteTabletServers() const = 0;

  // Returns connection info for the passed in 'ts_uuids', if available. If unavailable,
  // returns a bad status.
  virtual Result<std::vector<client::internal::RemoteTabletServerPtr>>
      GetRemoteTabletServers(const std::unordered_set<std::string>& ts_uuids) const = 0;

  virtual const std::shared_ptr<MemTracker>& mem_tracker() const = 0;

  virtual void SetPublisher(rpc::Publisher service) = 0;

  virtual void RegisterCertificateReloader(CertificateReloader reloader) = 0;

  client::YBClient* client() const {
    return client_future().get();
  }

  virtual void SetCQLServer(yb::server::RpcAndWebServerBase* server,
      server::YCQLStatementStatsProvider* stmt_provider) = 0;

  virtual rpc::Messenger* GetMessenger(ash::Component component) const = 0;

  virtual std::shared_ptr<cdc::CDCServiceImpl> GetCDCService() const = 0;

  virtual void ClearAllMetaCachesOnServer() = 0;

  virtual Status ClearMetacache(const std::string& namespace_id) = 0;

  virtual Status YCQLStatementStats(const tserver::PgYCQLStatementStatsRequestPB& req,
    tserver::PgYCQLStatementStatsResponsePB* resp) const = 0;

  virtual Result<std::vector<tablet::TabletStatusPB>> GetLocalTabletsMetadata() const = 0;
  virtual Result<std::vector<TserverMetricsInfoPB>> GetMetrics() const = 0;

  virtual Result<pgwrapper::PGConn> CreateInternalPGConn(
      const std::string& database_name, const std::optional<CoarseTimePoint>& deadline) = 0;

  virtual Result<tserver::PgTxnSnapshot> GetLocalPgTxnSnapshot(
      const PgTxnSnapshotLocalId& snapshot_id) = 0;

  virtual bool SkipCatalogVersionChecks() { return false; }

  virtual const std::string& permanent_uuid() const = 0;
};

} // namespace tserver
} // namespace yb
