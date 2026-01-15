// Copyright (c) YugabyteDB, Inc.
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

#include "yb/master/master_tserver.h"

#include <map>
#include <set>

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_service_context.h"

#include "yb/client/client.h"

#include "yb/common/pg_types.h"
#include "yb/common/wire_protocol.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/master/master.h"
#include "yb/master/scoped_leader_shared_lock.h"
#include "yb/master/sys_catalog_constants.h"

#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/tserver.pb.h"
#include "yb/tserver/pg_client.pb.h"
#include "yb/tserver/pg_client_session.h"

#include "yb/util/atomic.h"
#include "yb/util/metric_entity.h"
#include "yb/util/monotime.h"
#include "yb/util/status_format.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

DEFINE_NON_RUNTIME_int32(master_xrepl_get_changes_concurrency, 3,
  "This determines the max number of concurrent GetChanges RPCs that can be "
  "processed on master.");
TAG_FLAG(master_xrepl_get_changes_concurrency, advanced);

DEFINE_RUNTIME_int32(update_min_cdc_indices_master_interval_secs, 300 /* 5 minutes */,
  "How often to read cdc_state table on master and move the retention barriers for the sys "
  "catalog tablet.");

DECLARE_bool(create_initial_sys_catalog_snapshot);
DECLARE_bool(ysql_yb_enable_implicit_dynamic_tables_logical_replication);

namespace yb {
namespace master {

using consensus::StartRemoteBootstrapRequestPB;

class MasterCDCServiceContextImpl : public cdc::CDCServiceContext {
 public:
  explicit MasterCDCServiceContextImpl(MasterTabletServer* master_tablet_server)
      : master_tablet_server_(*master_tablet_server) {}

  tablet::TabletPeerPtr LookupTablet(const TabletId& tablet_id) const override {
    auto serving_tablet_res = master_tablet_server_.GetServingTablet(tablet_id);
    return serving_tablet_res.ok() ? *serving_tablet_res : nullptr;
  }

  Result<tablet::TabletPeerPtr> GetTablet(const TabletId& tablet_id) const override {
    SCHECK_EQ(
        tablet_id, kSysCatalogTabletId, NotFound, Format("Tablet ID $0 not found.", tablet_id));
    return master_tablet_server_.GetServingTablet(tablet_id);
  }

  Result<tablet::TabletPeerPtr> GetServingTablet(const TabletId& tablet_id) const override {
    SCHECK_EQ(
        tablet_id, kSysCatalogTabletId, NotFound, Format("Tablet ID $0 not found.", tablet_id));
    return master_tablet_server_.GetServingTablet(tablet_id);
  }

  const std::string& permanent_uuid() const override {
    return master_tablet_server_.permanent_uuid();
  }

  Result<uint32> GetAutoFlagsConfigVersion() const override {
    return STATUS(InternalError, "Unexpected call to GetAutoFlagsConfigVersion in master_tserver.");
  }

  Result<HostPort> GetDesiredHostPortForLocal() const override {
    return STATUS(NotSupported, "GetDesiredHostPortForLocal not supported on master");
  }

