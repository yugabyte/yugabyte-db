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

#include "kudu/tablet/tablet_bootstrap.h"

#include <gflags/gflags.h>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "kudu/common/partial_row.h"
#include "kudu/common/row_operations.h"
#include "kudu/common/wire_protocol.h"
#include "kudu/consensus/consensus_meta.h"
#include "kudu/consensus/log.h"
#include "kudu/consensus/log_anchor_registry.h"
#include "kudu/consensus/log_reader.h"
#include "kudu/consensus/log_util.h"
#include "kudu/consensus/opid_util.h"
#include "kudu/fs/fs_manager.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/strings/strcat.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/gutil/strings/util.h"
#include "kudu/gutil/walltime.h"
#include "kudu/server/clock.h"
#include "kudu/server/hybrid_clock.h"
#include "kudu/server/metadata.h"
#include "kudu/tablet/lock_manager.h"
#include "kudu/tablet/row_op.h"
#include "kudu/tablet/tablet.h"
#include "kudu/tablet/tablet_peer.h"
#include "kudu/tablet/transactions/alter_schema_transaction.h"
#include "kudu/tablet/transactions/write_transaction.h"
#include "kudu/util/debug/trace_event.h"
#include "kudu/util/fault_injection.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/locks.h"
#include "kudu/util/logging.h"
#include "kudu/util/path_util.h"
#include "kudu/util/stopwatch.h"

DEFINE_bool(skip_remove_old_recovery_dir, false,
            "Skip removing WAL recovery dir after startup. (useful for debugging)");
TAG_FLAG(skip_remove_old_recovery_dir, hidden);

DEFINE_double(fault_crash_during_log_replay, 0.0,
              "Fraction of the time when the tablet will crash immediately "
              "after processing a log entry during log replay. "
              "(For testing only!)");
TAG_FLAG(fault_crash_during_log_replay, unsafe);

DECLARE_int32(max_clock_sync_error_usec);

namespace kudu {
namespace tablet {

using boost::shared_lock;
using consensus::ALTER_SCHEMA_OP;
using consensus::CHANGE_CONFIG_OP;
using consensus::ChangeConfigRecordPB;
using consensus::CommitMsg;
using consensus::ConsensusBootstrapInfo;
using consensus::ConsensusMetadata;
using consensus::ConsensusRound;
using consensus::MinimumOpId;
using consensus::NO_OP;
using consensus::OperationType;
using consensus::OperationType_Name;
using consensus::OpId;
using consensus::OpIdEquals;
using consensus::OpIdEqualsFunctor;
using consensus::OpIdHashFunctor;
using consensus::OpIdToString;
using consensus::RaftConfigPB;
using consensus::ReplicateMsg;
using consensus::WRITE_OP;
using log::Log;
using log::LogAnchorRegistry;
using log::LogEntryPB;
using log::LogOptions;
using log::LogReader;
using log::ReadableLogSegment;
using server::Clock;
using std::map;
using std::shared_ptr;
using std::string;
using std::unordered_map;
using strings::Substitute;
using tserver::AlterSchemaRequestPB;
using tserver::WriteRequestPB;

struct ReplayState;

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
  TabletBootstrap(const scoped_refptr<TabletMetadata>& meta,
                  const scoped_refptr<Clock>& clock,
                  shared_ptr<MemTracker> mem_tracker,
                  MetricRegistry* metric_registry,
                  TabletStatusListener* listener,
                  const scoped_refptr<LogAnchorRegistry>& log_anchor_registry);

  // Plays the log segments, rebuilding the portion of the Tablet's soft
  // state that is present in the log (additional soft state may be present
  // in other replicas).
  // A successful call will yield the rebuilt tablet and the rebuilt log.
  Status Bootstrap(shared_ptr<Tablet>* rebuilt_tablet,
                   scoped_refptr<Log>* rebuilt_log,
                   ConsensusBootstrapInfo* results);

 private:

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
                         shared_ptr<Tablet>* rebuilt_tablet);

  // Plays the log segments into the tablet being built.
  // The process of playing the segments generates a new log that can be continued
  // later on when then tablet is rebuilt and starts accepting writes from clients.
  Status PlaySegments(ConsensusBootstrapInfo* results);

  // Append the given commit message to the log.
  // Does not support writing a TxResult.
  Status AppendCommitMsg(const CommitMsg& commit_msg);

  Status PlayWriteRequest(ReplicateMsg* replicate_msg,
                          const CommitMsg& commit_msg);

  Status PlayAlterSchemaRequest(ReplicateMsg* replicate_msg,
                                const CommitMsg& commit_msg);

  Status PlayChangeConfigRequest(ReplicateMsg* replicate_msg,
                                 const CommitMsg& commit_msg);

  Status PlayNoOpRequest(ReplicateMsg* replicate_msg,
                         const CommitMsg& commit_msg);

  // Plays operations, skipping those that have already been flushed.
  Status PlayRowOperations(WriteTransactionState* tx_state,
                           const SchemaPB& schema_pb,
                           const RowOperationsPB& ops_pb,
                           const TxResultPB& result);

  // Pass through all of the decoded operations in tx_state. For
  // each op:
  // - if it was previously failed, mark as failed
  // - if it previously succeeded but was flushed, mark as skipped
  // - otherwise, re-apply to the tablet being bootstrapped.
  Status FilterAndApplyOperations(WriteTransactionState* tx_state,
                                  const TxResultPB& orig_result);

  // Filter a single insert operation, setting it to failed if
  // it was already flushed.
  Status FilterInsert(WriteTransactionState* tx_state,
                      RowOp* op,
                      const OperationResultPB& op_result);

  // Filter a single mutate operation, setting it to failed if
  // it was already flushed.
  Status FilterMutate(WriteTransactionState* tx_state,
                      RowOp* op,
                      const OperationResultPB& op_result);

  // Returns whether all the stores that are referred to in the commit
  // message are already flushed.
  bool AreAllStoresAlreadyFlushed(const CommitMsg& commit);

  // Returns whether there is any store that is referred to in the commit
  // message that is already flushed.
  bool AreAnyStoresAlreadyFlushed(const CommitMsg& commit);

  void DumpReplayStateToLog(const ReplayState& state);

  // Handlers for each type of message seen in the log during replay.
  Status HandleEntry(ReplayState* state, LogEntryPB* entry);
  Status HandleReplicateMessage(ReplayState* state, LogEntryPB* replicate_entry);
  Status HandleCommitMessage(ReplayState* state, LogEntryPB* commit_entry);
  Status ApplyCommitMessage(ReplayState* state, LogEntryPB* commit_entry);
  Status HandleEntryPair(LogEntryPB* replicate_entry, LogEntryPB* commit_entry);

  // Checks that an orphaned commit message is actually irrelevant, i.e that the
  // data stores it refers to are already flushed.
  Status CheckOrphanedCommitAlreadyFlushed(const CommitMsg& commit);

  // Decodes a Timestamp from the provided string and updates the clock
  // with it.
  Status UpdateClock(uint64_t timestamp);

  // Removes the recovery directory and all files contained therein.
  // Intended to be invoked after log replay successfully completes.
  Status RemoveRecoveryDir();

