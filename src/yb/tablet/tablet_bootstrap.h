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
#ifndef YB_TABLET_TABLET_BOOTSTRAP_H
#define YB_TABLET_TABLET_BOOTSTRAP_H

#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/consensus/consensus_meta.h"
#include "yb/consensus/opid_util.h"
#include "yb/consensus/log_reader.h"

namespace yb {
namespace tablet {

struct RowOp;
class WriteOperationState;

typedef std::map<int64_t, std::unique_ptr<log::LogEntryPB>> OpIndexToEntryMap;

// State kept during replay.
struct ReplayState {
  explicit ReplayState(const consensus::OpId& last_op_id);

  // Return true if 'b' is allowed to immediately follow 'a' in the log.
  static bool IsValidSequence(const consensus::OpId& a, const consensus::OpId& b);

  // Return a Corruption status if 'id' seems to be out-of-sequence in the log.
  Status CheckSequentialReplicateId(const consensus::ReplicateMsg& msg);

  void UpdateCommittedOpId(const consensus::OpId& id);

  void AddEntriesToStrings(const OpIndexToEntryMap& entries, vector<string>* strings) const;

  void DumpReplayStateToStrings(vector<string>* strings)  const;

  bool CanApply(int64_t index, log::LogEntryPB* entry);

  template<class Handler>
  void ApplyCommittedPendingReplicates(const Handler& handler) {
    auto iter = pending_replicates.begin();
    while (iter != pending_replicates.end() && CanApply(iter->first, iter->second.get())) {
      std::unique_ptr<log::LogEntryPB> entry = std::move(iter->second);
      handler(entry.get());
      iter = pending_replicates.erase(iter);  // erase and advance the iterator (C++11)
      if (rocksdb_applied_index != -1) {
        ++rocksdb_applied_index;
      }
      ++num_entries_applied_to_rocksdb;
    }
  }

  // The last replicate message's ID.
  consensus::OpId prev_op_id = consensus::MinimumOpId();

  // The last operation known to be committed.
  // All other operations with lower IDs are also committed.
  consensus::OpId committed_op_id = consensus::MinimumOpId();

  // For Kudu's columnar tables: REPLICATE log entries whose corresponding COMMIT record has
  // not yet been seen.
  //
  // For YugaByte's RocksDB-backed tables: all REPLICATE entries that have not been applied to
  // RocksDB yet. We decide what entries are safe to apply and delete from this map based on the
  // commit index included into each REPLICATE message.
  //
  // The key in this map is the Raft index.
  OpIndexToEntryMap pending_replicates;

  // COMMIT log entries which couldn't be applied immediately.
  // Not being used for RocksDB-backed tables.
  OpIndexToEntryMap pending_commits;

  // ----------------------------------------------------------------------------------------------
  // State specific to RocksDB-backed tables

  const consensus::OpId last_stored_op_id;

  // Last index applied to RocksDB. This gets incremented as we apply more entries to RocksDB as
  // part of bootstrap.
  int64_t rocksdb_applied_index;

  // Total number of log entries applied to RocksDB.
  int64_t num_entries_applied_to_rocksdb = 0;

  // If we encounter the last entry flushed to a RocksDB SSTable (as identified by the max
  // persistent sequence number), we remember the hybrid time of that entry in this field.
  // We guarantee that we'll either see that entry or a latter entry we know is committed into Raft
  // during log replay. This is crucial for properly setting safe time at bootstrap.
  HybridTime rocksdb_last_entry_hybrid_time = HybridTime::kMin;
};

// Information from the tablet metadata which indicates which data was
// flushed prior to this restart.
//
// We take a snapshot of this information at the beginning of the bootstrap
// process so that we can allow compactions and flushes to run during bootstrap
// without confusing our tracking of flushed stores.
class FlushedStoresSnapshot {
 public:
  FlushedStoresSnapshot() {}
  Status InitFrom(const TabletMetadata& meta);

  bool WasStoreAlreadyFlushed(const MemStoreTargetPB& target) const;

 private:
  int64_t last_durable_mrs_id_;
  unordered_map<int64_t, int64_t> flushed_dms_by_drs_id_;

