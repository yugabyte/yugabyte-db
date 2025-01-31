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

#include "yb/master/master_tserver.h"

#include <map>
#include <set>

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>

#include "yb/common/pg_types.h"

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

DECLARE_bool(create_initial_sys_catalog_snapshot);

namespace yb {
namespace master {

using consensus::StartRemoteBootstrapRequestPB;

MasterTabletServer::MasterTabletServer(Master* master, scoped_refptr<MetricEntity> metric_entity)
    : master_(master), metric_entity_(metric_entity) {
}

tserver::TSTabletManager* MasterTabletServer::tablet_manager() {
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
    LOG(ERROR) << "Could not get YSQL catalog version for master's tserver API: "
               << s.ToUserMessage();
    fill_vers();
  }
}

tserver::TServerSharedData& MasterTabletServer::SharedObject() {
  return master_->shared_object();
}

Status MasterTabletServer::get_ysql_db_oid_to_cat_version_info_map(
    const tserver::GetTserverCatalogVersionInfoRequestPB& req,
    tserver::GetTserverCatalogVersionInfoResponsePB* resp) const {
  return master_->get_ysql_db_oid_to_cat_version_info_map(req, resp);
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

const std::string& MasterTabletServer::permanent_uuid() const {
  return master_->permanent_uuid();
}

} // namespace master
} // namespace yb