  // Return a log prefix string in the standard "T xxx P yyy" format.
  string LogPrefix() const;

  scoped_refptr<TabletMetadata> meta_;
  scoped_refptr<Clock> clock_;
  shared_ptr<MemTracker> mem_tracker_;
  MetricRegistry* metric_registry_;
  TabletStatusListener* listener_;
  gscoped_ptr<tablet::Tablet> tablet_;
  const scoped_refptr<log::LogAnchorRegistry> log_anchor_registry_;
  scoped_refptr<log::Log> log_;
  gscoped_ptr<log::LogReader> log_reader_;

  Arena arena_;

  gscoped_ptr<ConsensusMetadata> cmeta_;

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

    string ToString() const {
      return Substitute("ops{read=$0 overwritten=$1 applied=$2} "
                        "inserts{seen=$3 ignored=$4} "
                        "mutations{seen=$5 ignored=$6} "
                        "orphaned_commits=$7",
                        ops_read, ops_overwritten, ops_committed,
                        inserts_seen, inserts_ignored,
                        mutations_seen, mutations_ignored,
                        orphaned_commits);
    }

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
  };
  Stats stats_;

  // Snapshot of which stores were flushed prior to restart.
  FlushedStoresSnapshot flushed_stores_;

  DISALLOW_COPY_AND_ASSIGN(TabletBootstrap);
};

TabletStatusListener::TabletStatusListener(const scoped_refptr<TabletMetadata>& meta)
    : meta_(meta),
      last_status_("") {
}

const string TabletStatusListener::tablet_id() const {
  return meta_->tablet_id();
}

const string TabletStatusListener::table_name() const {
  return meta_->table_name();
}

const Partition& TabletStatusListener::partition() const {
  return meta_->partition();
}

const Schema& TabletStatusListener::schema() const {
  return meta_->schema();
}

TabletStatusListener::~TabletStatusListener() {
}

void TabletStatusListener::StatusMessage(const string& status) {
  LOG(INFO) << "T " << tablet_id() << " P " << meta_->fs_manager()->uuid() << ": "
            << status;
  boost::lock_guard<boost::shared_mutex> l(lock_);
  last_status_ = status;
}

Status BootstrapTablet(const scoped_refptr<TabletMetadata>& meta,
                       const scoped_refptr<Clock>& clock,
                       const shared_ptr<MemTracker>& mem_tracker,
                       MetricRegistry* metric_registry,
                       TabletStatusListener* listener,
                       shared_ptr<tablet::Tablet>* rebuilt_tablet,
                       scoped_refptr<log::Log>* rebuilt_log,
                       const scoped_refptr<log::LogAnchorRegistry>& log_anchor_registry,
                       ConsensusBootstrapInfo* consensus_info) {
  TRACE_EVENT1("tablet", "BootstrapTablet",
               "tablet_id", meta->tablet_id());
  TabletBootstrap bootstrap(meta, clock, mem_tracker,
                            metric_registry, listener, log_anchor_registry);
  RETURN_NOT_OK(bootstrap.Bootstrap(rebuilt_tablet, rebuilt_log, consensus_info));
  // This is necessary since OpenNewLog() initially disables sync.
  RETURN_NOT_OK((*rebuilt_log)->ReEnableSyncIfRequired());
  return Status::OK();
}

static string DebugInfo(const string& tablet_id,
                        int segment_seqno,
                        int entry_idx,
                        const string& segment_path,
                        const LogEntryPB& entry) {
  // Truncate the debug string to a reasonable length for logging.
  // Otherwise, glog will truncate for us and we may miss important
  // information which came after this long string.
  string debug_str = entry.ShortDebugString();
  if (debug_str.size() > 500) {
    debug_str.resize(500);
    debug_str.append("...");
  }
  return Substitute("Debug Info: Error playing entry $0 of segment $1 of tablet $2. "
                    "Segment path: $3. Entry: $4", entry_idx, segment_seqno, tablet_id,
                    segment_path, debug_str);
}

TabletBootstrap::TabletBootstrap(
    const scoped_refptr<TabletMetadata>& meta,
    const scoped_refptr<Clock>& clock, shared_ptr<MemTracker> mem_tracker,
    MetricRegistry* metric_registry, TabletStatusListener* listener,
    const scoped_refptr<LogAnchorRegistry>& log_anchor_registry)
    : meta_(meta),
      clock_(clock),
      mem_tracker_(std::move(mem_tracker)),
      metric_registry_(metric_registry),
      listener_(listener),
      log_anchor_registry_(log_anchor_registry),
      arena_(256 * 1024, 4 * 1024 * 1024) {}

Status TabletBootstrap::Bootstrap(shared_ptr<Tablet>* rebuilt_tablet,
                                  scoped_refptr<Log>* rebuilt_log,
                                  ConsensusBootstrapInfo* consensus_info) {
  string tablet_id = meta_->tablet_id();

  // Replay requires a valid Consensus metadata file to exist in order to
  // compare the committed consensus configuration seqno with the log entries and also to persist
  // committed but unpersisted changes.
  RETURN_NOT_OK_PREPEND(ConsensusMetadata::Load(meta_->fs_manager(), tablet_id,
                                                meta_->fs_manager()->uuid(), &cmeta_),
                        "Unable to load Consensus metadata");

  // Make sure we don't try to locally bootstrap a tablet that was in the middle
  // of a remote bootstrap. It's likely that not all files were copied over
  // successfully.
  TabletDataState tablet_data_state = meta_->tablet_data_state();
  if (tablet_data_state != TABLET_DATA_READY) {
    return Status::Corruption("Unable to locally bootstrap tablet " + tablet_id + ": " +
                              "TabletMetadata bootstrap state is " +
                              TabletDataState_Name(tablet_data_state));
  }

  meta_->PinFlush();

  listener_->StatusMessage("Bootstrap starting.");

  if (VLOG_IS_ON(1)) {
    TabletSuperBlockPB super_block;
    RETURN_NOT_OK(meta_->ToSuperBlock(&super_block));
    VLOG_WITH_PREFIX(1) << "Tablet Metadata: " << super_block.DebugString();
  }

  RETURN_NOT_OK(flushed_stores_.InitFrom(*meta_.get()));

  bool has_blocks;
  RETURN_NOT_OK(OpenTablet(&has_blocks));

  bool needs_recovery;
  RETURN_NOT_OK(PrepareRecoveryDir(&needs_recovery));
  if (needs_recovery) {
    RETURN_NOT_OK(OpenLogReaderInRecoveryDir());
  }

  // This is a new tablet, nothing left to do.
  if (!has_blocks && !needs_recovery) {
    LOG_WITH_PREFIX(INFO) << "No blocks or log segments found. Creating new log.";
    RETURN_NOT_OK_PREPEND(OpenNewLog(), "Failed to open new log");
    RETURN_NOT_OK(FinishBootstrap("No bootstrap required, opened a new log",
                                  rebuilt_log,
                                  rebuilt_tablet));
    consensus_info->last_id = MinimumOpId();
    consensus_info->last_committed_id = MinimumOpId();
    return Status::OK();
  }

  // If there were blocks, there must be segments to replay. This is required
  // by Raft, since we always need to know the term and index of the last
  // logged op in order to vote, know how to respond to AppendEntries(), etc.
  if (has_blocks && !needs_recovery) {
    return Status::IllegalState(Substitute("Tablet $0: Found rowsets but no log "
                                           "segments could be found.",
                                           tablet_id));
  }

  // Before playing any segments we set the safe and clean times to 'kMin' so that
  // the MvccManager will accept all transactions that we replay as uncommitted.
  tablet_->mvcc_manager()->OfflineAdjustSafeTime(Timestamp::kMin);
  RETURN_NOT_OK_PREPEND(PlaySegments(consensus_info), "Failed log replay. Reason");

  // Flush the consensus metadata once at the end to persist our changes, if any.
  cmeta_->Flush();

  RETURN_NOT_OK(RemoveRecoveryDir());
  RETURN_NOT_OK(FinishBootstrap("Bootstrap complete.", rebuilt_log, rebuilt_tablet));

  return Status::OK();
}