  DISALLOW_COPY_AND_ASSIGN(FlushedStoresSnapshot);
};

// Bootstraps an existing tablet by opening the metadata from disk, and rebuilding soft
// state by playing log segments. A bootstrapped tablet can then be added to an existing
// consensus configuration as a LEARNER, which will bring its state up to date with the
// rest of the consensus configuration, or it can start serving the data itself, after it
// has been appointed LEADER of that particular consensus configuration.
//
// NOTE: this does not handle pulling data from other replicas in the cluster. That
// is handled by the 'RemoteBootstrap' classes, which copy blocks and metadata locally
// before invoking this local bootstrap functionality.
//
// TODO Because the table that is being rebuilt is never flushed/compacted, consensus
// is only set on the tablet after bootstrap, when we get to flushes/compactions though
// we need to set it before replay or we won't be able to re-rebuild.
class TabletBootstrap {
 public:
  explicit TabletBootstrap(const BootstrapTabletData& data);

  virtual ~TabletBootstrap() {}

  // Plays the log segments, rebuilding the portion of the Tablet's soft
  // state that is present in the log (additional soft state may be present
  // in other replicas).
  // A successful call will yield the rebuilt tablet and the rebuilt log.
  CHECKED_STATUS Bootstrap(std::shared_ptr<TabletClass>* rebuilt_tablet,
                           scoped_refptr<log::Log>* rebuilt_log,
                           consensus::ConsensusBootstrapInfo* results);

 protected:
  // Opens the tablet.
  // Sets '*has_blocks' to true if there was any data on disk for this tablet.
  Status OpenTablet(bool* has_blocks);

  // Checks if a previous log recovery directory exists. If so, it deletes any
  // files in the log dir and sets 'needs_recovery' to true, meaning that the
  // previous recovery attempt should be retried from the recovery dir.
  //
  // Otherwise, if there is a log directory with log files in it, renames that
  // log dir to the log recovery dir and creates a new, empty log dir so that
  // log replay can proceed. 'needs_recovery' is also returned as true in this
  // case.
  //
  // If no log segments are found, 'needs_recovery' is set to false.
  Status PrepareRecoveryDir(bool* needs_recovery);

  // Opens the latest log segments for the Tablet that will allow to rebuild
  // the tablet's soft state. If there are existing log segments in the tablet's
  // log directly they are moved to a "log-recovery" directory which is deleted
  // when the replay process is completed (as they have been duplicated in the
  // current log directory).
  //
  // If a "log-recovery" directory is already present, we will continue to replay
  // from the "log-recovery" directory. Tablet metadata is updated once replay
  // has finished from the "log-recovery" directory.
  Status OpenLogReaderInRecoveryDir();

  // Opens a new log in the tablet's log directory.
  // The directory is expected to be clean.
  Status OpenNewLog();

  // Finishes bootstrap, setting 'rebuilt_log' and 'rebuilt_tablet'.
  Status FinishBootstrap(const string& message,
                         scoped_refptr<log::Log>* rebuilt_log,
                         std::shared_ptr<TabletClass>* rebuilt_tablet);

  // Plays the log segments into the tablet being built.
  // The process of playing the segments generates a new log that can be continued
  // later on when then tablet is rebuilt and starts accepting writes from clients.
  Status PlaySegments(consensus::ConsensusBootstrapInfo* results);

  // Append the given commit message to the log.
  // Does not support writing a TxResult.
  Status AppendCommitMsg(const consensus::CommitMsg& commit_msg);

  Status PlayWriteRequest(consensus::ReplicateMsg* replicate_msg,
                          const consensus::CommitMsg* commit_msg);

  Status PlayUpdateTransactionRequest(consensus::ReplicateMsg* replicate_msg,
                                      const consensus::CommitMsg* commit_msg);

  Status PlayAlterSchemaRequest(consensus::ReplicateMsg* replicate_msg,
                                const consensus::CommitMsg* commit_msg);

  Status PlayChangeConfigRequest(consensus::ReplicateMsg* replicate_msg,
                                 const consensus::CommitMsg* commit_msg);

  Status PlayNoOpRequest(consensus::ReplicateMsg* replicate_msg,
                         const consensus::CommitMsg* commit_msg);

  // Plays operations, skipping those that have already been flushed.
  Status PlayRowOperations(WriteOperationState* operation_state,
                           const TxResultPB* result);

  // Pass through all of the decoded operations in operation_state. For
  // each op:
  // - if it was previously failed, mark as failed
  // - if it previously succeeded but was flushed, mark as skipped
  // - otherwise, re-apply to the tablet being bootstrapped.
  Status FilterAndApplyOperations(WriteOperationState* operation_state,
                                  const TxResultPB* orig_result);

