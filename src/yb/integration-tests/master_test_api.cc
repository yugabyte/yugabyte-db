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

#include "yb/integration-tests/master_test_api.h"

#include <deque>
#include <mutex>

#include "yb/client/yb_table_name.h"

#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.proxy.h"

#include "yb/integration-tests/mini_cluster_base.h"

#include "yb/master/catalog_entity_parser.h"
#include "yb/master/master_admin.proxy.h"
#include "yb/master/master_client.proxy.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/master_util.h"
#include "yb/master/sys_catalog_constants.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/status_format.h"

using namespace std::literals;

namespace yb {

namespace {

constexpr auto kRpcTimeout = 30s;

template <class Resp>
Status ResponseStatus(const Status& rpc_status, const Resp& resp) {
  RETURN_NOT_OK(rpc_status);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

template <class Pb>
Result<Pb> ParseRow(master::SysRowEntryType type, const std::string& text) {
  auto message = VERIFY_RESULT(master::DebugStringToCatalogEntityPB(type, text));
  auto* pb = dynamic_cast<Pb*>(message.get());
  SCHECK_NOTNULL(pb);
  return std::move(*pb);
}

// TableInfo keeps its tablets through weak pointers, so the TabletInfos of a snapshot must outlive
// the call that built it: a test usually reads a table from a temporary MasterTestApi and then
// calls GetTablets on it. The last snapshots are kept here, which bounds what tests can hold.
class RecentSnapshots {
 public:
  void Keep(std::shared_ptr<std::vector<master::TabletInfoPtr>> tablets) {
    std::lock_guard lock(mutex_);
    kept_.push_back(std::move(tablets));
    if (kept_.size() > kMaxKept) {
      kept_.pop_front();
    }
  }

 private:
  static constexpr size_t kMaxKept = 64;
  std::mutex mutex_;
  std::deque<std::shared_ptr<std::vector<master::TabletInfoPtr>>> kept_;
};

RecentSnapshots& recent_snapshots() {
  static RecentSnapshots instance;
  return instance;
}

} // namespace

master::TableInfoPtr CatalogSnapshot::FindTableOrNull(const TableId& id) const {
  auto it = tables_.find(id);
  return it == tables_.end() ? nullptr : it->second;
}

Result<master::TableInfoPtr> CatalogSnapshot::GetTableInfo(const TableId& id) const {
  auto table = FindTableOrNull(id);
  if (!table) {
    return STATUS_FORMAT(NotFound, "Table $0 not found", id);
  }
  return table;
}

Result<master::TableInfoPtr> CatalogSnapshot::FindTable(const client::YBTableName& name) const {
  for (const auto& [id, table] : tables_) {
    auto lock = table->LockForRead();
    if (lock->is_deleted() || lock->name() != name.table_name()) {
      continue;
    }
    if (name.has_namespace()) {
      auto ns = FindNamespaceOrNull(lock->pb.namespace_id());
      const auto ns_name = ns ? ns->name() : lock->pb.namespace_name();
      if (ns_name != name.namespace_name()) {
        continue;
      }
    }
    return table;
  }
  return STATUS_FORMAT(NotFound, "Table $0 not found", name);
}

Result<master::TabletInfoPtr> CatalogSnapshot::GetTabletInfo(const TabletId& id) const {
  auto it = tablets_.find(id);
  if (it == tablets_.end()) {
    return STATUS_FORMAT(NotFound, "Tablet $0 not found", id);
  }
  return it->second;
}

master::NamespaceInfoPtr CatalogSnapshot::FindNamespaceOrNull(const NamespaceId& id) const {
  auto it = namespaces_.find(id);
  return it == namespaces_.end() ? nullptr : it->second;
}

Result<master::NamespaceInfoPtr> CatalogSnapshot::FindNamespace(
    YQLDatabase database_type, const NamespaceName& name) const {
  for (const auto& [id, ns] : namespaces_) {
    if (ns->database_type() == database_type && ns->name() == name) {
      return ns;
    }
  }
  return STATUS_FORMAT(NotFound, "Namespace $0 not found", name);
}

std::vector<master::TableInfoPtr> CatalogSnapshot::GetTables() const {
  std::vector<master::TableInfoPtr> result;
  for (const auto& [_, table] : tables_) {
    result.push_back(table);
  }
  return result;
}

std::vector<master::TabletInfoPtr> CatalogSnapshot::GetAllTablets() const {
  std::vector<master::TabletInfoPtr> result;
  for (const auto& [_, tablet] : tablets_) {
    result.push_back(tablet);
  }
  return result;
}

std::vector<master::NamespaceInfoPtr> CatalogSnapshot::GetNamespaces() const {
  std::vector<master::NamespaceInfoPtr> result;
  for (const auto& [_, ns] : namespaces_) {
    result.push_back(ns);
  }
  return result;
}

void CatalogSnapshot::AddTableRow(const TableId& id, const master::SysTablesEntryPB& pb) {
  auto table = make_scoped_refptr<master::TableInfo>(id, pb.colocated());
  auto lock = table->LockForWrite();
  lock.mutable_data()->pb.CopyFrom(pb);
  lock.Commit();
  tables_[id] = std::move(table);
}

void CatalogSnapshot::AddNamespaceRow(const NamespaceId& id, const master::SysNamespaceEntryPB& pb) {
  auto ns = make_scoped_refptr<master::NamespaceInfo>(id, /* tasks_tracker= */ nullptr);
  auto lock = ns->LockForWrite();
  lock.mutable_data()->pb.CopyFrom(pb);
  lock.Commit();
  namespaces_[id] = std::move(ns);
}

Status CatalogSnapshot::AddTabletRow(const TabletId& id, const master::SysTabletsEntryPB& pb) {
  auto primary_table = FindTableOrNull(pb.table_id());
  if (!primary_table) {
    // The tablet of a table whose row is gone, as the C++ loader also skips.
    return Status::OK();
  }
  auto tablet = std::make_shared<master::TabletInfo>(primary_table, id);
  // TableInfo::AddTablets reads the tablet's dirty state, so add it under the write lock, as the
  // loader does.
  auto lock = tablet->LockForWrite();
  lock.mutable_data()->pb.CopyFrom(pb);
  tablets_[id] = tablet;
  // As the loader does: a colocated tablet belongs to every table listed on it.
  std::vector<TableId> table_ids(pb.table_ids().begin(), pb.table_ids().end());
  if (table_ids.empty()) {
    table_ids.push_back(pb.table_id());
  }
  for (const auto& table_id : table_ids) {
    if (auto table = FindTableOrNull(table_id)) {
      RETURN_NOT_OK(table->AddTablets({tablet}));
    }
  }
  lock.Commit();
  return Status::OK();
}

template <class Proxy>
Result<Proxy> MasterTestApi::LeaderProxy() {
  return Proxy(&cluster_->proxy_cache(), VERIFY_RESULT(cluster_->GetLeaderMasterBoundRpcAddr()));
}

Result<HostPort> MasterTestApi::LeaderAddress() {
  return cluster_->GetLeaderMasterBoundRpcAddr();
}

Result<std::vector<std::pair<std::string, std::string>>> MasterTestApi::DumpRows(
    master::SysRowEntryType type, const std::string& entity_id_filter) {
  master::DumpSysCatalogEntriesRequestPB req;
  req.set_entry_type(type);
  if (!entity_id_filter.empty()) {
    req.set_entity_id_filter(entity_id_filter);
  }
  master::DumpSysCatalogEntriesResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(kRpcTimeout);
  auto proxy = VERIFY_RESULT(LeaderProxy<master::MasterAdminProxy>());
  RETURN_NOT_OK(ResponseStatus(proxy.DumpSysCatalogEntries(req, &resp, &controller), resp));
  std::vector<std::pair<std::string, std::string>> rows;
  for (const auto& entry : resp.entries()) {
    rows.emplace_back(entry.entity_id(), entry.pb_debug_string());
  }
  return rows;
}

Result<CatalogSnapshot> MasterTestApi::Catalog() {
  CatalogSnapshot snapshot;
  for (const auto& [id, text] : VERIFY_RESULT(DumpRows(master::SysRowEntryType::NAMESPACE))) {
    snapshot.AddNamespaceRow(
        id, VERIFY_RESULT(ParseRow<master::SysNamespaceEntryPB>(
                master::SysRowEntryType::NAMESPACE, text)));
  }
  for (const auto& [id, text] : VERIFY_RESULT(DumpRows(master::SysRowEntryType::TABLE))) {
    snapshot.AddTableRow(
        id, VERIFY_RESULT(ParseRow<master::SysTablesEntryPB>(master::SysRowEntryType::TABLE, text)));
  }
  for (const auto& [id, text] : VERIFY_RESULT(DumpRows(master::SysRowEntryType::TABLET))) {
    RETURN_NOT_OK(snapshot.AddTabletRow(
        id,
        VERIFY_RESULT(ParseRow<master::SysTabletsEntryPB>(master::SysRowEntryType::TABLET, text))));
  }
  recent_snapshots().Keep(
      std::make_shared<std::vector<master::TabletInfoPtr>>(snapshot.GetAllTablets()));
  return snapshot;
}

Result<master::TableInfoPtr> MasterTestApi::GetTableInfo(const TableId& table_id) {
  return VERIFY_RESULT(Catalog()).GetTableInfo(table_id);
}

Result<master::TableInfoPtr> MasterTestApi::FindTable(const client::YBTableName& table_name) {
  return VERIFY_RESULT(Catalog()).FindTable(table_name);
}

Result<TableId> MasterTestApi::FindTableId(const TableName& table_name) {
  auto resp = VERIFY_RESULT(ListTables());
  for (const auto& table : resp.tables()) {
    if (table.name() == table_name) {
      return table.id();
    }
  }
  return STATUS_FORMAT(NotFound, "Table $0 not found", table_name);
}

Result<master::TabletInfoPtr> MasterTestApi::GetTabletInfo(const TabletId& tablet_id) {
  return VERIFY_RESULT(Catalog()).GetTabletInfo(tablet_id);
}

Result<master::TabletInfos> MasterTestApi::GetTablets(const TableId& table_id) {
  return VERIFY_RESULT(GetTableInfo(table_id))->GetTablets();
}

Result<master::ListTabletServersResponsePB> MasterTestApi::ListTabletServers(bool primary_only) {
  master::ListTabletServersRequestPB req;
  req.set_primary_only(primary_only);
  master::ListTabletServersResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(kRpcTimeout);
  auto proxy = VERIFY_RESULT(LeaderProxy<master::MasterClusterProxy>());
  RETURN_NOT_OK(ResponseStatus(proxy.ListTabletServers(req, &resp, &controller), resp));
  return resp;
}

Result<master::ListTablesResponsePB> MasterTestApi::ListTables(
    const master::ListTablesRequestPB& req) {
  master::ListTablesResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(kRpcTimeout);
  auto proxy = VERIFY_RESULT(LeaderProxy<master::MasterDdlProxy>());
  RETURN_NOT_OK(ResponseStatus(proxy.ListTables(req, &resp, &controller), resp));
  return resp;
}

Result<master::TabletLocationsPB> MasterTestApi::GetTabletLocations(const TabletId& tablet_id) {
  master::GetTabletLocationsRequestPB req;
  req.add_tablet_ids(tablet_id);
  master::GetTabletLocationsResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(kRpcTimeout);
  auto proxy = VERIFY_RESULT(LeaderProxy<master::MasterClientProxy>());
  RETURN_NOT_OK(ResponseStatus(proxy.GetTabletLocations(req, &resp, &controller), resp));
  if (resp.errors_size() > 0) {
    return StatusFromPB(resp.errors(0).status());
  }
  SCHECK_EQ(resp.tablet_locations_size(), 1, IllegalState,
            Format("Expected one location for tablet $0", tablet_id));
  return resp.tablet_locations(0);
}

Result<master::SysClusterConfigEntryPB> MasterTestApi::GetClusterConfig() {
  master::GetMasterClusterConfigRequestPB req;
  master::GetMasterClusterConfigResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(kRpcTimeout);
  auto proxy = VERIFY_RESULT(LeaderProxy<master::MasterClusterProxy>());
  RETURN_NOT_OK(ResponseStatus(proxy.GetMasterClusterConfig(req, &resp, &controller), resp));
  return resp.cluster_config();
}

Status MasterTestApi::ChangeClusterConfig(const master::SysClusterConfigEntryPB& config) {
  master::ChangeMasterClusterConfigRequestPB req;
  *req.mutable_cluster_config() = config;
  master::ChangeMasterClusterConfigResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(kRpcTimeout);
  auto proxy = VERIFY_RESULT(LeaderProxy<master::MasterClusterProxy>());
  return ResponseStatus(proxy.ChangeMasterClusterConfig(req, &resp, &controller), resp);
}

Result<AutoFlagsConfigPB> MasterTestApi::GetAutoFlagsConfig() {
  master::GetAutoFlagsConfigRequestPB req;
  master::GetAutoFlagsConfigResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(kRpcTimeout);
  auto proxy = VERIFY_RESULT(LeaderProxy<master::MasterClusterProxy>());
  RETURN_NOT_OK(ResponseStatus(proxy.GetAutoFlagsConfig(req, &resp, &controller), resp));
  return resp.config();
}

Result<bool> MasterTestApi::IsLoadBalancerIdle() {
  master::IsLoadBalancerIdleRequestPB req;
  master::IsLoadBalancerIdleResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(kRpcTimeout);
  auto proxy = VERIFY_RESULT(LeaderProxy<master::MasterClusterProxy>());
  RETURN_NOT_OK(proxy.IsLoadBalancerIdle(req, &resp, &controller));
  // A busy balancer answers with an error, as CatalogManager::IsLoadBalancerIdle does.
  return !resp.has_error();
}

Status MasterTestApi::WaitForLoadBalancerIdle(MonoDelta timeout) {
  return WaitFor([this] { return IsLoadBalancerIdle(); }, timeout, "Load balancer idle");
}

Result<master::ListCDCStreamsResponsePB> MasterTestApi::ListCDCStreams(
    const master::ListCDCStreamsRequestPB& req) {
  master::ListCDCStreamsResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(kRpcTimeout);
  auto proxy = VERIFY_RESULT(LeaderProxy<master::MasterReplicationProxy>());
  RETURN_NOT_OK(ResponseStatus(proxy.ListCDCStreams(req, &resp, &controller), resp));
  return resp;
}

Status MasterTestApi::SplitTablet(const TabletId& tablet_id, int split_factor) {
  master::SplitTabletRequestPB req;
  req.set_tablet_id(tablet_id);
  if (split_factor > 0) {
    req.set_split_factor(split_factor);
  }
  master::SplitTabletResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(kRpcTimeout);
  auto proxy = VERIFY_RESULT(LeaderProxy<master::MasterAdminProxy>());
  return ResponseStatus(proxy.SplitTablet(req, &resp, &controller), resp);
}

Status MasterTestApi::FlushSysCatalog() {
  master::FlushSysCatalogRequestPB req;
  master::FlushSysCatalogResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(kRpcTimeout);
  auto proxy = VERIFY_RESULT(LeaderProxy<master::MasterAdminProxy>());
  return ResponseStatus(proxy.FlushSysCatalog(req, &resp, &controller), resp);
}

Status MasterTestApi::CompactSysCatalog() {
  master::CompactSysCatalogRequestPB req;
  master::CompactSysCatalogResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(kRpcTimeout);
  auto proxy = VERIFY_RESULT(LeaderProxy<master::MasterAdminProxy>());
  return ResponseStatus(proxy.CompactSysCatalog(req, &resp, &controller), resp);
}

Result<bool> MasterTestApi::IsInitDbDone() {
  master::IsInitDbDoneRequestPB req;
  master::IsInitDbDoneResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(kRpcTimeout);
  auto proxy = VERIFY_RESULT(LeaderProxy<master::MasterAdminProxy>());
  RETURN_NOT_OK(ResponseStatus(proxy.IsInitDbDone(req, &resp, &controller), resp));
  if (resp.has_initdb_error()) {
    return STATUS_FORMAT(RuntimeError, "initdb failed: $0", resp.initdb_error());
  }
  return resp.done();
}

Status MasterTestApi::XClusterStopPersisting(
    const std::vector<NamespaceId>& namespace_ids, MonoDelta timeout) {
  master::XClusterStopPersistingRequestPB req;
  for (const auto& id : namespace_ids) {
    req.add_namespace_ids(id);
  }
  master::XClusterStopPersistingResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(timeout);
  auto proxy = VERIFY_RESULT(LeaderProxy<master::MasterReplicationProxy>());
  return ResponseStatus(proxy.XClusterStopPersisting(req, &resp, &controller), resp);
}

Status MasterTestApi::XClusterStartPersisting(
    const std::vector<NamespaceId>& namespace_ids, MonoDelta timeout) {
  master::XClusterStartPersistingRequestPB req;
  for (const auto& id : namespace_ids) {
    req.add_namespace_ids(id);
  }
  master::XClusterStartPersistingResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(timeout);
  auto proxy = VERIFY_RESULT(LeaderProxy<master::MasterReplicationProxy>());
  return ResponseStatus(proxy.XClusterStartPersisting(req, &resp, &controller), resp);
}

Result<master::SysPersistenceBlockMapEntryPB> MasterTestApi::XClusterGetPersistenceBlockMap() {
  master::XClusterGetPersistenceBlockMapRequestPB req;
  master::XClusterGetPersistenceBlockMapResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(kRpcTimeout);
  auto proxy = VERIFY_RESULT(LeaderProxy<master::MasterReplicationProxy>());
  RETURN_NOT_OK(
      ResponseStatus(proxy.XClusterGetPersistenceBlockMap(req, &resp, &controller), resp));
  return resp.persistence_block_map();
}

Status MasterTestApi::StepDownLeader(const std::string& new_leader_uuid) {
  consensus::LeaderStepDownRequestPB req;
  req.set_tablet_id(master::kSysCatalogTabletId);
  if (!new_leader_uuid.empty()) {
    req.set_new_leader_uuid(new_leader_uuid);
  }
  req.set_force_step_down(true);
  return WaitFor([&]() -> Result<bool> {
    consensus::LeaderStepDownResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(kRpcTimeout);
    auto proxy = VERIFY_RESULT(LeaderProxy<consensus::ConsensusServiceProxy>());
    RETURN_NOT_OK(proxy.LeaderStepDown(req, &resp, &controller));
    if (!resp.has_error()) {
      return true;
    }
    if (resp.error().code() == tserver::TabletServerErrorPB::LEADER_NOT_READY_TO_STEP_DOWN) {
      return false;
    }
    return STATUS_FORMAT(RuntimeError, "Step down failed: $0", resp.ShortDebugString());
  }, kRpcTimeout, Format("Step down of the master leader to '$0'", new_leader_uuid));
}

} // namespace yb