Status TabletBootstrap::FinishBootstrap(const string& message,
                                        scoped_refptr<log::Log>* rebuilt_log,
                                        shared_ptr<Tablet>* rebuilt_tablet) {
  // Add a callback to TabletMetadata that makes sure that each time we flush the metadata
  // we also wait for in-flights to finish and for their wal entry to be fsynced.
  // This might be a bit conservative in some situations but it will prevent us from
  // ever flushing the metadata referring to tablet data blocks containing data whose
  // commit entries are not durable, a pre-requisite for recovery.
  meta_->SetPreFlushCallback(
      Bind(&FlushInflightsToLogCallback::WaitForInflightsAndFlushLog,
           make_scoped_refptr(new FlushInflightsToLogCallback(tablet_.get(),
                                                              log_))));
  tablet_->MarkFinishedBootstrapping();
  RETURN_NOT_OK(tablet_->metadata()->UnPinFlush());
  listener_->StatusMessage(message);
  rebuilt_tablet->reset(tablet_.release());
  rebuilt_log->swap(log_);
  return Status::OK();
}

Status TabletBootstrap::OpenTablet(bool* has_blocks) {
  gscoped_ptr<Tablet> tablet(new Tablet(meta_,
                                        clock_,
                                        mem_tracker_,
                                        metric_registry_,
                                        log_anchor_registry_));
  // doing nothing for now except opening a tablet locally.
  LOG_TIMING_PREFIX(INFO, LogPrefix(), "opening tablet") {
    RETURN_NOT_OK(tablet->Open());
  }
  *has_blocks = tablet->num_rowsets() != 0;
  tablet_.reset(tablet.release());
  return Status::OK();
}

Status TabletBootstrap::PrepareRecoveryDir(bool* needs_recovery) {
  *needs_recovery = false;

  FsManager* fs_manager = tablet_->metadata()->fs_manager();
  string tablet_id = tablet_->metadata()->tablet_id();
  string log_dir = fs_manager->GetTabletWalDir(tablet_id);

  // If the recovery directory exists, then we crashed mid-recovery.
  // Throw away any logs from the previous recovery attempt and restart the log
  // replay process from the beginning using the same recovery dir as last time.
  string recovery_path = fs_manager->GetTabletWalRecoveryDir(tablet_id);
  if (fs_manager->Exists(recovery_path)) {
    LOG_WITH_PREFIX(INFO) << "Previous recovery directory found at " << recovery_path << ": "
                          << "Replaying log files from this location instead of " << log_dir;

    // Since we have a recovery directory, clear out the log_dir by recursively
    // deleting it and creating a new one so that we don't end up with remnants
    // of old WAL segments or indexes after replay.
    if (fs_manager->env()->FileExists(log_dir)) {
      LOG_WITH_PREFIX(INFO) << "Deleting old log files from previous recovery attempt in "
                            << log_dir;
      RETURN_NOT_OK_PREPEND(fs_manager->env()->DeleteRecursively(log_dir),
                            "Could not recursively delete old log dir " + log_dir);
    }

    RETURN_NOT_OK_PREPEND(fs_manager->CreateDirIfMissing(log_dir),
                          "Failed to create log directory " + log_dir);

    *needs_recovery = true;
    return Status::OK();
  }

  // If we made it here, there was no pre-existing recovery dir.
  // Now we look for log files in log_dir, and if we find any then we rename
  // the whole log_dir to a recovery dir and return needs_recovery = true.
  RETURN_NOT_OK_PREPEND(fs_manager->CreateDirIfMissing(log_dir),
                        "Failed to create log dir");

  vector<string> children;
  RETURN_NOT_OK_PREPEND(fs_manager->ListDir(log_dir, &children),
                        "Couldn't list log segments.");
  for (const string& child : children) {
    if (!log::IsLogFileName(child)) {
      continue;
    }

    string source_path = JoinPathSegments(log_dir, child);
    string dest_path = JoinPathSegments(recovery_path, child);
    LOG_WITH_PREFIX(INFO) << "Will attempt to recover log segment " << source_path
                          << " to " << dest_path;
    *needs_recovery = true;
  }

  if (*needs_recovery) {
    // Atomically rename the log directory to the recovery directory
    // and then re-create the log directory.
    LOG_WITH_PREFIX(INFO) << "Moving log directory " << log_dir << " to recovery directory "
                          << recovery_path << " in preparation for log replay";
    RETURN_NOT_OK_PREPEND(fs_manager->env()->RenameFile(log_dir, recovery_path),
                          Substitute("Could not move log directory $0 to recovery dir $1",
                                     log_dir, recovery_path));
    RETURN_NOT_OK_PREPEND(fs_manager->env()->CreateDir(log_dir),
                          "Failed to recreate log directory " + log_dir);
  }
  return Status::OK();
}

Status TabletBootstrap::OpenLogReaderInRecoveryDir() {
  VLOG_WITH_PREFIX(1) << "Opening log reader in log recovery dir "
                      << meta_->fs_manager()->GetTabletWalRecoveryDir(tablet_->tablet_id());
  // Open the reader.
  RETURN_NOT_OK_PREPEND(LogReader::OpenFromRecoveryDir(tablet_->metadata()->fs_manager(),
                                                       tablet_->metadata()->tablet_id(),
                                                       tablet_->GetMetricEntity().get(),
                                                       &log_reader_),
                        "Could not open LogReader. Reason");
  return Status::OK();
}

Status TabletBootstrap::RemoveRecoveryDir() {
  FsManager* fs_manager = tablet_->metadata()->fs_manager();
  string recovery_path = fs_manager->GetTabletWalRecoveryDir(tablet_->metadata()->tablet_id());
  CHECK(fs_manager->Exists(recovery_path))
      << "Tablet WAL recovery dir " << recovery_path << " does not exist.";

  LOG_WITH_PREFIX(INFO) << "Preparing to delete log recovery files and directory " << recovery_path;

  string tmp_path = Substitute("$0-$1", recovery_path, GetCurrentTimeMicros());
  LOG_WITH_PREFIX(INFO) << "Renaming log recovery dir from "  << recovery_path
                        << " to " << tmp_path;
  RETURN_NOT_OK_PREPEND(fs_manager->env()->RenameFile(recovery_path, tmp_path),
                        Substitute("Could not rename old recovery dir from: $0 to: $1",
                                   recovery_path, tmp_path));

  if (FLAGS_skip_remove_old_recovery_dir) {
    LOG_WITH_PREFIX(INFO) << "--skip_remove_old_recovery_dir enabled. NOT deleting " << tmp_path;
    return Status::OK();
  }
  LOG_WITH_PREFIX(INFO) << "Deleting all files from renamed log recovery directory " << tmp_path;
  RETURN_NOT_OK_PREPEND(fs_manager->env()->DeleteRecursively(tmp_path),
                        "Could not remove renamed recovery dir " + tmp_path);
  LOG_WITH_PREFIX(INFO) << "Completed deletion of old log recovery files and directory "
                        << tmp_path;
  return Status::OK();
}

