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

#include <string>
#include <sstream>
#include <vector>

#include "yb/common/wire_protocol.pb.h"
#include "yb/gutil/macros.h"

#include "yb/master/master_fwd.h"

#include "yb/server/webserver.h"
#include "yb/server/monitored_task.h"
#include "yb/util/enums.h"
#include "yb/util/jsonwriter.h"

namespace yb {

class Schema;

namespace master {

static constexpr char kTserverAlive[] = "ALIVE";
static constexpr char kTserverDead[] = "DEAD";

class Master;
struct TabletReplica;
class TSDescriptor;
class TSRegistrationPB;

YB_DEFINE_ENUM(TServersViewType, (kTServersDefaultView)(kTServersClocksView));

// Web page support for the master.
class MasterPathHandlers {
 public:
  explicit MasterPathHandlers(Master* master)
    : master_(master),
      output_precision_(6) {
  }

  ~MasterPathHandlers();

  const std::string kYBOrange = "#f75821";
  const std::string kYBDarkBlue = "#202951";
  const std::string kYBLightBlue = "#3eb1cc";
  const std::string kYBGray = "#5e647a";

  const std::vector<std::string> kYBColorList = {
    "#30307F", "#36B8F5",
    "#BB43BC", "#43BFC2", "#90948E",
    "#1C7180", "#EEA95F", "#3590D9",
    "#F0679E", "#707B8E", "#800000",
    "#F08080", "#FF8C00", "#7CFC00",
    "#D2691E", "#696969", "#FFD700",
    "#B8860B", "#006400", "#FF6347"
  };

  Status Register(Webserver* server);

  std::string BytesToHumanReadable (uint64_t bytes);

 private:
  enum TableType {
    kUserTable,
    kUserIndex,
    kParentTable,
    kSystemTable,
    kNumTypes,
  };

  enum CatalogTableColumns {
    kKeyspace,
    kTableName,
    kState,
    kMessage,
    kUuid,
    kYsqlOid,
    kParentOid,
    kColocationId,
    kOnDiskSize,
    kHidden,
    kNumColumns
  };

  enum NamespaceColumns {
    kNamespaceName,
    kNamespaceId,
    kNamespaceLanguage,
    kNamespaceState,
    kNamespaceColocated,
    kNumNamespaceColumns
  };

  struct TabletCounts {
    uint32_t user_tablet_leaders = 0;
    uint32_t user_tablet_followers = 0;
    uint32_t system_tablet_leaders = 0;
    uint32_t system_tablet_followers = 0;

    void operator+=(const TabletCounts& other);
  };

  // Struct used to store the number of nodes and tablets in an availability zone.
  struct ZoneTabletCounts {
    TabletCounts tablet_counts;
    uint32_t node_count = 1;
    uint32_t active_tablets_count;

    ZoneTabletCounts() = default;

    // Create a ZoneTabletCounts object from the TabletCounts of a TServer (one node).
    ZoneTabletCounts(const TabletCounts& tablet_counts, uint32_t active_tablets_count);

    void operator+=(const ZoneTabletCounts& other);

    typedef std::map<std::string, ZoneTabletCounts> ZoneTree;
    typedef std::map<std::string, ZoneTree> RegionTree;
    typedef std::map<std::string, RegionTree> CloudTree;
  };
  // Map of tserver UUID -> TabletCounts
  typedef std::unordered_map<std::string, TabletCounts> TabletCountMap;

  struct ReplicaInfo {
    PeerRole role;
    TabletId tablet_id;

    ReplicaInfo(const PeerRole& role, const TabletId& tablet_id) {
      this->role = role;
      this->tablet_id = tablet_id;
    }
  };

  // Map of table id -> tablet list for a tserver.
  typedef std::unordered_map<std::string, std::vector<ReplicaInfo>> PerTServerTableTree;

  // Map of tserver UUID -> its table tree.
  typedef std::unordered_map<std::string, PerTServerTableTree> TServerTree;

  // Map of zone -> its tserver tree.
  typedef std::unordered_map<std::string, TServerTree> ZoneToTServer;

  const std::string table_type_[kNumTypes] = {"User", "Index", "Parent", "System"};

  const std::string kNoPlacementUUID = "NONE";

  static inline void TServerTable(std::stringstream* output, TServersViewType viewType);

  void TServerDisplay(const std::string& current_uuid,
                      std::vector<std::shared_ptr<TSDescriptor>>* descs,
                      TabletCountMap* tmap,
                      std::stringstream* output,
                      const int hide_dead_node_threshold_override,
                      TServersViewType viewType);

  // Outputs a ZoneTabletCounts::CloudTree as an html table with a heading.
  static void DisplayTabletZonesTable(
    const ZoneTabletCounts::CloudTree& counts,
    std::stringstream* output
  );

  // Builds a "cloud -> region -> zone" tree of tablet and node counts.
  // Each leaf of the tree is a ZoneTabletCounts struct corresponding to the
  // unique availability zone identified by the path from the root to the leaf.
  ZoneTabletCounts::CloudTree CalculateTabletCountsTree(
    const std::vector<std::shared_ptr<TSDescriptor>>& descriptors,
    const TabletCountMap& tablet_count_map
  );

