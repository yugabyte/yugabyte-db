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

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/optional/optional_fwd.hpp>
#include <gtest/gtest_prod.h>

#include "yb/client/client_fwd.h"
#include "yb/client/async_initializer.h"

#include "yb/common/constants.h"
#include "yb/common/snapshot.h"

#include "yb/consensus/consensus_fwd.h"
#include "yb/consensus/metadata.pb.h"

#include "yb/docdb/local_waiting_txn_registry.h"

#include "yb/gutil/callback.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/stl_util.h"

#include "yb/master/master_fwd.h"
#include "yb/master/master_heartbeat.fwd.h"

#include "yb/rocksdb/cache.h"
#include "yb/rocksdb/options.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/tablet/tablet_fwd.h"
#include "yb/tablet/metadata.pb.h"
#include "yb/tablet/tablet_options.h"
#include "yb/tablet/tablet_splitter.h"

#include "yb/tserver/tserver_fwd.h"
#include "yb/tserver/tablet_memory_manager.h"
#include "yb/tserver/tablet_peer_lookup.h"
#include "yb/tserver/tserver_types.pb.h"

#include "yb/util/status_fwd.h"
#include "yb/util/locks.h"
#include "yb/util/rw_mutex.h"
#include "yb/util/shared_lock.h"
#include "yb/util/threadpool.h"

namespace yb {

class GarbageCollector;
class FsManager;
class HostPort;
class Schema;
class BackgroundTask;
class XClusterSafeTimeTest;

namespace consensus {
class RaftConfigPB;
} // namespace consensus

namespace tserver {
class TabletServer;
class FullCompactionManager;

using rocksdb::MemoryMonitor;

// Map of tablet id -> transition reason string.
typedef std::unordered_map<TabletId, std::string> TransitionInProgressMap;

class TransitionInProgressDeleter;
struct TabletCreationMetaData;
typedef boost::container::static_vector<TabletCreationMetaData, kNumSplitParts>
    SplitTabletsCreationMetaData;

typedef Callback<void(tablet::TabletPeerPtr)> ConsensusChangeCallback;

class TabletMetadataValidator;

// If 'expr' fails, log a message, tombstone the given tablet, and return the
// error status.
#define TOMBSTONE_NOT_OK(expr, meta, uuid, msg, ts_manager_ptr) \
  do { \
    Status _s = (expr); \
    if (PREDICT_FALSE(!_s.ok())) { \
      tserver::LogAndTombstone((meta), (msg), (uuid), _s, ts_manager_ptr); \
      return _s; \
    } \
  } while (0)

// Type of tablet directory.
YB_DEFINE_ENUM(TabletDirType, (kData)(kWal));
YB_DEFINE_ENUM(TabletRemoteSessionType, (kBootstrap)(kSnapshotTransfer));

YB_STRONGLY_TYPED_BOOL(MarkDirtyAfterRegister);

// Keeps track of the tablets hosted on the tablet server side.
//
// TODO: will also be responsible for keeping the local metadata about
// which tablets are hosted on this server persistent on disk, as well
// as re-opening all the tablets at startup, etc.
class TSTabletManager : public tserver::TabletPeerLookupIf, public tablet::TabletSplitter {
 public:
  typedef std::vector<std::shared_ptr<tablet::TabletPeer>> TabletPeers;
  typedef std::vector<tablet::TabletPtr> TabletPtrs;

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
  Status Start();

  Status RegisterServiceCallback(
      StatefulServiceKind service_kind, ConsensusChangeCallback callback);

  // Waits for all the bootstraps to complete.
  // Returns Status::OK if all tablets bootstrapped successfully. If
  // the bootstrap of any tablet failed returns the failure reason for
  // the first tablet whose bootstrap failed.
  Status WaitForAllBootstrapsToFinish();

  // Starts shutdown process.
  void StartShutdown();
  // Completes shutdown process and waits for it's completeness.
  void CompleteShutdown();