Status TabletBootstrap::OpenNewLog() {
  OpId init;
  init.set_term(0);
  init.set_index(0);
  RETURN_NOT_OK(Log::Open(LogOptions(),
                          tablet_->metadata()->fs_manager(),
                          tablet_->tablet_id(),
                          *tablet_->schema(),
                          tablet_->metadata()->schema_version(),
                          tablet_->GetMetricEntity(),
                          &log_));
  // Disable sync temporarily in order to speed up appends during the
  // bootstrap process.
  log_->DisableSync();
  return Status::OK();
}

typedef map<int64_t, LogEntryPB*> OpIndexToEntryMap;

// State kept during replay.
struct ReplayState {
  ReplayState()
    : prev_op_id(MinimumOpId()),
      committed_op_id(MinimumOpId()) {
  }

  ~ReplayState() {
    STLDeleteValues(&pending_replicates);
    STLDeleteValues(&pending_commits);
  }

  // Return true if 'b' is allowed to immediately follow 'a' in the log.
  static bool IsValidSequence(const OpId& a, const OpId& b) {
    if (a.term() == 0 && a.index() == 0) {
      // Not initialized - can start with any opid.
      return true;
    }

    // Within the same term, we should never skip entries.
    // We can, however go backwards (see KUDU-783 for an example)
    if (b.term() == a.term() &&
        b.index() > a.index() + 1) {
      return false;
    }

    return true;
  }

  // Return a Corruption status if 'id' seems to be out-of-sequence in the log.
  Status CheckSequentialReplicateId(const ReplicateMsg& msg) {
    DCHECK(msg.has_id());
    if (PREDICT_FALSE(!IsValidSequence(prev_op_id, msg.id()))) {
      string op_desc = Substitute("$0 REPLICATE (Type: $1)",
                                  OpIdToString(msg.id()),
                                  OperationType_Name(msg.op_type()));
      return Status::Corruption(
        Substitute("Unexpected opid following opid $0. Operation: $1",
                   OpIdToString(prev_op_id),
                   op_desc));
    }

    prev_op_id = msg.id();
    return Status::OK();
  }

  void UpdateCommittedOpId(const OpId& id) {
    if (id.index() > committed_op_id.index()) {
      committed_op_id = id;
    }
  }

  void AddEntriesToStrings(const OpIndexToEntryMap& entries, vector<string>* strings) const {
    for (const OpIndexToEntryMap::value_type& map_entry : entries) {
      LogEntryPB* entry = DCHECK_NOTNULL(map_entry.second);
      strings->push_back(Substitute("   $0", entry->ShortDebugString()));
    }
  }

  void DumpReplayStateToStrings(vector<string>* strings)  const {
    strings->push_back(Substitute("ReplayState: Previous OpId: $0, Committed OpId: $1, "
        "Pending Replicates: $2, Pending Commits: $3", OpIdToString(prev_op_id),
        OpIdToString(committed_op_id), pending_replicates.size(), pending_commits.size()));
    if (!pending_replicates.empty()) {
      strings->push_back("Dumping REPLICATES: ");
      AddEntriesToStrings(pending_replicates, strings);
    }
    if (!pending_commits.empty()) {
      strings->push_back("Dumping COMMITS: ");
      AddEntriesToStrings(pending_commits, strings);
    }
  }

  // The last replicate message's ID.
  OpId prev_op_id;

  // The last operation known to be committed.
  // All other operations with lower IDs are also committed.
  OpId committed_op_id;

  // REPLICATE log entries whose corresponding COMMIT record has
  // not yet been seen. Keyed by index.
  OpIndexToEntryMap pending_replicates;

  // COMMIT log entries which couldn't be applied immediately.
  OpIndexToEntryMap pending_commits;
};

// Handle the given log entry. If OK is returned, then takes ownership of 'entry'.
// Otherwise, caller frees.
Status TabletBootstrap::HandleEntry(ReplayState* state, LogEntryPB* entry) {
  if (VLOG_IS_ON(1)) {
    VLOG_WITH_PREFIX(1) << "Handling entry: " << entry->ShortDebugString();
  }

  switch (entry->type()) {
    case log::REPLICATE:
      RETURN_NOT_OK(HandleReplicateMessage(state, entry));
      break;
    case log::COMMIT:
      // check the unpaired ops for the matching replicate msg, abort if not found
      RETURN_NOT_OK(HandleCommitMessage(state, entry));
      break;
    default:
      return Status::Corruption(Substitute("Unexpected log entry type: $0", entry->type()));
  }
  MAYBE_FAULT(FLAGS_fault_crash_during_log_replay);
  return Status::OK();
}

// Takes ownership of 'replicate_entry' on OK status.
Status TabletBootstrap::HandleReplicateMessage(ReplayState* state, LogEntryPB* replicate_entry) {
  stats_.ops_read++;

  const ReplicateMsg& replicate = replicate_entry->replicate();
  RETURN_NOT_OK(state->CheckSequentialReplicateId(replicate));
  DCHECK(replicate.has_timestamp());
  CHECK_OK(UpdateClock(replicate.timestamp()));

  // Append the replicate message to the log as is
  RETURN_NOT_OK(log_->Append(replicate_entry));

  int64_t index = replicate_entry->replicate().id().index();

  LogEntryPB** existing_entry_ptr = InsertOrReturnExisting(
      &state->pending_replicates, index, replicate_entry);

  // If there was a entry with the same index we're overwriting then we need to delete
  // that entry and all entries with higher indexes.
  if (existing_entry_ptr) {
    LogEntryPB* existing_entry = *existing_entry_ptr;

    auto iter = state->pending_replicates.lower_bound(index);
    DCHECK(OpIdEquals((*iter).second->replicate().id(), existing_entry->replicate().id()));

    LogEntryPB* last_entry = (*state->pending_replicates.rbegin()).second;

    LOG_WITH_PREFIX(INFO) << "Overwriting operations starting at: "
                          << existing_entry->replicate().id()
                          << " up to: " << last_entry->replicate().id()
                          << " with operation: " << replicate_entry->replicate().id();

    while (iter != state->pending_replicates.end()) {
      delete (*iter).second;
      state->pending_replicates.erase(iter++);
      stats_.ops_overwritten++;
    }

    InsertOrDie(&state->pending_replicates, index, replicate_entry);
  }
  return Status::OK();
}

