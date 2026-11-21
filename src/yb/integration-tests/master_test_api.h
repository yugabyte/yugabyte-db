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

#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "yb/client/client_fwd.h"

#include "yb/common/common_types.pb.h"
#include "yb/common/entity_ids_types.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/master_admin.pb.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_cluster.pb.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_fwd.h"
#include "yb/master/master_replication.pb.h"

#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"
#include "yb/util/result.h"

namespace yb {

class MiniClusterBase;

// The master's catalog at one moment, as C++ catalog entities, so tests can read tables, tablets
// and namespaces the way they used to read CatalogManager's objects. The entities are copies built
// from the sys catalog rows the leader returns for DumpSysCatalogEntries: changing them changes
// nothing on the master, and nothing on them changes after they are read.
//
// Every tablet row is kept, DELETED ones included, so GetTabletInfo finds a deleted tablet and
// reports it deleted. A table's tablet list (TableInfo::GetTablets) holds only the tablets that are
// not deleted, as the Rust master's catalog does.
class CatalogSnapshot {
 public:
  master::TableInfoPtr FindTableOrNull(const TableId& id) const;
  Result<master::TableInfoPtr> GetTableInfo(const TableId& id) const;
  // The table with this name in this namespace that is not deleted.
  Result<master::TableInfoPtr> FindTable(const client::YBTableName& name) const;
  Result<master::TabletInfoPtr> GetTabletInfo(const TabletId& id) const;
  master::NamespaceInfoPtr FindNamespaceOrNull(const NamespaceId& id) const;
  Result<master::NamespaceInfoPtr> FindNamespace(
      YQLDatabase database_type, const NamespaceName& name) const;

  std::vector<master::TableInfoPtr> GetTables() const;
  std::vector<master::TabletInfoPtr> GetAllTablets() const;
  std::vector<master::NamespaceInfoPtr> GetNamespaces() const;

  void AddTableRow(const TableId& id, const master::SysTablesEntryPB& pb);
  void AddNamespaceRow(const NamespaceId& id, const master::SysNamespaceEntryPB& pb);
  // Call after every table row is added.
  Status AddTabletRow(const TabletId& id, const master::SysTabletsEntryPB& pb);

 private:
  std::map<TableId, master::TableInfoPtr> tables_;
  std::map<TabletId, master::TabletInfoPtr> tablets_;
  std::map<NamespaceId, master::NamespaceInfoPtr> namespaces_;
};

// What tests ask the master leader, over RPCs, so they run the same against any master and in
// both MiniCluster and ExternalMiniCluster. It replaces reaching into the C++ master's objects
// (MiniMaster::catalog_manager(), ts_manager() and the like), which a Rust master does not have.
class MasterTestApi {
 public:
  // MiniCluster or ExternalMiniCluster.
  explicit MasterTestApi(MiniClusterBase* cluster) : cluster_(cluster) {}

  // The leader's tables, tablets and namespaces.
  Result<CatalogSnapshot> Catalog();

  // Shortcuts over Catalog(), for one lookup.
  Result<master::TableInfoPtr> GetTableInfo(const TableId& table_id);
  Result<master::TableInfoPtr> FindTable(const client::YBTableName& table_name);
  Result<TableId> FindTableId(const TableName& table_name);
  Result<master::TabletInfoPtr> GetTabletInfo(const TabletId& tablet_id);
  // The table's tablets that are not deleted or hidden, by partition.
  Result<master::TabletInfos> GetTablets(const TableId& table_id);

  // The sys catalog rows of one type, as (entity id, entry pb in text format).
  Result<std::vector<std::pair<std::string, std::string>>> DumpRows(
      master::SysRowEntryType type, const std::string& entity_id_filter = "");

  Result<master::ListTabletServersResponsePB> ListTabletServers(bool primary_only = false);
  Result<master::ListTablesResponsePB> ListTables(const master::ListTablesRequestPB& req = {});
  Result<master::TabletLocationsPB> GetTabletLocations(const TabletId& tablet_id);
  Result<master::SysClusterConfigEntryPB> GetClusterConfig();
  Status ChangeClusterConfig(const master::SysClusterConfigEntryPB& config);
  Result<AutoFlagsConfigPB> GetAutoFlagsConfig();
  Result<bool> IsLoadBalancerIdle();
  Status WaitForLoadBalancerIdle(MonoDelta timeout);
  Result<master::ListCDCStreamsResponsePB> ListCDCStreams(
      const master::ListCDCStreamsRequestPB& req);
  // MasterAdmin.SplitTablet: a manual split, as CatalogManager::SplitTablet with is_manual_split.
  // A split_factor of 0 leaves the number of children to the master.
  Status SplitTablet(const TabletId& tablet_id, int split_factor = 0);
  Status FlushSysCatalog();
  Status CompactSysCatalog();
  Result<bool> IsInitDbDone();
  // xCluster persistence of YSQL databases. The master's own deadline is set from `timeout`.
  Status XClusterStopPersisting(const std::vector<NamespaceId>& namespace_ids, MonoDelta timeout);
  Status XClusterStartPersisting(const std::vector<NamespaceId>& namespace_ids, MonoDelta timeout);
  Result<master::SysPersistenceBlockMapEntryPB> XClusterGetPersistenceBlockMap();
  // Consensus LeaderStepDown of the leader's sys catalog tablet, forced, retried while the leader
  // is not ready to step down. An empty new_leader_uuid leaves the next leader to the election.
  Status StepDownLeader(const std::string& new_leader_uuid = "");

  Result<HostPort> LeaderAddress();

 private:
  template <class Proxy>
  Result<Proxy> LeaderProxy();

  MiniClusterBase* cluster_;
};

} // namespace yb
