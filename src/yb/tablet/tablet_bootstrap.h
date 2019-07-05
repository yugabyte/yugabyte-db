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
#include "yb/util/threadpool.h"

namespace yb {
namespace tablet {

struct ReplayState;
class WriteOperationState;

YB_STRONGLY_TYPED_BOOL(AlreadyApplied);

// Bootstraps an existing tablet by opening the metadata from disk, and rebuilding soft state by
// playing log segments. A bootstrapped tablet can then be added to an existing consensus
// configuration as a LEARNER, which will bring its state up to date with the rest of the consensus
// configuration, or it can start serving the data itself, after it has been appointed LEADER of
// that particular consensus configuration.
//
// NOTE: this does not handle pulling data from other replicas in the cluster. That is handled by
// the 'RemoteBootstrap' classes, which copy blocks and metadata locally before invoking this local
// bootstrap functionality.
//
// TODO Because the table that is being rebuilt is never flushed/compacted, consensus is only set on
// the tablet after bootstrap, when we get to flushes/compactions though we need to set it before
// replay or we won't be able to re-rebuild.
class TabletBootstrap {
 public:
  explicit TabletBootstrap(const BootstrapTabletData& data);

  virtual ~TabletBootstrap();

  // Plays the log segments, rebuilding the portion of the Tablet's soft state that is present in
  // the log (additional soft state may be present in other replicas).  A successful call will yield
  // the rebuilt tablet and the rebuilt log.
  CHECKED_STATUS Bootstrap(std::shared_ptr<TabletClass>* rebuilt_tablet,
                           scoped_refptr<log::Log>* rebuilt_log,
                           consensus::ConsensusBootstrapInfo* results);

 protected:
  // Opens the tablet.
  // Sets result to true if there was any data on disk for this tablet.
  virtual Result<bool> OpenTablet();

  // Checks if a previous log recovery directory exists. If so, it deletes any files in the log dir
  // and sets 'needs_recovery' to true, meaning that the previous recovery attempt should be retried
  // from the recovery dir.
  //
  // Otherwise, if there is a log directory with log files in it, renames that log dir to the log
  // recovery dir and creates a new, empty log dir so that log replay can proceed. 'needs_recovery'
  // is also returned as true in this case.
  //
  // If no log segments are found, 'needs_recovery' is set to false.
  CHECKED_STATUS PrepareToReplay(bool* needs_recovery);

  // Opens the latest log segments for the Tablet that will allow to rebuild the tablet's soft
  // state. If there are existing log segments in the tablet's log directly they are moved to a
  // "log-recovery" directory which is deleted when the replay process is completed (as they have
  // been duplicated in the current log directory).
  //
  // If a "log-recovery" directory is already present, we will continue to replay from the
  // "log-recovery" directory. Tablet metadata is updated once replay has finished from the
  // "log-recovery" directory.
  CHECKED_STATUS OpenLogReader();

  // Opens a new log in the tablet's log directory.  The directory is expected to be clean.
  CHECKED_STATUS OpenNewLog();

  // Finishes bootstrap, setting 'rebuilt_log' and 'rebuilt_tablet'.
  CHECKED_STATUS FinishBootstrap(const std::string& message,
                                 scoped_refptr<log::Log>* rebuilt_log,
                                 std::shared_ptr<TabletClass>* rebuilt_tablet);

  // Plays the log segments into the tablet being built.  The process of playing the segments
  // generates a new log that can be continued later on when then tablet is rebuilt and starts
  // accepting writes from clients.
  CHECKED_STATUS PlaySegments(consensus::ConsensusBootstrapInfo* results);

  void PlayWriteRequest(consensus::ReplicateMsg* replicate_msg);

  CHECKED_STATUS PlayUpdateTransactionRequest(
      consensus::ReplicateMsg* replicate_msg, AlreadyApplied already_applied);

  CHECKED_STATUS PlayChangeMetadataRequest(consensus::ReplicateMsg* replicate_msg);

  CHECKED_STATUS PlayChangeConfigRequest(consensus::ReplicateMsg* replicate_msg);

  CHECKED_STATUS PlayNoOpRequest(consensus::ReplicateMsg* replicate_msg);

  CHECKED_STATUS PlayTruncateRequest(consensus::ReplicateMsg* replicate_msg);

  CHECKED_STATUS PlayTabletSnapshotOpRequest(consensus::ReplicateMsg* replicate_msg);

  void DumpReplayStateToLog(const ReplayState& state);

  // Handlers for each type of message seen in the log during replay.
  CHECKED_STATUS HandleEntry(
      yb::log::LogEntryMetadata entry_metadata, ReplayState* state,
      std::unique_ptr<log::LogEntryPB>* entry);
  CHECKED_STATUS HandleReplicateMessage(
      yb::log::LogEntryMetadata entry_metadata, ReplayState* state,
      std::unique_ptr<log::LogEntryPB>* replicate_entry);
  CHECKED_STATUS HandleEntryPair(
      ReplayState* state, log::LogEntryPB* replicate_entry, RestartSafeCoarseTimePoint entry_time);

  virtual CHECKED_STATUS HandleOperation(consensus::OperationType op_type,
                                         consensus::ReplicateMsg* replicate);

  // Decodes a HybridTime from the provided string and updates the clock with it.
  void UpdateClock(uint64_t hybrid_time);

  // Removes the recovery directory and all files contained therein.  Intended to be invoked after
  // log replay successfully completes.
  CHECKED_STATUS RemoveRecoveryDir();

  // Return a log prefix string in the standard "T xxx P yyy" format.
  std::string LogPrefix() const;

  Env* GetEnv();

  BootstrapTabletData data_;
  RaftGroupMetadataPtr meta_;
  std::shared_ptr<MemTracker> mem_tracker_;
  std::shared_ptr<MemTracker> block_based_table_mem_tracker_;
  MetricRegistry* metric_registry_;
  TabletStatusListener* listener_;
  std::unique_ptr<TabletClass> tablet_;
  const scoped_refptr<log::LogAnchorRegistry> log_anchor_registry_;
  scoped_refptr<log::Log> log_;
  std::unique_ptr<log::LogReader> log_reader_;

  std::unique_ptr<consensus::ConsensusMetadata> cmeta_;
  TabletOptions tablet_options_;

  // Thread pool for append task for bootstrap.
  ThreadPool* append_pool_;

  // Statistics on the replay of entries in the log.
  struct Stats {
    Stats()
      : ops_read(0),
        ops_overwritten(0),
        inserts_seen(0),
        inserts_ignored(0),
        mutations_seen(0),
        mutations_ignored(0) {
    }

    std::string ToString() const;

    // Number of REPLICATE messages read from the log
    int ops_read;

    // Number of REPLICATE messages which were overwritten by later entries.
    int ops_overwritten;

    // Number inserts/mutations seen and ignored.
    int inserts_seen, inserts_ignored;
    int mutations_seen, mutations_ignored;
  } stats_;

  HybridTime rocksdb_last_entry_hybrid_time_ = HybridTime::kMin;

  bool skip_wal_rewrite_;

 private:
  DISALLOW_COPY_AND_ASSIGN(TabletBootstrap);
};

}  // namespace tablet
}  // namespace yb

#endif // YB_TABLET_TABLET_BOOTSTRAP_H