// Takes ownership of 'commit_entry' on OK status.
Status TabletBootstrap::HandleCommitMessage(ReplayState* state, LogEntryPB* commit_entry) {
  DCHECK(commit_entry->has_commit()) << "Not a commit message: " << commit_entry->DebugString();

  // Match up the COMMIT record with the original entry that it's applied to.
  const OpId& committed_op_id = commit_entry->commit().commited_op_id();
  state->UpdateCommittedOpId(committed_op_id);

  // If there are no pending replicates, or if this commit's index is lower than the
  // the first pending replicate on record this is likely an orphaned commit.
  if (state->pending_replicates.empty() ||
      (*state->pending_replicates.begin()).first > committed_op_id.index()) {
    VLOG_WITH_PREFIX(2) << "Found orphaned commit for " << committed_op_id;
    RETURN_NOT_OK(CheckOrphanedCommitAlreadyFlushed(commit_entry->commit()));
    stats_.orphaned_commits++;
    delete commit_entry;
    return Status::OK();
  }

  // If this commit does not correspond to the first replicate message in the pending
  // replicates set we keep it to apply later...
  if ((*state->pending_replicates.begin()).first != committed_op_id.index()) {
    if (!ContainsKey(state->pending_replicates, committed_op_id.index())) {
      return Status::Corruption(Substitute("Could not find replicate for commit: $0",
                                           commit_entry->ShortDebugString()));
    }
    VLOG_WITH_PREFIX(2) << "Adding pending commit for " << committed_op_id;
    InsertOrDie(&state->pending_commits, committed_op_id.index(), commit_entry);
    return Status::OK();
  }

  // ... if it does, we apply it and all the commits that immediately follow in the sequence.
  OpId last_applied = commit_entry->commit().commited_op_id();
  RETURN_NOT_OK(ApplyCommitMessage(state, commit_entry));
  delete commit_entry;

  auto iter = state->pending_commits.begin();
  while (iter != state->pending_commits.end()) {
    if ((*iter).first == last_applied.index() + 1) {
      gscoped_ptr<LogEntryPB> buffered_commit_entry((*iter).second);
      state->pending_commits.erase(iter++);
      last_applied = buffered_commit_entry->commit().commited_op_id();
      RETURN_NOT_OK(ApplyCommitMessage(state, buffered_commit_entry.get()));
      continue;
    }
    break;
  }

  return Status::OK();
}

bool TabletBootstrap::AreAllStoresAlreadyFlushed(const CommitMsg& commit) {
  for (const OperationResultPB& op_result : commit.result().ops()) {
    for (const MemStoreTargetPB& mutated_store : op_result.mutated_stores()) {
      if (!flushed_stores_.WasStoreAlreadyFlushed(mutated_store)) {
        return false;
      }
    }
  }
  return true;
}

bool TabletBootstrap::AreAnyStoresAlreadyFlushed(const CommitMsg& commit) {
  for (const OperationResultPB& op_result : commit.result().ops()) {
    for (const MemStoreTargetPB& mutated_store : op_result.mutated_stores()) {
      if (flushed_stores_.WasStoreAlreadyFlushed(mutated_store)) {
        return true;
      }
    }
  }
  return false;
}

Status TabletBootstrap::CheckOrphanedCommitAlreadyFlushed(const CommitMsg& commit) {
  if (!AreAllStoresAlreadyFlushed(commit)) {
    TabletSuperBlockPB super;
    WARN_NOT_OK(meta_->ToSuperBlock(&super), LogPrefix() + "Couldn't build TabletSuperBlockPB");
    return Status::Corruption(Substitute("CommitMsg was orphaned but it referred to "
        "unflushed stores. Commit: $0. TabletMetadata: $1", commit.ShortDebugString(),
        super.ShortDebugString()));
  }
  return Status::OK();
}

Status TabletBootstrap::ApplyCommitMessage(ReplayState* state, LogEntryPB* commit_entry) {

  const OpId& committed_op_id = commit_entry->commit().commited_op_id();
  VLOG_WITH_PREFIX(2) << "Applying commit for " << committed_op_id;
  gscoped_ptr<LogEntryPB> pending_replicate_entry;

  // They should also have an associated replicate index (it may have been in a
  // deleted log segment though).
  pending_replicate_entry.reset(EraseKeyReturnValuePtr(&state->pending_replicates,
                                                       committed_op_id.index()));

  if (pending_replicate_entry != nullptr) {
    // We found a replicate with the same index, make sure it also has the same
    // term.
    if (!OpIdEquals(committed_op_id, pending_replicate_entry->replicate().id())) {
      string error_msg = Substitute("Committed operation's OpId: $0 didn't match the"
          "commit message's committed OpId: $1. Pending operation: $2, Commit message: $3",
          pending_replicate_entry->replicate().id().ShortDebugString(),
          committed_op_id.ShortDebugString(),
          pending_replicate_entry->replicate().ShortDebugString(),
          commit_entry->commit().ShortDebugString());
      LOG_WITH_PREFIX(DFATAL) << error_msg;
      return Status::Corruption(error_msg);
    }
    RETURN_NOT_OK(HandleEntryPair(pending_replicate_entry.get(), commit_entry));
    stats_.ops_committed++;
  } else {
    stats_.orphaned_commits++;
    RETURN_NOT_OK(CheckOrphanedCommitAlreadyFlushed(commit_entry->commit()));
  }

  return Status::OK();
}

// Never deletes 'replicate_entry' or 'commit_entry'.
Status TabletBootstrap::HandleEntryPair(LogEntryPB* replicate_entry, LogEntryPB* commit_entry) {
  const char* error_fmt = "Failed to play $0 request. ReplicateMsg: { $1 }, CommitMsg: { $2 }";

#define RETURN_NOT_OK_REPLAY(ReplayMethodName, replicate, commit) \
  RETURN_NOT_OK_PREPEND(ReplayMethodName(replicate, commit), \
                        Substitute(error_fmt, OperationType_Name(op_type), \
                                   replicate->ShortDebugString(), commit.ShortDebugString()))

  ReplicateMsg* replicate = replicate_entry->mutable_replicate();
  const CommitMsg& commit = commit_entry->commit();
  OperationType op_type = commit.op_type();

  switch (op_type) {
    case WRITE_OP:
      RETURN_NOT_OK_REPLAY(PlayWriteRequest, replicate, commit);
      break;

    case ALTER_SCHEMA_OP:
      RETURN_NOT_OK_REPLAY(PlayAlterSchemaRequest, replicate, commit);
      break;

    case CHANGE_CONFIG_OP:
      RETURN_NOT_OK_REPLAY(PlayChangeConfigRequest, replicate, commit);
      break;

    case NO_OP:
      RETURN_NOT_OK_REPLAY(PlayNoOpRequest, replicate, commit);
      break;

    default:
      return Status::IllegalState(Substitute("Unsupported commit entry type: $0",
                                             commit.op_type()));
  }

#undef RETURN_NOT_OK_REPLAY

  // Non-tablet operations should not advance the safe time, because they are
  // not started serially and so may have timestamps that are out of order.
  if (op_type == NO_OP || op_type == CHANGE_CONFIG_OP) {
    return Status::OK();
  }

  // Handle safe time advancement:
  //
  // If this operation has an external consistency mode other than COMMIT_WAIT, we know that no
  // future transaction will have a timestamp that is lower than it, so we can just advance the
  // safe timestamp to this operation's timestamp.
  //
  // If the hybrid clock is disabled, all transactions will fall into this category.
  Timestamp safe_time;
  if (replicate->write_request().external_consistency_mode() != COMMIT_WAIT) {
    safe_time = Timestamp(replicate->timestamp());
  // ... else we set the safe timestamp to be the transaction's timestamp minus the maximum clock
  // error. This opens the door for problems if the flags changed across reboots, but this is
  // unlikely and the problem would manifest itself immediately and clearly (mvcc would complain
  // the operation is already committed, with a CHECK failure).
  } else {
    DCHECK(clock_->SupportsExternalConsistencyMode(COMMIT_WAIT)) << "The provided clock does not"
        "support COMMIT_WAIT external consistency mode.";
    safe_time = server::HybridClock::AddPhysicalTimeToTimestamp(
        Timestamp(replicate->timestamp()),
        MonoDelta::FromMicroseconds(-FLAGS_max_clock_sync_error_usec));
  }
  tablet_->mvcc_manager()->OfflineAdjustSafeTime(safe_time);

  return Status::OK();
}