  ThreadPool* tablet_prepare_pool() const { return tablet_prepare_pool_.get(); }
  ThreadPool* raft_pool() const { return raft_pool_.get(); }
  ThreadPool* read_pool() const { return read_pool_.get(); }
  ThreadPool* append_pool() const { return append_pool_.get(); }
  ThreadPool* log_sync_pool() const { return log_sync_pool_.get(); }
  ThreadPool* full_compaction_pool() const { return full_compaction_pool_.get(); }
  ThreadPool* admin_triggered_compaction_pool() const {
    return admin_triggered_compaction_pool_.get();
  }
  ThreadPool* waiting_txn_pool() const { return waiting_txn_pool_.get(); }
  ThreadPool* flush_retryable_requests_pool() const { return flush_retryable_requests_pool_.get(); }

  // Create a new tablet and register it with the tablet manager. The new tablet
  // is persisted on disk and opened before this method returns.
  //
  // If tablet_peer is non-NULL, the newly created tablet will be returned.
  //
  // If another tablet already exists with this ID, logs a DFATAL
  // and returns a bad Status.
  Result<tablet::TabletPeerPtr> CreateNewTablet(
      const tablet::TableInfoPtr& table_info,
      const std::string& tablet_id,
      const dockv::Partition& partition,
      consensus::RaftConfigPB config,
      const bool colocated = false,
      const std::vector<SnapshotScheduleId>& snapshot_schedules = {},
      const std::unordered_set<StatefulServiceKind>& hosted_services = {});

  Status ApplyTabletSplit(
      tablet::SplitOperation* operation, log::Log* raft_log,
      boost::optional<consensus::RaftConfigPB> committed_raft_config) override;

  // Delete the specified tablet.
  // 'delete_type' must be one of TABLET_DATA_DELETED or TABLET_DATA_TOMBSTONED
  // or else returns Status::IllegalArgument.
  // 'cas_config_opid_index_less_or_equal' is optionally specified to enable an
  // atomic DeleteTablet operation that only occurs if the latest committed
  // raft config change op has an opid_index equal to or less than the specified
  // value. If not, 'error_code' is set to CAS_FAILED and a non-OK Status is
  // returned.
  // If `hide_only` is true, then just hide tablet instead of deleting it.
  // If `keep_data` is true, then on disk data is not deleted.
  Status DeleteTablet(
      const TabletId& tablet_id,
      tablet::TabletDataState delete_type,
      tablet::ShouldAbortActiveTransactions should_abort_active_txns,
      const boost::optional<int64_t>& cas_config_opid_index_less_or_equal,
      bool hide_only,
      bool keep_data,
      boost::optional<TabletServerErrorPB::Code>* error_code);

  // Lookup the given tablet peer by its ID. Returns nullptr if the tablet is not found.
  tablet::TabletPeerPtr LookupTablet(const TabletId& tablet_id) const;
  tablet::TabletPeerPtr LookupTablet(const Slice& tablet_id) const;

  // Lookup the given tablet peer by its ID.
  // Returns NotFound error if the tablet is not found.
  Result<tablet::TabletPeerPtr> GetTablet(const TabletId& tablet_id) const;
  Result<tablet::TabletPeerPtr> GetTablet(const Slice& tablet_id) const;

  Result<tablet::TabletPeerPtr> GetTablet(const char* tablet_id) const {
    return GetTablet(Slice(tablet_id));
  }

  Result<consensus::RetryableRequests> GetTabletRetryableRequests(
      const TabletId& tablet_id) const;

  // Lookup the given tablet peer by its ID.
  // Returns NotFound error if the tablet is not found.
  // Returns IllegalState if the tablet cannot serve requests.
  Result<tablet::TabletPeerPtr> GetServingTablet(const TabletId& tablet_id) const override;
  Result<tablet::TabletPeerPtr> GetServingTablet(const Slice& tablet_id) const override;

  const NodeInstancePB& NodeInstance() const override;

  Status GetRegistration(ServerRegistrationPB* reg) const override;

  // Initiate remote bootstrap of the specified tablet.
  // See the StartRemoteBootstrap() RPC declaration in consensus.proto for details.
  // Currently this runs the entire procedure synchronously.
  // TODO: KUDU-921: Run this procedure on a background thread.
  virtual Status
      StartRemoteBootstrap(const consensus::StartRemoteBootstrapRequestPB& req) override;

  // Initiate remote snapshot transfer of the specified tablet.
  Status StartRemoteSnapshotTransfer(const StartRemoteSnapshotTransferRequestPB& req);