  // Filter a single insert operation, setting it to failed if
  // it was already flushed.
  Status FilterInsert(WriteOperationState* operation_state,
                      RowOp* op,
                      const OperationResultPB* op_result);

  // Filter a single mutate operation, setting it to failed if
  // it was already flushed.
  Status FilterMutate(WriteOperationState* operation_state,
                      RowOp* op,
                      const OperationResultPB* op_result);

  // Returns whether all the stores that are referred to in the commit
  // message are already flushed.
  bool AreAllStoresAlreadyFlushed(const consensus::CommitMsg& commit);

  // Returns whether there is any store that is referred to in the commit
  // message that is already flushed.
  bool AreAnyStoresAlreadyFlushed(const consensus::CommitMsg& commit);

  void DumpReplayStateToLog(const ReplayState& state);

  // Handlers for each type of message seen in the log during replay.
  CHECKED_STATUS HandleEntry(ReplayState* state, std::unique_ptr<log::LogEntryPB>* entry);
  CHECKED_STATUS HandleReplicateMessage(
      ReplayState* state, std::unique_ptr<log::LogEntryPB>* replicate_entry);
  CHECKED_STATUS HandleCommitMessage(
      ReplayState* state, std::unique_ptr<log::LogEntryPB>* commit_entry);
  CHECKED_STATUS ApplyCommitMessage(ReplayState* state, const log::LogEntryPB& commit_entry);
  CHECKED_STATUS HandleEntryPair(
      log::LogEntryPB* replicate_entry, const log::LogEntryPB* commit_entry);
  virtual CHECKED_STATUS HandleOperation(consensus::OperationType op_type,
      consensus::ReplicateMsg* replicate, const consensus::CommitMsg* commit);

  // Checks that an orphaned commit message is actually irrelevant, i.e that the
  // data stores it refers to are already flushed.
  Status CheckOrphanedCommitAlreadyFlushed(const consensus::CommitMsg& commit);

  // Decodes a HybridTime from the provided string and updates the clock
  // with it.
  Status UpdateClock(uint64_t hybrid_time);

  // Removes the recovery directory and all files contained therein.
  // Intended to be invoked after log replay successfully completes.
  Status RemoveRecoveryDir();

  // Return a log prefix string in the standard "T xxx P yyy" format.
  string LogPrefix() const;

  BootstrapTabletData data_;
  scoped_refptr<TabletMetadata> meta_;
  std::shared_ptr<MemTracker> mem_tracker_;
  MetricRegistry* metric_registry_;
  TabletStatusListener* listener_;
  std::unique_ptr<TabletClass> tablet_;
  const scoped_refptr<log::LogAnchorRegistry> log_anchor_registry_;
  scoped_refptr<log::Log> log_;
  gscoped_ptr<log::LogReader> log_reader_;

  gscoped_ptr<consensus::ConsensusMetadata> cmeta_;
  TabletOptions tablet_options_;

  // Statistics on the replay of entries in the log.
  struct Stats {
    Stats()
      : ops_read(0),
        ops_overwritten(0),
        ops_committed(0),
        inserts_seen(0),
        inserts_ignored(0),
        mutations_seen(0),
        mutations_ignored(0),
        orphaned_commits(0) {
    }

    std::string ToString() const;

    // Number of REPLICATE messages read from the log
    int ops_read;
    // Number of REPLICATE messages which were overwritten by later entries.
    int ops_overwritten;
    // Number of REPLICATE messages for which a matching COMMIT was found.
    int ops_committed;

    // Number inserts/mutations seen and ignored.
    int inserts_seen, inserts_ignored;
    int mutations_seen, mutations_ignored;

    // Number of COMMIT messages for which a corresponding REPLICATE was not found.
    int orphaned_commits;
  } stats_;

  // Snapshot of which stores were flushed prior to restart.
  FlushedStoresSnapshot flushed_stores_;

  HybridTime rocksdb_last_entry_hybrid_time_ = HybridTime::kMin;

 private:
  DISALLOW_COPY_AND_ASSIGN(TabletBootstrap);
};

}  // namespace tablet
}  // namespace yb

#endif // YB_TABLET_TABLET_BOOTSTRAP_H