void TabletBootstrap::DumpReplayStateToLog(const ReplayState& state) {
  // Dump the replay state, this will log the pending replicates as well as the pending commits,
  // which might be useful for debugging.
  vector<string> state_dump;
  state.DumpReplayStateToStrings(&state_dump);
  for (const string& string : state_dump) {
    LOG_WITH_PREFIX(INFO) << string;
  }
}

Status TabletBootstrap::PlaySegments(ConsensusBootstrapInfo* consensus_info) {
  ReplayState state;
  log::SegmentSequence segments;
  RETURN_NOT_OK(log_reader_->GetSegmentsSnapshot(&segments));

  // The first thing to do is to rewind the tablet's schema back to the schema
  // as of the point in time where the logs begin. We must replay the writes
  // in the logs with the correct point-in-time schema.
  if (!segments.empty()) {
    const scoped_refptr<ReadableLogSegment>& segment = segments[0];
    // Set the point-in-time schema for the tablet based on the log header.
    Schema pit_schema;
    RETURN_NOT_OK_PREPEND(SchemaFromPB(segment->header().schema(), &pit_schema),
                          "Couldn't decode log segment schema");
    RETURN_NOT_OK_PREPEND(tablet_->RewindSchemaForBootstrap(
                              pit_schema, segment->header().schema_version()),
                          "couldn't set point-in-time schema");
  }

  // We defer opening the log until here, so that we properly reproduce the
  // point-in-time schema from the log we're reading into the log we're
  // writing.
  RETURN_NOT_OK_PREPEND(OpenNewLog(), "Failed to open new log");

  int segment_count = 0;
  for (const scoped_refptr<ReadableLogSegment>& segment : segments) {
    vector<LogEntryPB*> entries;
    ElementDeleter deleter(&entries);
    // TODO: Optimize this to not read the whole thing into memory?
    Status read_status = segment->ReadEntries(&entries);
    for (int entry_idx = 0; entry_idx < entries.size(); ++entry_idx) {
      LogEntryPB* entry = entries[entry_idx];
      Status s = HandleEntry(&state, entry);
      if (!s.ok()) {
        DumpReplayStateToLog(state);
        RETURN_NOT_OK_PREPEND(s, DebugInfo(tablet_->tablet_id(),
                                           segment->header().sequence_number(),
                                           entry_idx, segment->path(),
                                           *entry));
      }


      // If HandleEntry returns OK, then it has taken ownership of the entry.
      // So, we have to remove it from the entries vector to avoid it getting
      // freed by ElementDeleter.
      entries[entry_idx] = nullptr;
    }

    // If the LogReader failed to read for some reason, we'll still try to
    // replay as many entries as possible, and then fail with Corruption.
    // TODO: this is sort of scary -- why doesn't LogReader expose an
    // entry-by-entry iterator-like API instead? Seems better to avoid
    // exposing the idea of segments to callers.
    if (PREDICT_FALSE(!read_status.ok())) {
      return Status::Corruption(Substitute("Error reading Log Segment of tablet $0: $1 "
                                           "(Read up to entry $2 of segment $3, in path $4)",
                                           tablet_->tablet_id(),
                                           read_status.ToString(),
                                           entries.size(),
                                           segment->header().sequence_number(),
                                           segment->path()));
    }

    // TODO: could be more granular here and log during the segments as well,
    // plus give info about number of MB processed, but this is better than
    // nothing.
    listener_->StatusMessage(Substitute("Bootstrap replayed $0/$1 log segments. "
                                        "Stats: $2. Pending: $3 replicates",
                                        segment_count + 1, log_reader_->num_segments(),
                                        stats_.ToString(),
                                        state.pending_replicates.size()));
    segment_count++;
  }

  // If we have non-applied commits they all must belong to pending operations and
  // they should only pertain to unflushed stores.
  if (!state.pending_commits.empty()) {
    for (const OpIndexToEntryMap::value_type& entry : state.pending_commits) {
      if (!ContainsKey(state.pending_replicates, entry.first)) {
        DumpReplayStateToLog(state);
        return Status::Corruption("Had orphaned commits at the end of replay.");
      }
      if (AreAnyStoresAlreadyFlushed(entry.second->commit())) {
        DumpReplayStateToLog(state);
        TabletSuperBlockPB super;
        WARN_NOT_OK(meta_->ToSuperBlock(&super), "Couldn't build TabletSuperBlockPB.");
        return Status::Corruption(Substitute("CommitMsg was pending but it referred to "
            "flushed stores. Commit: $0. TabletMetadata: $1",
            entry.second->commit().ShortDebugString(), super.ShortDebugString()));
      }
    }
  }

  // Note that we don't pass the information contained in the pending commits along with
  // ConsensusBootstrapInfo. We know that this is safe as they must refer to unflushed
  // stores (we make doubly sure above).
  //
  // Example/Explanation:
  // Say we have two different operations that touch the same row, one insert and one
  // mutate. Since we use Early Lock Release the commit for the second (mutate) operation
  // might end up in the log before the insert's commit. This wouldn't matter since
  // we replay in order, but a corner case here is that we might crash before we
  // write the commit for the insert, meaning it might not be present at all.
  //
  // One possible log for this situation would be:
  // - Replicate 10.10 (insert)
  // - Replicate 10.11 (mutate)
  // - Commit    10.11 (mutate)
  // ~CRASH while Commit 10.10 is in-flight~
  //
  // We can't replay 10.10 during bootstrap because we haven't seen its commit, but
  // since we can't replay out-of-order we won't replay 10.11 either, in fact we'll
  // pass them both as "pending" to consensus to be applied again.
  //
  // The reason why it is safe to simply disregard 10.11's commit is that we know that
  // it must refer only to unflushed stores. We know this because one important flush/compact
  // pre-condition is:
  // - No flush will become visible on reboot (meaning we won't durably update the tablet
  //   metadata), unless the snapshot under which the flush/compact was performed has no
  //   in-flight transactions and all the messages that are in-flight to the log are durable.
  //
  // In our example this means that if we had flushed/compacted after 10.10 was applied
  // (meaning losing the commit message would lead to corruption as we might re-apply it)
  // then the commit for 10.10 would be durable. Since it isn't then no flush/compaction
  // occurred after 10.10 was applied and thus we can disregard the commit message for
  // 10.11 and simply apply both 10.10 and 10.11 as if we hadn't applied them before.
  //
  // This generalizes to:
  // - If a committed replicate message with index Y is missing a commit message,
  //   no later committed replicate message (with index > Y) is visible across reboots
  //   in the tablet data.

  DumpReplayStateToLog(state);

  // Set up the ConsensusBootstrapInfo structure for the caller.
  for (OpIndexToEntryMap::value_type& e : state.pending_replicates) {
    consensus_info->orphaned_replicates.push_back(e.second->release_replicate());
  }
  consensus_info->last_id = state.prev_op_id;
  consensus_info->last_committed_id = state.committed_op_id;

  return Status::OK();
}