  // Generate a tablet report.
  //
  // This will report any tablets which have changed since the last acknowleged
  // tablet report. Once the report is successfully transferred, call
  // MarkTabletReportAcknowledged() to clear the incremental state. Otherwise, the
  // next tablet report will continue to include the same tablets until one
  // is acknowleged.
  // 'include_bootstrap' flag indicates whether to include bootstrapped tablets that have not
  // changed.  Normal reports include bootstrap information on every HB, but full reports do not.
  //
  // This is thread-safe to call along with tablet modification, but not safe
  // to call from multiple threads at the same time.
  void GenerateTabletReport(master::TabletReportPB* report, bool include_bootstrap = true);

  // Start a full tablet report and reset any incremental state tracking.
  void StartFullTabletReport(master::TabletReportPB* report);

  // Mark that the master successfully received and processed the given tablet report.
  // 'seq_num' - only remove tablets unchanged since the acknowledged report sequence number.
  // 'updates' - explicitly ACK'd updates from the Master, may be a subset of request tablets.
  // 'dirty_check' - DEBUG. Confirm we've processed all dirty tablets after a full sweep.
  void MarkTabletReportAcknowledged(uint32_t seq_num,
                                    const master::TabletReportUpdatesPB& updates,
                                    bool dirty_check = false);

  // Adjust the max number of tablets that will be included in a single report.
  // This is normally controlled by a master-configured GFLAG.
  void SetReportLimit(int32_t limit) {
    std::lock_guard write_lock(mutex_);
    report_limit_ = limit;
  }
  int32_t GetReportLimit() {
    SharedLock<RWMutex> read_lock(mutex_);
    return report_limit_;
  }

  // Get all of the tablets currently hosted on this server.
  TabletPeers GetTabletPeers(TabletPtrs* tablet_ptrs = nullptr) const;
  // Get all of the tablets currently hosted on this server that belong to a given table.
  TabletPeers GetTabletPeersWithTableId(const TableId& table_id) const;
  void GetTabletPeersUnlocked(TabletPeers* tablet_peers) const REQUIRES_SHARED(mutex_);
  void PreserveLocalLeadersOnly(std::vector<const TabletId*>* tablet_ids) const;

  // Get TabletPeers for all status tablets hosted on this server.
  TabletPeers GetStatusTabletPeers();

  // Callback used for state changes outside of the control of TsTabletManager, such as a consensus
  // role change. They are applied asynchronously internally.
  void ApplyChange(const TabletId& tablet_id,
                   std::shared_ptr<consensus::StateChangeContext> context);

  void NotifyConfigChangeToStatefulServices(const TabletId& tablet_id) EXCLUDES(mutex_);

  // Marks tablet with 'tablet_id' dirty.
  // Used for state changes outside of the control of TsTabletManager, such as consensus role
  // changes.
  void MarkTabletDirty(const TabletId& tablet_id,
                       std::shared_ptr<consensus::StateChangeContext> context);

  void MarkTabletBeingRemoteBootstrapped(const TabletId& tablet_id, const TableId& table_id);

  void UnmarkTabletBeingRemoteBootstrapped(const TabletId& tablet_id, const TableId& table_id);

  // Returns the number of tablets in the "dirty" map, for use by unit tests.
  size_t TEST_GetNumDirtyTablets() const;

  // Return the number of tablets in RUNNING or BOOTSTRAPPING state.
  int GetNumLiveTablets() const;

  // Return the number of tablets for which this ts is a leader.
  int GetLeaderCount() const;

  // Set the number of tablets which are waiting to be bootstrapped and can go to RUNNING
  // state in the response proto. Also set the total number of runnable tablets on this tserver.
  // If the tablet manager itself is not initialized, then INT_MAX is set for both.
  Status GetNumTabletsPendingBootstrap(IsTabletServerReadyResponsePB* resp) const;

  Status RunAllLogGC();

  // Creates and updates the map of table to the set of tablets assigned per table per disk
  // for both data and wal directories.
  void GetAndRegisterDataAndWalDir(FsManager* fs_manager,
                                   const std::string& table_id,
                                   const TabletId& tablet_id,
                                   std::string* data_root_dir,
                                   std::string* wal_root_dir);
  // Updates the map of table to the set of tablets assigned per table per disk
  // for both of the given data and wal directories.
  void RegisterDataAndWalDir(FsManager* fs_manager,
                            const std::string& table_id,
                            const TabletId& tablet_id,
                            const std::string& data_root_dir,
                            const std::string& wal_root_dir);
  // Removes the tablet id assigned to the table and disk pair for both the data and WAL directory
  // as pointed by the data and wal directory map.
  void UnregisterDataWalDir(const std::string& table_id,
                            const TabletId& tablet_id,
                            const std::string& data_root_dir,
                            const std::string& wal_root_dir);