 private:
  MasterTabletServer& master_tablet_server_;
};

MasterTabletServer::MasterTabletServer(Master* master, scoped_refptr<MetricEntity> metric_entity)
    : master_(master), metric_entity_(metric_entity) {
}

tserver::TSTabletManager* MasterTabletServer::tablet_manager() const {
  return nullptr;
}

tserver::TabletPeerLookupIf* MasterTabletServer::tablet_peer_lookup() {
  return this;
}

server::Clock* MasterTabletServer::Clock() {
  return master_->clock();
}

const scoped_refptr<MetricEntity>& MasterTabletServer::MetricEnt() const {
  return metric_entity_;
}

tserver::TSLocalLockManagerPtr MasterTabletServer::ts_local_lock_manager() const {
  return nullptr;
}

Result<tablet::TabletPeerPtr> MasterTabletServer::GetServingTablet(
    const TabletId& tablet_id) const {
  return master_->catalog_manager()->GetServingTablet(tablet_id);
}

Result<tablet::TabletPeerPtr> MasterTabletServer::GetServingTablet(const Slice& tablet_id) const {
  return master_->catalog_manager()->GetServingTablet(tablet_id);
}

Status MasterTabletServer::GetTabletStatus(const tserver::GetTabletStatusRequestPB* req,
                                           tserver::GetTabletStatusResponsePB* resp) const {
  // Tablets for YCQL virtual tables have no peer and we will return the NotFound status. That is
  // ok because GetTabletStatus is called for the cases when a tablet is moved or otherwise down
  // and being boostrapped, which should not happen to those tables.
  auto tablet_peer = VERIFY_RESULT(GetServingTablet(req->tablet_id()));
  tablet_peer->GetTabletStatusPB(resp->mutable_tablet_status());
  return Status::OK();
}

bool MasterTabletServer::LeaderAndReady(const TabletId& tablet_id, bool allow_stale) const {
  auto tablet_peer = GetServingTablet(tablet_id);
  if (!tablet_peer.ok()) {
    return false;
  }
  return (**tablet_peer).LeaderStatus(allow_stale) == consensus::LeaderStatus::LEADER_AND_READY;
}

const NodeInstancePB& MasterTabletServer::NodeInstance() const {
  return master_->catalog_manager()->NodeInstance();
}

Status MasterTabletServer::GetRegistration(ServerRegistrationPB* reg) const {
  return STATUS(NotSupported, "Getting tserver registration not supported by master tserver");
}

Status MasterTabletServer::StartRemoteBootstrap(const StartRemoteBootstrapRequestPB& req) {
  return STATUS(NotSupported, "Remote bootstrap not supported by master tserver");
}

void MasterTabletServer::get_ysql_catalog_version(uint64_t* current_version,
                                                  uint64_t* last_breaking_version) const {
  get_ysql_db_catalog_version(kPgInvalidOid, current_version, last_breaking_version);
}

void MasterTabletServer::get_ysql_db_catalog_version(uint32_t db_oid,
                                                     uint64_t* current_version,
                                                     uint64_t* last_breaking_version) const {
  auto fill_vers = [current_version, last_breaking_version](){
    /*
     * This should never happen, but if it does then we cannot guarantee that user requests
     * received by this master's tserver interface have a compatible version.
     * Log an error and return the highest possible version to ensure we reject the request if
     * it needs a catalog version compatibility check.
     */
    if (current_version) {
      *current_version = UINT64_MAX;
    }
    if (last_breaking_version) {
      *last_breaking_version = UINT64_MAX;
    }
  };
  // Ensure that we are currently the Leader before handling catalog version.
  {
    SCOPED_LEADER_SHARED_LOCK(l, master_->catalog_manager_impl());
    if (!l.IsInitializedAndIsLeader()) {
      LOG(WARNING) << l.failed_status_string();
      fill_vers();
      return;
    }
  }

  Status s = db_oid == kPgInvalidOid ?
    master_->catalog_manager()->GetYsqlCatalogVersion(current_version, last_breaking_version) :
    master_->catalog_manager()->GetYsqlDBCatalogVersion(
        db_oid, current_version, last_breaking_version);
  if (!s.ok()) {
    LOG(WARNING) << "Could not get YSQL catalog version for master's tserver API: " << s;
    fill_vers();
  }
}

ConcurrentPointerReference<tserver::TServerSharedData> MasterTabletServer::SharedObject() {
  return master_->shared_object();
}

Status MasterTabletServer::get_ysql_db_oid_to_cat_version_info_map(
    const tserver::GetTserverCatalogVersionInfoRequestPB& req,
    tserver::GetTserverCatalogVersionInfoResponsePB* resp) const {
  return master_->get_ysql_db_oid_to_cat_version_info_map(req, resp);
}

Status MasterTabletServer::GetTserverCatalogMessageLists(
    const tserver::GetTserverCatalogMessageListsRequestPB& req,
    tserver::GetTserverCatalogMessageListsResponsePB *resp) const {
  return master_->GetTserverCatalogMessageLists(req, resp);
}

Status MasterTabletServer::SetTserverCatalogMessageList(
    uint32_t db_oid, bool is_breaking_change, uint64_t new_catalog_version,
    const std::optional<std::string>& message_list) {
  return master_->SetTserverCatalogMessageList(db_oid, is_breaking_change,
                                               new_catalog_version, message_list);
}

Status MasterTabletServer::TriggerRelcacheInitConnection(
    const tserver::TriggerRelcacheInitConnectionRequestPB& req,
    tserver::TriggerRelcacheInitConnectionResponsePB *resp) {
  return master_->TriggerRelcacheInitConnection(req, resp);
}
const std::shared_future<client::YBClient*>& MasterTabletServer::client_future() const {
  return master_->client_future();
}

Status MasterTabletServer::GetLiveTServers(
    std::vector<master::TSInformationPB> *live_tservers) const {
  return Status::OK();
}

Result<std::vector<client::internal::RemoteTabletServerPtr>>
    MasterTabletServer::GetRemoteTabletServers() const {
  return STATUS_FORMAT(NotSupported,
                       Format("GetRemoteTabletServers not implemented for master_tserver"));
}

Result<std::vector<client::internal::RemoteTabletServerPtr>>
    MasterTabletServer::GetRemoteTabletServers(const std::unordered_set<std::string>&) const {
  return STATUS_FORMAT(NotSupported,
                       Format("GetRemoteTabletServers not implemented for master_tserver"));
}

const std::shared_ptr<MemTracker>& MasterTabletServer::mem_tracker() const {
  return master_->mem_tracker();
}

void MasterTabletServer::SetPublisher(rpc::Publisher service) {
}

client::TransactionPool& MasterTabletServer::TransactionPool() {
  LOG(FATAL) << "Unexpected call of TransactionPool()";
  client::TransactionPool* temp = nullptr;
  return *temp;
}

rpc::Messenger* MasterTabletServer::GetMessenger(ash::Component component) const {
  return nullptr;
}

void MasterTabletServer::ClearAllMetaCachesOnServer() {
  client()->ClearAllMetaCachesOnServer();
}

Status MasterTabletServer::ClearMetacache(const std::string& namespace_id) {
  return client()->ClearMetacache(namespace_id);
}

Status MasterTabletServer::YCQLStatementStats(const tserver::PgYCQLStatementStatsRequestPB& req,
    tserver::PgYCQLStatementStatsResponsePB* resp) const {
  LOG(FATAL) << "Unexpected call of YCQLStatementStats()";
  return Status::OK();
}

Result<std::vector<tablet::TabletStatusPB>> MasterTabletServer::GetLocalTabletsMetadata() const {
  LOG(DFATAL) << "Unexpected call of GetLocalTabletsMetadata()";
  return STATUS_FORMAT(InternalError, "Unexpected call of GetLocalTabletsMetadata()");
}

Result<std::vector<TserverMetricsInfoPB>> MasterTabletServer::GetMetrics() const {
  LOG(DFATAL) << "Unexpected call of GetMetrics()";
  return STATUS_FORMAT(InternalError, "Unexpected call of GetMetrics()");
}

Result<pgwrapper::PGConn> MasterTabletServer::CreateInternalPGConn(
    const std::string& database_name, const std::optional<CoarseTimePoint>& deadline) {
  LOG(DFATAL) << "Unexpected call of CreateInternalPGConn()";
  return STATUS_FORMAT(InternalError, "Unexpected call of CreateInternalPGConn()");
}

Result<tserver::PgTxnSnapshot> MasterTabletServer::GetLocalPgTxnSnapshot(
  const tserver::PgTxnSnapshotLocalId& snapshot_id) {
  LOG(WARNING) << "Unexpected call of " << __PRETTY_FUNCTION__;
  return STATUS_FORMAT(InternalError, "Unexpected call of $0", __PRETTY_FUNCTION__);
}

bool MasterTabletServer::SkipCatalogVersionChecks() {
  return master_->catalog_manager()->SkipCatalogVersionChecks();
}

Result<tserver::YSQLLeaseInfo> MasterTabletServer::GetYSQLLeaseInfo() const {
  return STATUS(InternalError, "Unexpected call of GetYSQLLeaseInfo");
}

const std::string& MasterTabletServer::permanent_uuid() const {
  return master_->permanent_uuid();
}

Result<std::string> MasterTabletServer::GetUniverseUuid() const {
  LOG(DFATAL) << "Unexpected call of GetUniverseUuid()";
  return STATUS_FORMAT(InternalError, "Unexpected call of GetUniverseUuid()");
}

rpc::ServiceIfPtr MasterTabletServer::CreateCDCService(
    const scoped_refptr<MetricEntity>& metric_entity,
    const std::shared_future<client::YBClient*>& client_future, MetricRegistry* metric_registry) {
  cdc_service_ = std::make_shared<cdc::CDCServiceImpl>(
      std::make_unique<MasterCDCServiceContextImpl>(this), metric_entity, metric_registry,
      FLAGS_master_xrepl_get_changes_concurrency, client_future,
      []() { return FLAGS_update_min_cdc_indices_master_interval_secs; });

  return std::static_pointer_cast<rpc::ServiceIf>(cdc_service_);
}

void MasterTabletServer::EnableCDCService() {
  if (!FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication) {
    return;
  }

  CHECK_NOTNULL(cdc_service_);
  cdc_service_->SetCDCServiceEnabled();
  LOG(INFO) << "CDC service enabled on master";
}

} // namespace master
} // namespace yb
