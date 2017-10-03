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
#ifndef KUDU_TSERVER_TS_TABLET_MANAGER_H
#define KUDU_TSERVER_TS_TABLET_MANAGER_H

#include <boost/optional/optional_fwd.hpp>
#include <boost/thread/locks.hpp>
#include <gtest/gtest_prod.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "kudu/gutil/macros.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/tserver/tablet_peer_lookup.h"
#include "kudu/tserver/tserver_admin.pb.h"
#include "kudu/tserver/tserver.pb.h"
#include "kudu/util/locks.h"
#include "kudu/util/metrics.h"
#include "kudu/util/status.h"
#include "kudu/util/threadpool.h"

namespace kudu {

class PartitionSchema;
class FsManager;
class HostPort;
class Partition;
class Schema;

namespace consensus {
class RaftConfigPB;
} // namespace consensus

namespace master {
class ReportedTabletPB;
class TabletReportPB;
} // namespace master

namespace tablet {
class TabletMetadata;
class TabletPeer;
class TabletStatusPB;
class TabletStatusListener;
}

namespace tserver {
class TabletServer;

// Map of tablet id -> transition reason string.
typedef std::unordered_map<std::string, std::string> TransitionInProgressMap;

class TransitionInProgressDeleter;

// Keeps track of the tablets hosted on the tablet server side.
//
// TODO: will also be responsible for keeping the local metadata about
// which tablets are hosted on this server persistent on disk, as well
// as re-opening all the tablets at startup, etc.
class TSTabletManager : public tserver::TabletPeerLookupIf {
 public:
  // Construct the tablet manager.
  // 'fs_manager' must remain valid until this object is destructed.
  TSTabletManager(FsManager* fs_manager,
                  TabletServer* server,
                  MetricRegistry* metric_registry);

  virtual ~TSTabletManager();

  // Load all tablet metadata blocks from disk, and open their respective tablets.
  // Upon return of this method all existing tablets are registered, but
  // the bootstrap is performed asynchronously.
  Status Init();

  // Waits for all the bootstraps to complete.
  // Returns Status::OK if all tablets bootstrapped successfully. If
  // the bootstrap of any tablet failed returns the failure reason for
  // the first tablet whose bootstrap failed.
  Status WaitForAllBootstrapsToFinish();

  // Shut down all of the tablets, gracefully flushing before shutdown.
  void Shutdown();

  // Create a new tablet and register it with the tablet manager. The new tablet
  // is persisted on disk and opened before this method returns.
  //
  // If tablet_peer is non-NULL, the newly created tablet will be returned.
  //
  // If another tablet already exists with this ID, logs a DFATAL
  // and returns a bad Status.
  Status CreateNewTablet(const std::string& table_id,
                         const std::string& tablet_id,
                         const Partition& partition,
                         const std::string& table_name,
                         const Schema& schema,
                         const PartitionSchema& partition_schema,
                         consensus::RaftConfigPB config,
                         scoped_refptr<tablet::TabletPeer>* tablet_peer);

  // Delete the specified tablet.
  // 'delete_type' must be one of TABLET_DATA_DELETED or TABLET_DATA_TOMBSTONED
  // or else returns Status::IllegalArgument.
  // 'cas_config_opid_index_less_or_equal' is optionally specified to enable an
  // atomic DeleteTablet operation that only occurs if the latest committed
  // raft config change op has an opid_index equal to or less than the specified
  // value. If not, 'error_code' is set to CAS_FAILED and a non-OK Status is
  // returned.
  Status DeleteTablet(const std::string& tablet_id,
                      tablet::TabletDataState delete_type,
                      const boost::optional<int64_t>& cas_config_opid_index_less_or_equal,
                      boost::optional<TabletServerErrorPB::Code>* error_code);

  // Lookup the given tablet peer by its ID.
  // Returns true if the tablet is found successfully.
  bool LookupTablet(const std::string& tablet_id,
                    scoped_refptr<tablet::TabletPeer>* tablet_peer) const;

  // Same as LookupTablet but doesn't acquired the shared lock.
  bool LookupTabletUnlocked(const std::string& tablet_id,
                            scoped_refptr<tablet::TabletPeer>* tablet_peer) const;

  virtual Status GetTabletPeer(const std::string& tablet_id,
                               scoped_refptr<tablet::TabletPeer>* tablet_peer) const
                               OVERRIDE;

  virtual const NodeInstancePB& NodeInstance() const OVERRIDE;

  // Initiate remote bootstrap of the specified tablet.
  // See the StartRemoteBootstrap() RPC declaration in consensus.proto for details.
  // Currently this runs the entire procedure synchronously.
  // TODO: KUDU-921: Run this procedure on a background thread.
  virtual Status StartRemoteBootstrap(const consensus::StartRemoteBootstrapRequestPB& req) OVERRIDE;

