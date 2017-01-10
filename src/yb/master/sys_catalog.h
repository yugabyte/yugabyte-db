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
#ifndef YB_MASTER_SYS_CATALOG_H_
#define YB_MASTER_SYS_CATALOG_H_

#include <string>
#include <vector>

#include "yb/master/catalog_manager.h"
#include "yb/master/master.pb.h"
#include "yb/server/metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/util/pb_util.h"
#include "yb/util/status.h"

namespace yb {

class Schema;
class FsManager;

namespace tserver {
class WriteRequestPB;
class WriteResponsePB;
}

namespace master {
class Master;
class MasterOptions;

static const char* const kSysCatalogTabletId = "00000000000000000000000000000000";
static const char* const kSysCatalogTableId = "sys.catalog.uuid";

class VisitorBase {
 public:
  VisitorBase() {}
  virtual ~VisitorBase() = default;

  virtual int entry_type() const = 0;

  virtual CHECKED_STATUS Visit(const Slice* id, const Slice* data) = 0;

 protected:
};

template <class PersistentDataEntryClass>
class Visitor : public VisitorBase {
 public:
  Visitor() {}
  virtual ~Visitor() = default;

  virtual CHECKED_STATUS Visit(const Slice* id, const Slice* data) {
    typename PersistentDataEntryClass::data_type metadata;
    RETURN_NOT_OK_PREPEND(
        pb_util::ParseFromArray(&metadata, data->data(), data->size()),
        "Unable to parse metadata field for item id: " + id->ToString());

    return Visit(id->ToString(), metadata);
  }

  int entry_type() const { return PersistentDataEntryClass::type(); }

 protected:
  virtual CHECKED_STATUS Visit(
      const std::string& id, const typename PersistentDataEntryClass::data_type& metadata) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(Visitor);
};

class TableVisitor : public Visitor<PersistentTableInfo> {
 public:
  TableVisitor() : Visitor() {}
};

class TabletVisitor : public Visitor<PersistentTabletInfo> {
 public:
  TabletVisitor() : Visitor() {}
};

class NamespaceVisitor : public Visitor<PersistentNamespaceInfo> {
 public:
  NamespaceVisitor() : Visitor() {}
};

class ClusterConfigVisitor : public Visitor<PersistentClusterConfigInfo> {
 public:
  ClusterConfigVisitor() : Visitor() {}
};

// Forward declaration of internally used class.
class SysCatalogWriter;

// SysCatalogTable is a YB table that keeps track of table and
// tablet metadata.
// - SysCatalogTable has only one tablet.
// - SysCatalogTable is managed by the master and not exposed to the user
//   as a "normal table", instead we have Master APIs to query the table.
class SysCatalogTable {
 public:
  typedef Callback<Status()> ElectedLeaderCallback;

  // 'leader_cb_' is invoked whenever this node is elected as a leader
  // of the consensus configuration for this tablet, including for local standalone
  // master consensus configurations. It used to initialize leader state, submit any
  // leader-specific tasks and so forth.
  //
  /// NOTE: Since 'leader_cb_' is invoked synchronously and can block
  // the consensus configuration's progress, any long running tasks (e.g., scanning
  // tablets) should be performed asynchronously (by, e.g., submitting
  // them to a to a separate threadpool).
  SysCatalogTable(Master* master, MetricRegistry* metrics,
                  ElectedLeaderCallback leader_cb);

  ~SysCatalogTable();

  // Allow for orderly shutdown of tablet peer, etc.
  void Shutdown();

  // Load the Metadata from disk, and initialize the TabletPeer for the sys-table
  CHECKED_STATUS Load(FsManager *fs_manager);

  // Create the new Metadata and initialize the TabletPeer for the sys-table.
  CHECKED_STATUS CreateNew(FsManager *fs_manager);

  // ==================================================================
  // Tables related methods
  // ==================================================================
  CHECKED_STATUS AddTable(const TableInfo* table);
  CHECKED_STATUS UpdateTable(const TableInfo* table);
  CHECKED_STATUS DeleteTable(const TableInfo* table);