  bool IsTabletInTransition(const TabletId& tablet_id) const;

  TabletServer* server() { return server_; }

  MemoryMonitor* memory_monitor() { return tablet_options_.memory_monitor.get(); }

  TabletMemoryManager* tablet_memory_manager() { return mem_manager_.get(); }

  FullCompactionManager* full_compaction_manager() { return full_compaction_manager_.get(); }

  docdb::LocalWaitingTxnRegistry* waiting_txn_registry() { return waiting_txn_registry_.get(); }

  Status UpdateSnapshotsInfo(const master::TSSnapshotsInfoPB& info);

  // Background task that verifies the data on each tablet for consistency.
  void VerifyTabletData();

  // Background task that Retires old metrics.
  void CleanupOldMetrics();

  client::YBClient& client();

  const std::shared_future<client::YBClient*>& client_future();

  tablet::TabletOptions* TEST_tablet_options() { return &tablet_options_; }

  // Trigger admin full compactions concurrently on the provided tablets.
  // should_wait determines whether this function is asynchronous or not.
  Status TriggerAdminCompaction(const TabletPtrs& tablets, bool should_wait);

  // Create Metadata cache atomically and return the metadata cache object.
  client::YBMetaDataCache* CreateYBMetaDataCache();

  // Get the metadata cache object.
  client::YBMetaDataCache* YBMetaDataCache() const;

 private:
  FRIEND_TEST(TsTabletManagerTest, TestTombstonedTabletsAreUnregistered);
  friend class ::yb::XClusterSafeTimeTest;

  // Flag specified when registering a TabletPeer.
  enum RegisterTabletPeerMode {
    NEW_PEER,
    REPLACEMENT_PEER
  };

  typedef std::unordered_set<TabletId> TabletIdUnorderedSet;

  // Maps directory to set of tablets (IDs) using that directory.
  typedef std::map<std::string, TabletIdUnorderedSet> TabletIdSetByDirectoryMap;

  // This is a map that takes a table id and maps it to a map of directory and
  // set of tablets using that directory.
  typedef std::unordered_map<TableId, TabletIdSetByDirectoryMap> TableDiskAssignmentMap;

  // Each tablet report is assigned a sequence number, so that subsequent
  // tablet reports only need to re-report those tablets which have
  // changed since the last report. Each tablet tracks the sequence
  // number at which it became dirty.
  struct TabletReportState {
    uint32_t change_seq;
  };
  typedef std::unordered_map<std::string, TabletReportState> DirtyMap;

  // Returns Status::OK() iff state_ == MANAGER_RUNNING.
  Status CheckRunningUnlocked(boost::optional<TabletServerErrorPB::Code>* error_code) const
      REQUIRES_SHARED(mutex_);

  // Registers the start of a tablet state transition by inserting the tablet
  // id and reason string into the transition_in_progress_ map.
  // 'reason' is a string included in the Status return when there is
  // contention indicating why the tablet is currently already transitioning.
  // Returns IllegalState if the tablet is already "locked" for a state
  // transition by some other operation.
  // On success, returns OK and populates 'deleter' with an object that removes
  // the map entry on destruction.
  Status StartTabletStateTransition(
      const TabletId& tablet_id, const std::string& reason,
      scoped_refptr<TransitionInProgressDeleter>* deleter);

  // Registers the start of a table state transition with "creating tablet" reason.
  // See StartTabletStateTransition.
  Result<scoped_refptr<TransitionInProgressDeleter>> StartTabletStateTransitionForCreation(
      const TabletId& tablet_id);

  // Open a tablet meta from the local file system by loading its superblock.
  Status OpenTabletMeta(const TabletId& tablet_id,
                        scoped_refptr<tablet::RaftGroupMetadata>* metadata);

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
  void OpenTablet(const scoped_refptr<tablet::RaftGroupMetadata>& meta,
                  const scoped_refptr<TransitionInProgressDeleter>& deleter);