  // Generate an incremental tablet report.
  //
  // This will report any tablets which have changed since the last acknowleged
  // tablet report. Once the report is successfully transferred, call
  // MarkTabletReportAcknowledged() to clear the incremental state. Otherwise, the
  // next tablet report will continue to include the same tablets until one
  // is acknowleged.
  //
  // This is thread-safe to call along with tablet modification, but not safe
  // to call from multiple threads at the same time.
  void GenerateIncrementalTabletReport(master::TabletReportPB* report);

  // Generate a full tablet report and reset any incremental state tracking.
  void GenerateFullTabletReport(master::TabletReportPB* report);

  // Mark that the master successfully received and processed the given
  // tablet report. This uses the report sequence number to "un-dirty" any
  // tablets which have not changed since the acknowledged report.
  void MarkTabletReportAcknowledged(const master::TabletReportPB& report);

  // Get all of the tablets currently hosted on this server.
  void GetTabletPeers(std::vector<scoped_refptr<tablet::TabletPeer> >* tablet_peers) const;

  // Marks tablet with 'tablet_id' dirty.
  // Used for state changes outside of the control of TsTabletManager, such as consensus role
  // changes.
  void MarkTabletDirty(const std::string& tablet_id, const std::string& reason);

  // Returns the number of tablets in the "dirty" map, for use by unit tests.
  int GetNumDirtyTabletsForTests() const;

  // Return the number of tablets in RUNNING or BOOTSTRAPPING state.
  int GetNumLiveTablets() const;

  Status RunAllLogGC();

 private:
  FRIEND_TEST(TsTabletManagerTest, TestPersistBlocks);

  // Flag specified when registering a TabletPeer.
  enum RegisterTabletPeerMode {
    NEW_PEER,
    REPLACEMENT_PEER
  };

  // Each tablet report is assigned a sequence number, so that subsequent
  // tablet reports only need to re-report those tablets which have
  // changed since the last report. Each tablet tracks the sequence
  // number at which it became dirty.
  struct TabletReportState {
    uint32_t change_seq;
  };
  typedef std::unordered_map<std::string, TabletReportState> DirtyMap;

  // Standard log prefix, given a tablet id.
  std::string LogPrefix(const std::string& tablet_id) const;

  // Returns Status::OK() iff state_ == MANAGER_RUNNING.
  Status CheckRunningUnlocked(boost::optional<TabletServerErrorPB::Code>* error_code) const;

  // Registers the start of a tablet state transition by inserting the tablet
  // id and reason string into the transition_in_progress_ map.
  // 'reason' is a string included in the Status return when there is
  // contention indicating why the tablet is currently already transitioning.
  // Returns IllegalState if the tablet is already "locked" for a state
  // transition by some other operation.
  // On success, returns OK and populates 'deleter' with an object that removes
  // the map entry on destruction.
  Status StartTabletStateTransitionUnlocked(const std::string& tablet_id,
                                            const std::string& reason,
                                            scoped_refptr<TransitionInProgressDeleter>* deleter);

  // Open a tablet meta from the local file system by loading its superblock.
  Status OpenTabletMeta(const std::string& tablet_id,
                        scoped_refptr<tablet::TabletMetadata>* metadata);

  // Open a tablet whose metadata has already been loaded/created.
  // This method does not return anything as it can be run asynchronously.
  // Upon completion of this method the tablet should be initialized and running.
  // If something wrong happened on bootstrap/initialization the relevant error
  // will be set on TabletPeer along with the state set to FAILED.
  //
  // The tablet must be registered and an entry corresponding to this tablet
  // must be put into the transition_in_progress_ map before calling this
  // method. A TransitionInProgressDeleter must be passed as 'deleter' into
  // this method in order to remove that transition-in-progress entry when
  // opening the tablet is complete (in either a success or a failure case).
  void OpenTablet(const scoped_refptr<tablet::TabletMetadata>& meta,
                  const scoped_refptr<TransitionInProgressDeleter>& deleter);

  // Open a tablet whose metadata has already been loaded.
  void BootstrapAndInitTablet(const scoped_refptr<tablet::TabletMetadata>& meta,
                              scoped_refptr<tablet::TabletPeer>* peer);

  // Add the tablet to the tablet map.
  // 'mode' specifies whether to expect an existing tablet to exist in the map.
  // If mode == NEW_PEER but a tablet with the same name is already registered,
  // or if mode == REPLACEMENT_PEER but a tablet with the same name is not
  // registered, a FATAL message is logged, causing a process crash.
  // Calls to this method are expected to be externally synchronized, typically
  // using the transition_in_progress_ map.
  void RegisterTablet(const std::string& tablet_id,
                      const scoped_refptr<tablet::TabletPeer>& tablet_peer,
                      RegisterTabletPeerMode mode);