  void CallIfLeaderOrPrintRedirect(const Webserver::WebRequest& req, Webserver::WebResponse* resp,
                                   const Webserver::PathHandlerCallback& callback);
  void RedirectToLeader(const Webserver::WebRequest& req, Webserver::WebResponse* resp);
  Result<std::string> GetLeaderAddress(const Webserver::WebRequest& req);
  void RootHandler(const Webserver::WebRequest& req,
                   Webserver::WebResponse* resp);
  void HandleTabletServers(const Webserver::WebRequest& req,
                           Webserver::WebResponse* resp,
                           TServersViewType viewType);
  void HandleCatalogManager(const Webserver::WebRequest& req,
                            Webserver::WebResponse* resp,
                            bool only_user_tables = false);
  void HandleCatalogManagerJSON(const Webserver::WebRequest& req,
                                Webserver::WebResponse* resp);
  void HandleNamespacesHTML(const Webserver::WebRequest& req,
                            Webserver::WebResponse* resp,
                            bool only_user_namespaces = false);
  void HandleNamespacesJSON(const Webserver::WebRequest& req, Webserver::WebResponse* resp);
  void HandleTablePage(const Webserver::WebRequest& req,
                       Webserver::WebResponse* resp);
  void HandleTablePageJSON(const Webserver::WebRequest& req,
                           Webserver::WebResponse* resp);
  void HandleTasksPage(const Webserver::WebRequest& req,
                       Webserver::WebResponse* resp);
  void HandleTabletReplicasPage(const Webserver::WebRequest &req, Webserver::WebResponse *resp);
  void HandleMasters(const Webserver::WebRequest& req,
                     Webserver::WebResponse* resp);
  void HandleDumpEntities(const Webserver::WebRequest& req,
                          Webserver::WebResponse* resp);
  void HandleGetTserverStatus(const Webserver::WebRequest& req,
                          Webserver::WebResponse* resp);
  void HandleGetClusterConfig(const Webserver::WebRequest& req, Webserver::WebResponse* resp);
  void HandleGetClusterConfigJSON(const Webserver::WebRequest& req, Webserver::WebResponse* resp);
  void HandleGetXClusterConfig(const Webserver::WebRequest& req, Webserver::WebResponse* resp);
  void HandleGetXClusterConfigJSON(const Webserver::WebRequest& req, Webserver::WebResponse* resp);
  void GetXClusterJSON(std::stringstream& output, bool pretty);
  void HandleXCluster(const Webserver::WebRequest& req, Webserver::WebResponse* resp);
  void HandleHealthCheck(const Webserver::WebRequest& req, Webserver::WebResponse* resp);
  void HandleCheckIfLeader(const Webserver::WebRequest& req, Webserver::WebResponse* resp);
  void HandleGetMastersStatus(const Webserver::WebRequest& req, Webserver::WebResponse* resp);
  void HandleGetReplicationStatus(const Webserver::WebRequest &req, Webserver::WebResponse *resp);
  void HandleGetUnderReplicationStatus(const Webserver::WebRequest &req,
                                        Webserver::WebResponse *resp);
  void HandleVersionInfoDump(const Webserver::WebRequest &req, Webserver::WebResponse *resp);
  void HandlePrettyLB(const Webserver::WebRequest& req, Webserver::WebResponse* resp);
  void HandleLoadBalancer(const Webserver::WebRequest& req, Webserver::WebResponse* resp);

  // Calcuates number of leaders/followers per table.
  void CalculateTabletMap(TabletCountMap* tablet_map);

  // Calculate tserver tree for ALL tables if max_table_count == -1.
  // Otherwise, do not perform calculation if number of tables is less than max_table_count.
  Status CalculateTServerTree(TServerTree* tserver_tree, int max_table_count);
  void RenderLoadBalancerViewPanel(
      const TServerTree& tserver_tree, const std::vector<std::shared_ptr<TSDescriptor>>& descs,
      const std::vector<TableInfoPtr>& tables, std::stringstream* output);
  TableType GetTableType(const TableInfo& table);
  std::vector<TabletInfoPtr> GetNonSystemTablets();

  std::vector<std::pair<TabletInfoPtr, std::string>> GetLeaderlessTablets();

  Result<std::vector<std::pair<TabletInfoPtr, std::vector<std::string>>>>
      GetUnderReplicatedTablets();

  // Calculates the YSQL OID of a tablegroup / colocated database parent table
  std::string GetParentTableOid(scoped_refptr<TableInfo> parent_table);

  // Convert location of peers to HTML, indicating the roles
  // of each tablet server in a consensus configuration.
  // This method will display 'locations' in the order given.
  std::string RaftConfigToHtml(const std::vector<TabletReplica>& locations,
                               const std::string& tablet_id) const;

  // Convert the specified TSDescriptor to HTML, adding a link to the
  // tablet server's own webserver if specified in 'desc'.
  std::string TSDescriptorToHtml(const TSDescriptor& desc,
                                 const std::string& tablet_id) const;

  // Convert the specified server registration to HTML, adding a link
  // to the server's own web server (if specified in 'reg') with
  // anchor text 'link_text'.
  std::string RegistrationToHtml(
      const ServerRegistrationPB& reg, const std::string& link_text) const;

  std::string GetHttpHostPortFromServerRegistration(const ServerRegistrationPB& reg) const;

  Status GetXClusterConfigs(
      SysXClusterConfigEntryPB* xcluster_config, SysClusterConfigEntryPB* cluster_config,
      GetReplicationStatusResponsePB* xcluster_status,
      std::vector<SysUniverseReplicationEntryPB>* replication_infos);

  Master* master_;

  const int output_precision_;
  DISALLOW_COPY_AND_ASSIGN(MasterPathHandlers);
};

void HandleTabletServersPage(const Webserver::WebRequest& req, Webserver::WebResponse* resp);

}  //  namespace master
}  //  namespace yb