  // ==================================================================
  // Tablets related methods
  // ==================================================================
  CHECKED_STATUS AddTablets(const vector<TabletInfo*>& tablets);
  CHECKED_STATUS UpdateTablets(const vector<TabletInfo*>& tablets);
  CHECKED_STATUS AddAndUpdateTablets(const vector<TabletInfo*>& tablets_to_add,
                             const vector<TabletInfo*>& tablets_to_update);
  CHECKED_STATUS DeleteTablets(const vector<TabletInfo*>& tablets);
  // ==================================================================
  // Namespace related methods
  // ==================================================================
  CHECKED_STATUS AddNamespace(const NamespaceInfo *ns);
  CHECKED_STATUS UpdateNamespace(const NamespaceInfo *ns);
  CHECKED_STATUS DeleteNamespace(const NamespaceInfo *ns);
  // ==================================================================
  // ClusterConfig related methods
  // ==================================================================
  CHECKED_STATUS AddClusterConfigInfo(ClusterConfigInfo* config_info);
  CHECKED_STATUS UpdateClusterConfigInfo(ClusterConfigInfo* config_info);

  // ==================================================================
  // Static schema related methods.
  // ==================================================================
  static std::string schema_column_type();
  static std::string schema_column_id();
  static std::string schema_column_metadata();

  const scoped_refptr<tablet::TabletPeer>& tablet_peer() const {
    return tablet_peer_;
  }

  // Create a new tablet peer with information from the metadata
  void SetupTabletPeer(const scoped_refptr<tablet::TabletMetadata>& metadata);

  // Update the in-memory master addresses. Report missing uuid's in the
  // config when check_missing_uuids is set to true.
  CHECKED_STATUS ConvertConfigToMasterAddresses(
      const yb::consensus::RaftConfigPB& config,
      bool check_missing_uuids = false);

  // Create consensus metadata object and flush it to disk.
  CHECKED_STATUS CreateAndFlushConsensusMeta(
      FsManager* fs_manager,
      const yb::consensus::RaftConfigPB& config,
      int64_t current_term);

  CHECKED_STATUS Visit(VisitorBase* visitor);

 private:
  DISALLOW_COPY_AND_ASSIGN(SysCatalogTable);

  friend class CatalogManager;
  friend class SysCatalogWriter;

  std::unique_ptr<SysCatalogWriter> NewWriter();

  const char *table_name() const { return "sys.catalog"; }

  // Return the schema of the table.
  // NOTE: This is the "server-side" schema, so it must have the column IDs.
  Schema BuildTableSchema();

  // Returns 'Status::OK()' if the WriteTranasction completed
  CHECKED_STATUS SyncWrite(const tserver::WriteRequestPB *req, tserver::WriteResponsePB *resp);

  void SysCatalogStateChanged(const std::string& tablet_id,
                              std::shared_ptr<consensus::StateChangeContext> context);

  CHECKED_STATUS SetupTablet(const scoped_refptr<tablet::TabletMetadata>& metadata);

  CHECKED_STATUS OpenTablet(const scoped_refptr<tablet::TabletMetadata>& metadata);

  // Use the master options to generate a new consensus configuration.
  // In addition, resolve all UUIDs of this consensus configuration.
  //
  // Note: The current node adds itself to the peers whether leader or
  // follower, depending on whether the Master options leader flag is
  // set. Even if the local node should be a follower, it should not be listed
  // in the Master options followers list, as it will add itself automatically.
  //
  // TODO: Revisit this whole thing when integrating leader election.
  CHECKED_STATUS SetupDistributedConfig(const MasterOptions& options,
                                consensus::RaftConfigPB* committed_config);

  std::string tablet_id() const {
    return tablet_peer_->tablet_id();
  }

  // Conventional "T xxx P xxxx..." prefix for logging.
  std::string LogPrefix() const;

  // Waits for the tablet to reach 'RUNNING' state.
  //
  // Contrary to tablet servers, in master we actually wait for the master tablet
  // to become online synchronously, this allows us to fail fast if something fails
  // and shouldn't induce the all-workers-blocked-waiting-for-tablets problem
  // that we've seen in tablet servers since the master only has to boot a few
  // tablets.
  CHECKED_STATUS WaitUntilRunning();

  // Shutdown the tablet peer and apply pool which are not needed in shell mode for this master.
  CHECKED_STATUS GoIntoShellMode();

  // Initializes the RaftPeerPB for the local peer.
  // Crashes due to an invariant check if the rpc server is not running.
  void InitLocalRaftPeerPB();

  // Table schema, without IDs, used to send messages to the TabletPeer
  Schema schema_;
  Schema key_schema_;

  MetricRegistry* metric_registry_;

  gscoped_ptr<ThreadPool> apply_pool_;

  scoped_refptr<tablet::TabletPeer> tablet_peer_;

  Master* master_;

  ElectedLeaderCallback leader_cb_;

  consensus::RaftPeerPB local_peer_pb_;
};

} // namespace master
} // namespace yb

#endif