  // Create and register a new TabletPeer, given tablet metadata.
  // Calls RegisterTablet() with the given 'mode' parameter after constructing
  // the TablerPeer object. See RegisterTablet() for details about the
  // semantics of 'mode' and the locking requirements.
  scoped_refptr<tablet::TabletPeer> CreateAndRegisterTabletPeer(
      const scoped_refptr<tablet::TabletMetadata>& meta,
      RegisterTabletPeerMode mode);

  // Helper to generate the report for a single tablet.
  void CreateReportedTabletPB(const std::string& tablet_id,
                              const scoped_refptr<tablet::TabletPeer>& tablet_peer,
                              master::ReportedTabletPB* reported_tablet);

  // Mark that the provided TabletPeer's state has changed. That should be taken into
  // account in the next report.
  //
  // NOTE: requires that the caller holds the lock.
  void MarkDirtyUnlocked(const std::string& tablet_id, const std::string& reason);

  // Handle the case on startup where we find a tablet that is not in
  // TABLET_DATA_READY state. Generally, we tombstone the replica.
  Status HandleNonReadyTabletOnStartup(const scoped_refptr<tablet::TabletMetadata>& meta);

  // Delete the tablet using the specified delete_type as the final metadata
  // state. Deletes the on-disk data, as well as all WAL segments.
  Status DeleteTabletData(const scoped_refptr<tablet::TabletMetadata>& meta,
                          tablet::TabletDataState delete_type,
                          const boost::optional<consensus::OpId>& last_logged_opid);

  // Return Status::IllegalState if leader_term < last_logged_term.
  // Helper function for use with remote bootstrap.
  Status CheckLeaderTermNotLower(const std::string& tablet_id,
                                 int64_t leader_term,
                                 int64_t last_logged_term);

  // Print a log message using the given info and tombstone the specified
  // tablet. If tombstoning the tablet fails, a FATAL error is logged, resulting
  // in a crash.
  void LogAndTombstone(const scoped_refptr<tablet::TabletMetadata>& meta,
                       const std::string& msg,
                       const Status& s);

  TSTabletManagerStatePB state() const {
    boost::shared_lock<rw_spinlock> lock(lock_);
    return state_;
  }

  // Initializes the RaftPeerPB for the local peer.
  // Guaranteed to include both uuid and last_seen_addr fields.
  // Crashes with an invariant check if the RPC server is not currently in a
  // running state.
  void InitLocalRaftPeerPB();

  FsManager* const fs_manager_;

  TabletServer* server_;

  consensus::RaftPeerPB local_peer_pb_;

  typedef std::unordered_map<std::string, scoped_refptr<tablet::TabletPeer> > TabletMap;

  // Lock protecting tablet_map_, dirty_tablets_, state_, and
  // transition_in_progress_.
  mutable rw_spinlock lock_;

  // Map from tablet ID to tablet
  TabletMap tablet_map_;

  // Map of tablet ids -> reason strings where the keys are tablets whose
  // bootstrap, creation, or deletion is in-progress
  TransitionInProgressMap transition_in_progress_;

  // Tablets to include in the next incremental tablet report.
  // When a tablet is added/removed/added locally and needs to be
  // reported to the master, an entry is added to this map.
  DirtyMap dirty_tablets_;

  // Next tablet report seqno.
  int32_t next_report_seq_;

  MetricRegistry* metric_registry_;

  TSTabletManagerStatePB state_;

  // Thread pool used to open the tablets async, whether bootstrap is required or not.
  gscoped_ptr<ThreadPool> open_tablet_pool_;

  // Thread pool for apply transactions, shared between all tablets.
  gscoped_ptr<ThreadPool> apply_pool_;

  DISALLOW_COPY_AND_ASSIGN(TSTabletManager);
};

// Helper to delete the transition-in-progress entry from the corresponding set
// when tablet boostrap, create, and delete operations complete.
class TransitionInProgressDeleter : public RefCountedThreadSafe<TransitionInProgressDeleter> {
 public:
  TransitionInProgressDeleter(TransitionInProgressMap* map, rw_spinlock* lock,
                              string entry);

 private:
  friend class RefCountedThreadSafe<TransitionInProgressDeleter>;
  ~TransitionInProgressDeleter();

  TransitionInProgressMap* const in_progress_;
  rw_spinlock* const lock_;
  const std::string entry_;
};

} // namespace tserver
} // namespace kudu
#endif /* KUDU_TSERVER_TS_TABLET_MANAGER_H */