Status TabletBootstrap::AppendCommitMsg(const CommitMsg& commit_msg) {
  LogEntryPB commit_entry;
  commit_entry.set_type(log::COMMIT);
  CommitMsg* commit = commit_entry.mutable_commit();
  commit->CopyFrom(commit_msg);
  return log_->Append(&commit_entry);
}

Status TabletBootstrap::PlayWriteRequest(ReplicateMsg* replicate_msg,
                                         const CommitMsg& commit_msg) {
  DCHECK(replicate_msg->has_timestamp());
  WriteRequestPB* write = replicate_msg->mutable_write_request();

  WriteTransactionState tx_state(nullptr, write, nullptr);
  tx_state.mutable_op_id()->CopyFrom(replicate_msg->id());
  tx_state.set_timestamp(Timestamp(replicate_msg->timestamp()));

  tablet_->StartTransaction(&tx_state);
  tablet_->StartApplying(&tx_state);

  // Use committed OpId for mem store anchoring.
  tx_state.mutable_op_id()->CopyFrom(replicate_msg->id());

  if (write->has_row_operations()) {
    // TODO: get rid of redundant params below - they can be gotten from the Request
    RETURN_NOT_OK(PlayRowOperations(&tx_state,
                                    write->schema(),
                                    write->row_operations(),
                                    commit_msg.result()));
  }

  // Append the commit msg to the log but replace the result with the new one.
  LogEntryPB commit_entry;
  commit_entry.set_type(log::COMMIT);
  CommitMsg* commit = commit_entry.mutable_commit();
  commit->CopyFrom(commit_msg);
  tx_state.ReleaseTxResultPB(commit->mutable_result());
  RETURN_NOT_OK(log_->Append(&commit_entry));

  return Status::OK();
}

Status TabletBootstrap::PlayAlterSchemaRequest(ReplicateMsg* replicate_msg,
                                               const CommitMsg& commit_msg) {
  AlterSchemaRequestPB* alter_schema = replicate_msg->mutable_alter_schema_request();

  // Decode schema
  Schema schema;
  RETURN_NOT_OK(SchemaFromPB(alter_schema->schema(), &schema));

  AlterSchemaTransactionState tx_state(nullptr, alter_schema, nullptr);

  // TODO(KUDU-860): we should somehow distinguish if an alter table failed on its original
  // attempt (e.g due to being an invalid request, or a request with a too-early
  // schema version).

  RETURN_NOT_OK(tablet_->CreatePreparedAlterSchema(&tx_state, &schema));

  // Apply the alter schema to the tablet
  RETURN_NOT_OK_PREPEND(tablet_->AlterSchema(&tx_state), "Failed to AlterSchema:");

  // Also update the log information. Normally, the AlterSchema() call above
  // takes care of this, but our new log isn't hooked up to the tablet yet.
  log_->SetSchemaForNextLogSegment(schema, tx_state.schema_version());

  return AppendCommitMsg(commit_msg);
}

Status TabletBootstrap::PlayChangeConfigRequest(ReplicateMsg* replicate_msg,
                                                const CommitMsg& commit_msg) {
  ChangeConfigRecordPB* change_config = replicate_msg->mutable_change_config_record();
  RaftConfigPB config = change_config->new_config();

  int64_t cmeta_opid_index =  cmeta_->committed_config().opid_index();
  if (replicate_msg->id().index() > cmeta_opid_index) {
    DCHECK(!config.has_opid_index());
    config.set_opid_index(replicate_msg->id().index());
    VLOG_WITH_PREFIX(1) << "WAL replay found Raft configuration with log index "
                        << config.opid_index()
                        << " that is greater than the committed config's index "
                        << cmeta_opid_index
                        << ". Applying this configuration change.";
    cmeta_->set_committed_config(config);
    // We flush once at the end of bootstrap.
  } else {
    VLOG_WITH_PREFIX(1) << "WAL replay found Raft configuration with log index "
                        << replicate_msg->id().index()
                        << ", which is less than or equal to the committed "
                        << "config's index " << cmeta_opid_index << ". "
                        << "Skipping application of this config change.";
  }

  return AppendCommitMsg(commit_msg);
}

Status TabletBootstrap::PlayNoOpRequest(ReplicateMsg* replicate_msg, const CommitMsg& commit_msg) {
  return AppendCommitMsg(commit_msg);
}

Status TabletBootstrap::PlayRowOperations(WriteTransactionState* tx_state,
                                          const SchemaPB& schema_pb,
                                          const RowOperationsPB& ops_pb,
                                          const TxResultPB& result) {
  Schema inserts_schema;
  RETURN_NOT_OK_PREPEND(SchemaFromPB(schema_pb, &inserts_schema),
                        "Couldn't decode client schema");

  arena_.Reset();

  RETURN_NOT_OK_PREPEND(tablet_->DecodeWriteOperations(&inserts_schema, tx_state),
                        Substitute("Could not decode row operations: $0",
                                   ops_pb.ShortDebugString()));
  CHECK_EQ(tx_state->row_ops().size(), result.ops_size());

  // Run AcquireRowLocks, Apply, etc!
  RETURN_NOT_OK_PREPEND(tablet_->AcquireRowLocks(tx_state),
                        "Failed to acquire row locks");

  RETURN_NOT_OK(FilterAndApplyOperations(tx_state, result));

  return Status::OK();
}