  // Open a tablet whose metadata has already been loaded.
  void BootstrapAndInitTablet(const scoped_refptr<tablet::RaftGroupMetadata>& meta,
                              std::shared_ptr<tablet::TabletPeer>* peer);

  // Add the tablet to the tablet map.
  // 'mode' specifies whether to expect an existing tablet to exist in the map.
  // If mode == NEW_PEER but a tablet with the same name is already registered,
  // or if mode == REPLACEMENT_PEER but a tablet with the same name is not
  // registered, a FATAL message is logged, causing a process crash.
  // Calls to this method are expected to be externally synchronized, typically
  // using the transition_in_progress_ map.
  Status RegisterTablet(const TabletId& tablet_id,
                        const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
                        RegisterTabletPeerMode mode);

  Status RegisterTabletUnlocked(const TabletId& tablet_id,
                                const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
                                RegisterTabletPeerMode mode) REQUIRES(mutex_);

  // Create and register a new TabletPeer, given tablet metadata.
  // Calls RegisterTablet() with the given 'mode' parameter after constructing
  // the TablerPeer object. See RegisterTablet() for details about the
  // semantics of 'mode' and the locking requirements.
  Result<std::shared_ptr<tablet::TabletPeer>> CreateAndRegisterTabletPeer(
      const scoped_refptr<tablet::RaftGroupMetadata>& meta,
      RegisterTabletPeerMode mode,
      MarkDirtyAfterRegister mark_dirty_after_register = MarkDirtyAfterRegister::kFalse);

  // Returns either table_data_assignment_map_ or table_wal_assignment_map_ depending on dir_type.
  TableDiskAssignmentMap* GetTableDiskAssignmentMapUnlocked(TabletDirType dir_type);

  // Returns assigned root dir of specified type for specified table and tablet.
  // If root dir is not registered for the specified table_id and tablet_id combination - returns
  // error.
  Result<const std::string&> GetAssignedRootDirForTablet(
      TabletDirType dir_type, const TableId& table_id, const TabletId& tablet_id);

  // Helper to generate the report for a single tablet.
  void CreateReportedTabletPB(const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
                              master::ReportedTabletPB* reported_tablet);

  // Mark that the provided TabletPeer's state has changed. That should be taken into
  // account in the next report.
  //
  // NOTE: requires that the caller holds the lock.
  void MarkDirtyUnlocked(const TabletId& tablet_id,
                         std::shared_ptr<consensus::StateChangeContext> context) REQUIRES(mutex_);

  // This function is a indirection for running on a thread pool.
  void HandleNonReadyTabletOnStartup(
      const scoped_refptr<tablet::RaftGroupMetadata>& meta,
      const scoped_refptr<TransitionInProgressDeleter>& deleter);

  // Handle the case on startup where we find a tablet that is not in ready state. Generally, we
  // tombstone the replica.
  Status DoHandleNonReadyTabletOnStartup(
      const tablet::RaftGroupMetadataPtr& meta,
      const scoped_refptr<TransitionInProgressDeleter>& deleter);

  Status StartSubtabletsSplit(
      const tablet::RaftGroupMetadata& source_tablet_meta, SplitTabletsCreationMetaData* tcmetas);

  // Creates tablet peer and schedules opening the tablet.
  // See CreateAndRegisterTabletPeer and OpenTablet.
  void CreatePeerAndOpenTablet(
      const tablet::RaftGroupMetadataPtr& meta,
      const scoped_refptr<TransitionInProgressDeleter>& deleter);

  TSTabletManagerStatePB state() const {
    SharedLock<RWMutex> lock(mutex_);
    return state_;
  }

  bool ClosingUnlocked() const REQUIRES_SHARED(mutex_);

  // Initializes the RaftPeerPB for the local peer.
  // Guaranteed to include both uuid and last_seen_addr fields.
  // Crashes with an invariant check if the RPC server is not currently in a
  // running state.
  void InitLocalRaftPeerPB();

  std::string LogPrefix() const;

  std::string TabletLogPrefix(const TabletId& tablet_id) const;

  void CleanupCheckpoints();

  void LogCacheGC(MemTracker* log_cache_mem_tracker, size_t required);

