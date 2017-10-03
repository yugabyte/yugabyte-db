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
#ifndef KUDU_MASTER_SYS_CATALOG_H_
#define KUDU_MASTER_SYS_CATALOG_H_

#include <string>
#include <vector>

#include "kudu/master/master.pb.h"
#include "kudu/server/metadata.h"
#include "kudu/tablet/tablet_peer.h"
#include "kudu/util/status.h"

namespace kudu {

class Schema;
class FsManager;

namespace tserver {
class WriteRequestPB;
class WriteResponsePB;
}

namespace master {
class Master;
struct MasterOptions;
class TableInfo;
class TabletInfo;

// The SysCatalogTable has two separate visitors because the tables
// data must be loaded into memory before the tablets data.
class TableVisitor {
 public:
  virtual Status VisitTable(const std::string& table_id,
                            const SysTablesEntryPB& metadata) = 0;
};

class TabletVisitor {
 public:
  virtual Status VisitTablet(const std::string& table_id,
                             const std::string& tablet_id,
                             const SysTabletsEntryPB& metadata) = 0;
};

// SysCatalogTable is a Kudu table that keeps track of table and
// tablet metadata.
// - SysCatalogTable has only one tablet.
// - SysCatalogTable is managed by the master and not exposed to the user
//   as a "normal table", instead we have Master APIs to query the table.
class SysCatalogTable {
 public:
  typedef Callback<Status()> ElectedLeaderCallback;

  enum CatalogEntryType {
    TABLES_ENTRY = 1,
    TABLETS_ENTRY = 2
  };

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
  Status Load(FsManager *fs_manager);

  // Create the new Metadata and initialize the TabletPeer for the sys-table.
  Status CreateNew(FsManager *fs_manager);

  // ==================================================================
  // Tables related methods
  // ==================================================================
  Status AddTable(const TableInfo* table);
  Status UpdateTable(const TableInfo* table);
  Status DeleteTable(const TableInfo* table);

  // Scan of the table-related entries.
  Status VisitTables(TableVisitor* visitor);

  // ==================================================================
  // Tablets related methods
  // ==================================================================
  Status AddTablets(const vector<TabletInfo*>& tablets);
  Status UpdateTablets(const vector<TabletInfo*>& tablets);
  Status AddAndUpdateTablets(const vector<TabletInfo*>& tablets_to_add,
                             const vector<TabletInfo*>& tablets_to_update);
  Status DeleteTablets(const vector<TabletInfo*>& tablets);

  // Scan of the tablet-related entries.
  Status VisitTablets(TabletVisitor* visitor);

 private:
  DISALLOW_COPY_AND_ASSIGN(SysCatalogTable);

  friend class CatalogManager;

  const char *table_name() const { return "sys.catalog"; }

  // Return the schema of the table.
  // NOTE: This is the "server-side" schema, so it must have the column IDs.
  Schema BuildTableSchema();

  // Returns 'Status::OK()' if the WriteTranasction completed
  Status SyncWrite(const tserver::WriteRequestPB *req, tserver::WriteResponsePB *resp);

  void SysCatalogStateChanged(const std::string& tablet_id, const std::string& reason);

  Status SetupTablet(const scoped_refptr<tablet::TabletMetadata>& metadata);

  // Use the master options to generate a new consensus configuration.
  // In addition, resolve all UUIDs of this consensus configuration.
  //
  // Note: The current node adds itself to the peers whether leader or
  // follower, depending on whether the Master options leader flag is
  // set. Even if the local node should be a follower, it should not be listed
  // in the Master options followers list, as it will add itself automatically.
  //
  // TODO: Revisit this whole thing when integrating leader election.
  Status SetupDistributedConfig(const MasterOptions& options,
                                consensus::RaftConfigPB* committed_config);

  const scoped_refptr<tablet::TabletPeer>& tablet_peer() const {
    return tablet_peer_;
  }

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
  Status WaitUntilRunning();

  // Table related private methods.
  Status VisitTableFromRow(const RowBlockRow& row, TableVisitor* visitor);

  // Tablet related private methods.

  // Add dirty tablet data to the given row operations.
  Status AddTabletsToPB(const std::vector<TabletInfo*>& tablets,
                        RowOperationsPB::Type op_type,
                        RowOperationsPB* ops) const;
  Status VisitTabletFromRow(const RowBlockRow& row, TabletVisitor* visitor);

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
  consensus::RaftPeerPB::Role old_role_;

  consensus::RaftPeerPB local_peer_pb_;
};

} // namespace master
} // namespace kudu

#endif