Status TabletBootstrap::FilterAndApplyOperations(WriteTransactionState* tx_state,
                                                 const TxResultPB& orig_result) {
  int32_t op_idx = 0;
  for (RowOp* op : tx_state->row_ops()) {
    const OperationResultPB& orig_op_result = orig_result.ops(op_idx++);

    // check if the operation failed in the original transaction
    if (PREDICT_FALSE(orig_op_result.has_failed_status())) {
      Status status = StatusFromPB(orig_op_result.failed_status());
      if (VLOG_IS_ON(1)) {
        VLOG_WITH_PREFIX(1) << "Skipping operation that originally resulted in error. OpId: "
                            << tx_state->op_id().DebugString() << " op index: "
                            << op_idx - 1 << " original error: "
                            << status.ToString();
      }
      op->SetFailed(status);
      continue;
    }

    // Check if it should be filtered out because it's already flushed.
    switch (op->decoded_op.type) {
      case RowOperationsPB::INSERT:
        stats_.inserts_seen++;
        if (!orig_op_result.flushed()) {
          RETURN_NOT_OK(FilterInsert(tx_state, op, orig_op_result));
        } else {
          op->SetAlreadyFlushed();
          stats_.inserts_ignored++;
          continue;
        }
        break;
      case RowOperationsPB::UPDATE:
      case RowOperationsPB::DELETE:
        stats_.mutations_seen++;
        if (!orig_op_result.flushed()) {
          RETURN_NOT_OK(FilterMutate(tx_state, op, orig_op_result));
        } else {
          op->SetAlreadyFlushed();
          stats_.mutations_ignored++;
          continue;
        }
        break;
      default:
        LOG_WITH_PREFIX(FATAL) << "Bad op type: " << op->decoded_op.type;
        break;
    }
    if (op->result != nullptr) {
      continue;
    }

    // Actually apply it.
    tablet_->ApplyRowOperation(tx_state, op);
    DCHECK(op->result != nullptr);

    // We expect that the above Apply() will always succeed, because we're
    // applying an operation that we know succeeded before the server
    // restarted. If it doesn't succeed, something is wrong and we are
    // diverging from our prior state, so bail.
    if (op->result->has_failed_status()) {
      return Status::Corruption("Operation which previously succeeded failed "
                                "during log replay",
                                Substitute("Op: $0\nFailure: $1",
                                           op->ToString(*tablet_->schema()),
                                           op->result->failed_status().ShortDebugString()));
    }
  }
  return Status::OK();
}

Status TabletBootstrap::FilterInsert(WriteTransactionState* tx_state,
                                     RowOp* op,
                                     const OperationResultPB& op_result) {
  DCHECK_EQ(op->decoded_op.type, RowOperationsPB::INSERT);

  if (PREDICT_FALSE(op_result.mutated_stores_size() != 1 ||
                    !op_result.mutated_stores(0).has_mrs_id())) {
    return Status::Corruption(Substitute("Insert operation result must have an mrs_id: $0",
                                         op_result.ShortDebugString()));
  }
  // check if the insert is already flushed
  if (flushed_stores_.WasStoreAlreadyFlushed(op_result.mutated_stores(0))) {
    if (VLOG_IS_ON(1)) {
      VLOG_WITH_PREFIX(1) << "Skipping insert that was already flushed. OpId: "
                          << tx_state->op_id().DebugString()
                          << " flushed to: " << op_result.mutated_stores(0).mrs_id()
                          << " latest durable mrs id: "
                          << tablet_->metadata()->last_durable_mrs_id();
    }

    op->SetAlreadyFlushed();
    stats_.inserts_ignored++;
  }
  return Status::OK();
}

Status TabletBootstrap::FilterMutate(WriteTransactionState* tx_state,
                                     RowOp* op,
                                     const OperationResultPB& op_result) {
  DCHECK(op->decoded_op.type == RowOperationsPB::UPDATE ||
         op->decoded_op.type == RowOperationsPB::DELETE)
    << RowOperationsPB::Type_Name(op->decoded_op.type);

  int num_mutated_stores = op_result.mutated_stores_size();
  if (PREDICT_FALSE(num_mutated_stores == 0 || num_mutated_stores > 2)) {
    return Status::Corruption(Substitute("Mutations must have one or two mutated_stores: $0",
                                         op_result.ShortDebugString()));
  }

  // The mutation may have been duplicated, so we'll check whether any of the
  // output targets was "unflushed".
  int num_unflushed_stores = 0;
  for (const MemStoreTargetPB& mutated_store : op_result.mutated_stores()) {
    if (!flushed_stores_.WasStoreAlreadyFlushed(mutated_store)) {
      num_unflushed_stores++;
    } else {
      if (VLOG_IS_ON(1)) {
        string mutation = op->decoded_op.changelist.ToString(*tablet_->schema());
        VLOG_WITH_PREFIX(1) << "Skipping mutation to " << mutated_store.ShortDebugString()
                            << " that was already flushed. "
                            << "OpId: " << tx_state->op_id().DebugString();
      }
    }
  }

  if (num_unflushed_stores == 0) {
    // The mutation was fully flushed.
    op->SetFailed(Status::AlreadyPresent("Update was already flushed."));
    stats_.mutations_ignored++;
    return Status::OK();
  }

  if (num_unflushed_stores == 2) {
    // 18:47 < dralves> off the top of my head, if we crashed before writing the meta
    //                  at the end of a flush/compation then both mutations could
    //                  potentually be considered unflushed
    // This case is not currently covered by any tests -- we need to add test coverage
    // for this. See KUDU-218. It's likely the correct behavior is just to apply the edit,
    // ie not fatal below.
    LOG_WITH_PREFIX(DFATAL) << "TODO: add test coverage for case where op is unflushed "
                            << "in both duplicated targets";
  }

  return Status::OK();
}

Status TabletBootstrap::UpdateClock(uint64_t timestamp) {
  Timestamp ts;
  RETURN_NOT_OK(ts.FromUint64(timestamp));
  RETURN_NOT_OK(clock_->Update(ts));
  return Status::OK();
}

string TabletBootstrap::LogPrefix() const {
  return Substitute("T $0 P $1: ", meta_->tablet_id(), meta_->fs_manager()->uuid());
}

Status FlushedStoresSnapshot::InitFrom(const TabletMetadata& meta) {
  CHECK(flushed_dms_by_drs_id_.empty()) << "already initted";
  last_durable_mrs_id_ = meta.last_durable_mrs_id();
  for (const shared_ptr<RowSetMetadata>& rsmd : meta.rowsets()) {
    if (!InsertIfNotPresent(&flushed_dms_by_drs_id_, rsmd->id(),
                            rsmd->last_durable_redo_dms_id())) {
      return Status::Corruption(Substitute(
          "Duplicate DRS ID $0 in tablet metadata. "
          "Found DRS $0 with last durable redo DMS ID $1 while trying to "
          "initialize DRS $0 with last durable redo DMS ID $2",
          rsmd->id(),
          flushed_dms_by_drs_id_[rsmd->id()],
          rsmd->last_durable_redo_dms_id()));
    }
  }
  return Status::OK();
}

bool FlushedStoresSnapshot::WasStoreAlreadyFlushed(const MemStoreTargetPB& target) const {
  if (target.has_mrs_id()) {
    DCHECK(!target.has_rs_id());
    DCHECK(!target.has_dms_id());

    // The original mutation went to the MRS. It is flushed if it went to an MRS
    // with a lower ID than the latest flushed one.
    return target.mrs_id() <= last_durable_mrs_id_;
  } else {
    // The original mutation went to a DRS's delta store.
    int64_t last_durable_dms_id;
    if (!FindCopy(flushed_dms_by_drs_id_, target.rs_id(), &last_durable_dms_id)) {
      // if we have no data about this RowSet, then it must have been flushed and
      // then deleted.
      // TODO: how do we avoid a race where we get an update on a rowset before
      // it is persisted? add docs about the ordering of flush.
      return true;
    }

    // If the original rowset that we applied the edit to exists, check whether
    // the edit was in a flushed DMS or a live one.
    if (target.dms_id() <= last_durable_dms_id) {
      return true;
    }

    return false;
  }
}

} // namespace tablet
} // namespace kudu