  // Check that the the global and per-table RBS limits are respected if flags
  // TEST_crash_if_remote_bootstrap_sessions_greater_than and
  // TEST_crash_if_remote_bootstrap_sessions_per_table_greater_than are non-zero.
  // Used only for tests.
  void MaybeDoChecksForTests(const TableId& table_id) REQUIRES_SHARED(mutex_);

  void CleanupSplitTablets();

  docdb::HistoryCutoff AllowedHistoryCutoff(tablet::RaftGroupMetadata* metadata);

  template <class Key>
  Result<tablet::TabletPeerPtr> DoGetServingTablet(const Key& tablet_id) const;

  template <class Key>
  tablet::TabletPeerPtr DoLookupTablet(const Key& tablet_id) const;

  template <class Key>
  Result<tablet::TabletPeerPtr> DoGetTablet(const Key& tablet_id) const;

  // Same as LookupTablet but doesn't acquired the shared lock.
  template <class Key>
  tablet::TabletPeerPtr LookupTabletUnlocked(const Key& tablet_id) const REQUIRES_SHARED(mutex_);

  void PollWaitingTxnRegistry();

  void FlushDirtySuperblocks();

  // Helper functions to reduce code duplication between RemoteBootstrap and RemoteSnapshotTransfer
  // flows.
  typedef std::unordered_map<std::string, int> RemoteSessionSourceAddresses;
  struct RemoteClients {
    RemoteSessionSourceAddresses source_addresses_;
    int32_t num_clients_ = 0;
  };

  // Registers remote client by incrementing num concurrent clients and adding private_addr to
  // source address map. Proceeds to call CheckStateAndLookupTabletUnlocked and the callback before
  // returning the result of the tablet lookup if successful.
  Result<tablet::TabletPeerPtr> RegisterRemoteClientAndLookupTablet(
      const TabletId& tablet_id, const std::string& private_addr, const std::string& log_prefix,
      RemoteClients* remote_clients,
      std::function<Status()> callback = [] { return Status::OK(); });

  void WaitForRemoteSessionsToEnd(
      TabletRemoteSessionType session_type, const std::string& debug_session_string) const
      EXCLUDES(mutex_);

  void DecrementRemoteSessionCount(const std::string& private_addr, RemoteClients* remote_clients);

  // Checks ClosingUnlocked and then returns the result of LookupTabletUnlocked.
  template <class Key>
  Result<tablet::TabletPeerPtr> CheckStateAndLookupTabletUnlocked(
      const Key& tablet_id, const std::string& log_prefix) const REQUIRES_SHARED(mutex_);

  template <class RemoteClient>
  std::unique_ptr<RemoteClient> InitRemoteClient(
      const std::string& log_prefix, const TabletId& tablet_id, const PeerId& source_uuid,
      const std::string& source_addr, const std::string& debug_session_string);

  const CoarseTimePoint start_time_;

  FsManager* const fs_manager_;

  TabletServer* server_;

  consensus::RaftPeerPB local_peer_pb_;

  using TabletMap = std::unordered_map<
      TabletId, std::shared_ptr<tablet::TabletPeer>, StringHash, std::equal_to<void>>;

  // Lock protecting tablet_map_, dirty_tablets_, state_, tablets_blocked_from_lb_,
  // tablets_being_remote_bootstrapped_, remote_bootstrap_clients_ and snapshot_transfer_clients_.
  mutable RWMutex mutex_;

  // Map from tablet ID to tablet
  TabletMap tablet_map_ GUARDED_BY(mutex_);

  // Map from table ID to count of children in data and wal directories.
  TableDiskAssignmentMap table_data_assignment_map_ GUARDED_BY(dir_assignment_mutex_);
  TableDiskAssignmentMap table_wal_assignment_map_ GUARDED_BY(dir_assignment_mutex_);
  std::unordered_map<std::string, size_t> data_dirs_per_drive_ GUARDED_BY(dir_assignment_mutex_);
  std::unordered_map<std::string, size_t> wal_dirs_per_drive_ GUARDED_BY(dir_assignment_mutex_);
  mutable std::mutex dir_assignment_mutex_;

  // Map of tablet ids -> reason strings where the keys are tablets whose
  // bootstrap, creation, or deletion is in-progress
  TransitionInProgressMap transition_in_progress_ GUARDED_BY(transition_in_progress_mutex_);
  mutable std::mutex transition_in_progress_mutex_;

  // Tablets to include in the next tablet report. When a tablet is added/removed/added
  // locally and needs to be reported to the master, an entry is added to this map.
  // Tablets aren't removed from this Map until the Master acknowledges it in response.
  DirtyMap dirty_tablets_ GUARDED_BY(mutex_);

  typedef std::set<TabletId> TabletIdSet;

  TabletIdSet tablets_being_remote_bootstrapped_ GUARDED_BY(mutex_);

  TabletIdSet tablets_blocked_from_lb_ GUARDED_BY(mutex_);

  // Used to keep track of the number of concurrent remote bootstrap sessions per table.
  std::unordered_map<TableId, TabletIdSet> tablets_being_remote_bootstrapped_per_table_;

  // Next tablet report seqno.
  uint32_t next_report_seq_ GUARDED_BY(mutex_) = 0;

  // Limit on the number of tablets to send in a single report.
  int32_t report_limit_ GUARDED_BY(mutex_) = std::numeric_limits<int32_t>::max();

  MetricRegistry* metric_registry_;

  TSTabletManagerStatePB state_ GUARDED_BY(mutex_);

  // Thread pool used to perform fsync operations corresponding to log::Log of each tablet_peer
  std::unique_ptr<ThreadPool> log_sync_pool_;

  // Thread pool used to open the tablets async, whether bootstrap is required or not.
  std::unique_ptr<ThreadPool> open_tablet_pool_;

  // Thread pool for preparing transactions, shared between all tablets.
  std::unique_ptr<ThreadPool> tablet_prepare_pool_;

  // Thread pool for apply transactions, shared between all tablets.
  std::unique_ptr<ThreadPool> apply_pool_;

  // Thread pool for Raft-related operations, shared between all tablets.
  std::unique_ptr<ThreadPool> raft_pool_;

  // Thread pool for appender threads, shared between all tablets.
  std::unique_ptr<ThreadPool> append_pool_;

  // Thread pool for log allocation threads, shared between all tablets.
  std::unique_ptr<ThreadPool> allocation_pool_;

  // Thread pool for read ops, that are run in parallel, shared between all tablets.
  std::unique_ptr<ThreadPool> read_pool_;

  // Thread pool for flushing retryable requests.
  std::unique_ptr<ThreadPool> flush_retryable_requests_pool_;

  // Thread pool for manually triggering full compactions for tablets, either via schedule
  // of tablets created from a split.
  // This is used by a tablet method to schedule compactions on the child tablets after
  // a split so each tablet has a reference to this pool.
  std::unique_ptr<ThreadPool> full_compaction_pool_;

  // Thread pool for admin triggered compactions for tablets.
  std::unique_ptr<ThreadPool> admin_triggered_compaction_pool_;

  std::unique_ptr<ThreadPool> waiting_txn_pool_;

  std::unique_ptr<rpc::Poller> tablets_cleaner_;

  // Used for verifying tablet data integrity.
  std::unique_ptr<rpc::Poller> verify_tablet_data_poller_;

  // Used for verifying tablet metadata data integrity.
  std::unique_ptr<TabletMetadataValidator> tablet_metadata_validator_;

  // Used for cleaning up old metrics.
  std::unique_ptr<rpc::Poller> metrics_cleaner_;

  std::unique_ptr<docdb::LocalWaitingTxnRegistry> waiting_txn_registry_;

  std::unique_ptr<rpc::Poller> waiting_txn_registry_poller_;

  // For block cache and memory monitor shared across tablets
  tablet::TabletOptions tablet_options_;

  std::unique_ptr<consensus::MultiRaftManager> multi_raft_manager_;

  TabletPeers shutting_down_peers_;

  std::shared_ptr<TabletMemoryManager> mem_manager_;

  RemoteClients remote_bootstrap_clients_ GUARDED_BY(mutex_);
  RemoteClients snapshot_transfer_clients_ GUARDED_BY(mutex_);

  // Gauge to monitor applied split operations.
  scoped_refptr<yb::AtomicGauge<uint64_t>> ts_split_op_apply_;

  // Gauge to monitor the total time to open all tablets' metadata.
  scoped_refptr<yb::AtomicGauge<uint64_t>> ts_open_metadata_time_us_;

  // Gauge to monitor post-split compactions that have been started.
  scoped_refptr<yb::AtomicGauge<uint64_t>> ts_post_split_compaction_added_;

  // Gauge for the count of live tablet peers running on this tserver.
  scoped_refptr<yb::AtomicGauge<uint32_t>> ts_live_tablet_peers_metric_;

  mutable simple_spinlock snapshot_schedule_allowed_history_cutoff_mutex_;
  std::unordered_map<SnapshotScheduleId, HybridTime, SnapshotScheduleIdHash>
      snapshot_schedule_allowed_history_cutoff_
      GUARDED_BY(snapshot_schedule_allowed_history_cutoff_mutex_);
  // Store snapshot schedules that were missing on previous calls to AllowedHistoryCutoff.
  std::unordered_map<SnapshotScheduleId, int64_t, SnapshotScheduleIdHash>
      missing_snapshot_schedules_
      GUARDED_BY(snapshot_schedule_allowed_history_cutoff_mutex_);
  int64_t snapshot_schedules_version_ = 0;
  HybridTime last_restorations_update_ht_;

  // Background task for periodically flushing the superblocks.
  std::unique_ptr<BackgroundTask> superblock_flush_bg_task_;

  std::unique_ptr<FullCompactionManager> full_compaction_manager_;

  std::shared_mutex service_registration_mutex_;
  std::unordered_map<StatefulServiceKind, ConsensusChangeCallback> service_consensus_change_cb_;

  // Metadata cache used by write operations for index requests processing.
  simple_spinlock metadata_cache_spinlock_;
  std::shared_ptr<client::YBMetaDataCache> metadata_cache_holder_;
  std::atomic<client::YBMetaDataCache*> metadata_cache_;

  DISALLOW_COPY_AND_ASSIGN(TSTabletManager);
};

// Helper to delete the transition-in-progress entry from the corresponding set
// when tablet bootstrap, create, and delete operations complete.
class TransitionInProgressDeleter : public RefCountedThreadSafe<TransitionInProgressDeleter> {
 public:
  TransitionInProgressDeleter(TransitionInProgressMap* map, std::mutex* mutex,
                              const TabletId& tablet_id);

 private:
  friend class RefCountedThreadSafe<TransitionInProgressDeleter>;
  ~TransitionInProgressDeleter();

  TransitionInProgressMap* const in_progress_;
  std::mutex* const mutex_;
  const std::string tablet_id_;
};

// Print a log message using the given info and tombstone the specified tablet.
// If tombstoning the tablet fails, a FATAL error is logged, resulting in a crash.
// If ts_manager pointer is passed in, it will unregister from the directory assignment map.
void LogAndTombstone(const scoped_refptr<tablet::RaftGroupMetadata>& meta,
                     const std::string& msg,
                     const std::string& uuid,
                     const Status& s,
                     TSTabletManager* ts_manager = nullptr);

// Delete the tablet using the specified delete_type as the final metadata
// state. Deletes the on-disk data, as well as all WAL segments.
// If ts_manager pointer is passed in, it will unregister from the directory assignment map.
Status DeleteTabletData(const scoped_refptr<tablet::RaftGroupMetadata>& meta,
                        tablet::TabletDataState delete_type,
                        const std::string& uuid,
                        const yb::OpId& last_logged_opid,
                        TSTabletManager* ts_manager = nullptr,
                        FsManager* fs_manager = nullptr);

// Return Status::IllegalState if leader_term < last_logged_term.
// Helper function for use with remote bootstrap.
Status CheckLeaderTermNotLower(const TabletId& tablet_id,
                               const std::string& uuid,
                               int64_t leader_term,
                               int64_t last_logged_term);

// Helper function to replace a stale tablet found from earlier failed tries.
Status HandleReplacingStaleTablet(scoped_refptr<tablet::RaftGroupMetadata> meta,
                                  std::shared_ptr<tablet::TabletPeer> old_tablet_peer,
                                  const TabletId& tablet_id,
                                  const std::string& uuid,
                                  const int64_t& leader_term);

Status ShutdownAndTombstoneTabletPeerNotOk(
    const Status& status, const tablet::TabletPeerPtr& tablet_peer,
    const tablet::RaftGroupMetadataPtr& meta, const std::string& uuid, const char* msg,
    TSTabletManager* ts_tablet_manager = nullptr);

} // namespace tserver
} // namespace yb
