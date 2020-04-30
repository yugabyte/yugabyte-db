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
#include "yb/tablet/tablet_bootstrap.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus_util.h"
#include "yb/consensus/log_anchor_registry.h"
#include "yb/consensus/log_reader.h"
#include "yb/consensus/retryable_requests.h"

#include "yb/server/hybrid_clock.h"
#include "yb/tablet/tablet_snapshots.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_splitter.h"
#include "yb/tablet/operations/change_metadata_operation.h"
#include "yb/tablet/operations/history_cutoff_operation.h"
#include "yb/tablet/operations/snapshot_operation.h"
#include "yb/tablet/operations/split_operation.h"
#include "yb/tablet/operations/truncate_operation.h"
#include "yb/tablet/operations/update_txn_operation.h"
#include "yb/tablet/operations/write_operation.h"
#include "yb/util/fault_injection.h"
#include "yb/util/flag_tags.h"
#include "yb/util/opid.h"
#include "yb/util/logging.h"
#include "yb/util/scope_exit.h"
#include "yb/util/stopwatch.h"
#include "yb/util/env_util.h"
#include "yb/consensus/log_index.h"
#include "yb/docdb/consensus_frontier.h"
#include "yb/tserver/backup.pb.h"

DEFINE_bool(skip_remove_old_recovery_dir, false,
            "Skip removing WAL recovery dir after startup. (useful for debugging)");
TAG_FLAG(skip_remove_old_recovery_dir, hidden);

DEFINE_bool(skip_wal_rewrite, true,
            "Skip rewriting WAL files during bootstrap.");
TAG_FLAG(skip_wal_rewrite, experimental);
TAG_FLAG(skip_wal_rewrite, runtime);

DEFINE_test_flag(double, fault_crash_during_log_replay, 0.0,
                 "Fraction of the time when the tablet will crash immediately "
                 "after processing a log entry during log replay.");

DECLARE_uint64(max_clock_sync_error_usec);

DEFINE_bool(force_recover_flushed_frontier, false,
            "Could be used to ignore the flushed frontier metadata from RocksDB manifest and "
            "recover it from the log instead.");
TAG_FLAG(force_recover_flushed_frontier, hidden);
TAG_FLAG(force_recover_flushed_frontier, advanced);

DEFINE_bool(skip_flushed_entries, true,
            "Only replay WAL entries that are not flushed to RocksDB or within the retryable "
            "request timeout.");

DECLARE_int32(retryable_request_timeout_secs);

DEFINE_uint64(transaction_status_tablet_log_segment_size_bytes, 4_MB,
              "The segment size for transaction status tablet log roll-overs, in bytes.");

namespace yb {
namespace tablet {

using namespace std::literals; // NOLINT
using namespace std::placeholders;
using std::shared_ptr;

using log::Log;
using log::LogEntryPB;
using log::LogOptions;
using log::LogReader;
using log::ReadableLogSegment;
using log::LogEntryMetadata;
using log::LogIndex;
using consensus::ChangeConfigRecordPB;
using consensus::RaftConfigPB;
using consensus::ConsensusBootstrapInfo;
using consensus::ConsensusMetadata;
using consensus::MinimumOpId;
using consensus::OpId;
using consensus::OpIdEquals;
using consensus::OpIdToString;
using consensus::ReplicateMsg;
using strings::Substitute;
using tserver::ChangeMetadataRequestPB;
using tserver::TruncateRequestPB;
using tserver::WriteRequestPB;
using tserver::TabletSnapshotOpRequestPB;

static string DebugInfo(const string& tablet_id,
                        int segment_seqno,
                        int entry_idx,
                        const string& segment_path,
                        const LogEntryPB* entry) {
  // Truncate the debug string to a reasonable length for logging.  Otherwise, glog will truncate
  // for us and we may miss important information which came after this long string.
  string debug_str = entry ? entry->ShortDebugString() : "<NULL>"s;
  if (debug_str.size() > 500) {
    debug_str.resize(500);
    debug_str.append("...");
  }
  return Substitute("Debug Info: Error playing entry $0 of segment $1 of tablet $2. "
                    "Segment path: $3. Entry: $4", entry_idx, segment_seqno, tablet_id,
                    segment_path, debug_str);
}

// ============================================================================
//  Class ReplayState.
// ============================================================================
struct Entry {
  std::unique_ptr<log::LogEntryPB> entry;
  RestartSafeCoarseTimePoint entry_time;

  std::string ToString() const {
    return Format("{ entry: $0 entry_time: $1 }", entry, entry_time);
  }
};

typedef std::map<int64_t, Entry> OpIndexToEntryMap;

// State kept during replay.
struct ReplayState {
  ReplayState(const consensus::OpId& regular_op_id, const consensus::OpId& intents_op_id);

  // Return true if 'b' is allowed to immediately follow 'a' in the log.
  static bool IsValidSequence(const consensus::OpId& a, const consensus::OpId& b);

  // Return a Corruption status if 'id' seems to be out-of-sequence in the log.
  Status CheckSequentialReplicateId(const consensus::ReplicateMsg& msg);

  void UpdateCommittedOpId(const consensus::OpId& id);

  // Updates split_op_id. Expects msg to be SPLIT_OP.
  // tablet_id is ID of the tablet being bootstrapped.
  // Return error if it catches inconsistency between split operations.
  CHECKED_STATUS UpdateSplitOpId(const ReplicateMsg& msg, const TabletId& tablet_id);

  void AddEntriesToStrings(
      const OpIndexToEntryMap& entries, std::vector<std::string>* strings) const;

  void DumpReplayStateToStrings(std::vector<std::string>* strings)  const;

  bool CanApply(log::LogEntryPB* entry);

  template<class Handler>
  CHECKED_STATUS ApplyCommittedPendingReplicates(const Handler& handler) {
    auto iter = pending_replicates.begin();
    while (iter != pending_replicates.end() && CanApply(iter->second.entry.get())) {
      RETURN_NOT_OK(handler(iter->second.entry.get(), iter->second.entry_time));
      iter = pending_replicates.erase(iter);  // erase and advance the iterator (C++11)
      ++num_entries_applied_to_rocksdb;
    }
    return Status::OK();
  }

  bool UpdateCommittedFromStored();

  // The last replicate message's ID.
  consensus::OpId prev_op_id = consensus::MinimumOpId();

  // The last operation known to be committed.  All other operations with lower IDs are also
  // committed.
  consensus::OpId committed_op_id = consensus::MinimumOpId();

  // The id of the split operation designated for this tablet added to Raft log.
  // See comments for ReplicateState::split_op_id_.
  consensus::OpId split_op_id;

  // All REPLICATE entries that have not been applied to RocksDB yet. We decide what entries are
  // safe to apply and delete from this map based on the commit index included into each REPLICATE
  // message.
  //
  // The key in this map is the Raft index.
  OpIndexToEntryMap pending_replicates;

  // ----------------------------------------------------------------------------------------------
  // State specific to RocksDB-backed tables

  const consensus::OpId regular_stored_op_id;
  const consensus::OpId intents_stored_op_id;

  // Total number of log entries applied to RocksDB.
  int64_t num_entries_applied_to_rocksdb = 0;

  // If we encounter the last entry flushed to a RocksDB SSTable (as identified by the max
  // persistent sequence number), we remember the hybrid time of that entry in this field.
  // We guarantee that we'll either see that entry or a latter entry we know is committed into Raft
  // during log replay. This is crucial for properly setting safe time at bootstrap.
  HybridTime max_committed_hybrid_time = HybridTime::kMin;
};

ReplayState::ReplayState(const consensus::OpId& regular_op_id, const consensus::OpId& intents_op_id)
    : regular_stored_op_id(regular_op_id), intents_stored_op_id(intents_op_id) {
}

bool ReplayState::UpdateCommittedFromStored() {
  bool result = false;

  if (consensus::OpIdBiggerThan(regular_stored_op_id, committed_op_id)) {
    committed_op_id = regular_stored_op_id;
    result = true;
  }

  if (consensus::OpIdBiggerThan(intents_stored_op_id, committed_op_id)) {
    committed_op_id = intents_stored_op_id;
    result = true;
  }

  return result;
}

// Return true if 'b' is allowed to immediately follow 'a' in the log.
bool ReplayState::IsValidSequence(const OpId& a, const OpId& b) {
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
Status ReplayState::CheckSequentialReplicateId(const ReplicateMsg& msg) {
  DCHECK(msg.has_id());
  if (PREDICT_FALSE(!IsValidSequence(prev_op_id, msg.id()))) {
    string op_desc = Substitute("$0 REPLICATE (Type: $1)",
                                OpIdToString(msg.id()),
                                OperationType_Name(msg.op_type()));
    return STATUS_FORMAT(Corruption,
                         "Unexpected opid following opid $0. Operation: $1",
                         OpIdToString(prev_op_id),
                         op_desc);
  }

  prev_op_id = msg.id();
  return Status::OK();
}

void ReplayState::UpdateCommittedOpId(const OpId& id) {
  if (consensus::OpIdLessThan(committed_op_id, id)) {
    committed_op_id = id;
  }
}

Status ReplayState::UpdateSplitOpId(const ReplicateMsg& msg, const TabletId& tablet_id) {
  SCHECK_EQ(
      msg.op_type(), consensus::SPLIT_OP, IllegalState,
      Format("Unexpected operation $0 instead of SPLIT_OP", msg));
  const auto tablet_id_to_split = msg.split_request().tablet_id();

  if (split_op_id.IsInitialized()) {
    if (tablet_id_to_split == tablet_id) {
      return STATUS_FORMAT(
          IllegalState,
          "There should be at most one SPLIT_OP designated for tablet $0 but we got two: "
          "$1, $2",
          tablet_id, split_op_id, msg.id());
    } else {
      return STATUS_FORMAT(
          IllegalState,
          "Unexpected SPLIT_OP $0 designated for another tablet $1 after we've already "
          "replayed SPLIT_OP $2 for this tablet $3",
          msg.id(), tablet_id_to_split, split_op_id, tablet_id);
    }
  } else {
    if (tablet_id_to_split == tablet_id) {
      // We might be asked to replay SPLIT_OP designated for a different (ancestor) tablet, will
      // just ignore it in this case.
      split_op_id = msg.id();
    }
    return Status::OK();
  }
}

void ReplayState::AddEntriesToStrings(const OpIndexToEntryMap& entries,
                                      std::vector<std::string>* strings) const {
  for (const OpIndexToEntryMap::value_type& map_entry : entries) {
    strings->push_back(Format("   [$0] $1", map_entry.first, map_entry.second.entry.get()));
  }
}

void ReplayState::DumpReplayStateToStrings(std::vector<std::string>* strings)  const {
  strings->push_back(Substitute(
      "ReplayState: "
      "Previous OpId: $0, "
      "Committed OpId: $1, "
      "Pending Replicates: $2, "
      "Flushed Regular: $3, "
      "Flushed Intents: $4",
      OpIdToString(prev_op_id),
      OpIdToString(committed_op_id),
      pending_replicates.size(),
      OpIdToString(regular_stored_op_id),
      OpIdToString(intents_stored_op_id)));
  if (num_entries_applied_to_rocksdb > 0) {
    strings->push_back(Substitute("Log entries applied to RocksDB: $0",
                                  num_entries_applied_to_rocksdb));
  }
  if (!pending_replicates.empty()) {
    strings->push_back(Substitute("Dumping REPLICATES ($0 items):", pending_replicates.size()));
    AddEntriesToStrings(pending_replicates, strings);
  }
}

bool ReplayState::CanApply(LogEntryPB* entry) {
  return consensus::OpIdCompare(entry->replicate().id(), committed_op_id) <= 0;
}

// ============================================================================
//  Class TabletBootstrap.
// ============================================================================
TabletBootstrap::TabletBootstrap(const BootstrapTabletData& data)
    : data_(data),
      meta_(data.tablet_init_data.metadata),
      mem_tracker_(data.tablet_init_data.parent_mem_tracker),
      listener_(data.listener),
      append_pool_(data.append_pool),
      skip_wal_rewrite_(FLAGS_skip_wal_rewrite) {
}

TabletBootstrap::~TabletBootstrap() {}

Status TabletBootstrap::Bootstrap(TabletPtr* rebuilt_tablet,
                                  scoped_refptr<Log>* rebuilt_log,
                                  ConsensusBootstrapInfo* consensus_info) {
  string tablet_id = meta_->raft_group_id();

  // Replay requires a valid Consensus metadata file to exist in order to compare the committed
  // consensus configuration seqno with the log entries and also to persist committed but
  // unpersisted changes.
  RETURN_NOT_OK_PREPEND(ConsensusMetadata::Load(meta_->fs_manager(), tablet_id,
                                                meta_->fs_manager()->uuid(), &cmeta_),
                        "Unable to load Consensus metadata");

  // Make sure we don't try to locally bootstrap a tablet that was in the middle of a remote
  // bootstrap. It's likely that not all files were copied over successfully.
  TabletDataState tablet_data_state = meta_->tablet_data_state();
  if (!CanServeTabletData(tablet_data_state)) {
    return STATUS(Corruption, "Unable to locally bootstrap tablet " + tablet_id + ": " +
                              "RaftGroupMetadata bootstrap state is " +
                              TabletDataState_Name(tablet_data_state));
  }

  listener_->StatusMessage("Bootstrap starting.");

  if (VLOG_IS_ON(1)) {
    RaftGroupReplicaSuperBlockPB super_block;
    meta_->ToSuperBlock(&super_block);
    VLOG_WITH_PREFIX(1) << "Tablet Metadata: " << super_block.DebugString();
  }

  bool has_blocks = VERIFY_RESULT(OpenTablet());

  bool needs_recovery;
  RETURN_NOT_OK(PrepareToReplay(&needs_recovery));
  if (needs_recovery && !skip_wal_rewrite_) {
    RETURN_NOT_OK(OpenLogReader());
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

  // If there were blocks, there must be segments to replay. This is required by Raft, since we
  // always need to know the term and index of the last logged op in order to vote, know how to
  // respond to AppendEntries(), etc.
  if (has_blocks && !needs_recovery) {
    return STATUS(IllegalState, Substitute("Tablet $0: Found rowsets but no log "
                                           "segments could be found.",
                                           tablet_id));
  }

  RETURN_NOT_OK_PREPEND(PlaySegments(consensus_info), "Failed log replay. Reason");

  if (cmeta_->current_term() < consensus_info->last_id.term()) {
    cmeta_->set_current_term(consensus_info->last_id.term());
  }

  // Flush the consensus metadata once at the end to persist our changes, if any.
  RETURN_NOT_OK(cmeta_->Flush());

  RETURN_NOT_OK(RemoveRecoveryDir());

  if (FLAGS_force_recover_flushed_frontier) {
    RETURN_NOT_OK(tablet_->Flush(FlushMode::kSync));
    docdb::ConsensusFrontier new_consensus_frontier;
    new_consensus_frontier.set_op_id(consensus_info->last_committed_id);
    new_consensus_frontier.set_hybrid_time(tablet_->mvcc_manager()->LastReplicatedHybridTime());
    // We don't attempt to recover the history cutoff here because it will be recovered
    // automatically on the first compaction, and this is a special mode for manual troubleshooting.
    LOG_WITH_PREFIX(WARNING)
        << "--force_recover_flushed_frontier specified, forcefully setting "
        << "flushed frontier after bootstrap: " << new_consensus_frontier.ToString();
    RETURN_NOT_OK(tablet_->ModifyFlushedFrontier(
        new_consensus_frontier, rocksdb::FrontierModificationMode::kForce));
  }

  RETURN_NOT_OK(FinishBootstrap("Bootstrap complete.", rebuilt_log, rebuilt_tablet));

  return Status::OK();
}

Status TabletBootstrap::FinishBootstrap(const string& message,
                                        scoped_refptr<log::Log>* rebuilt_log,
                                        TabletPtr* rebuilt_tablet) {
  tablet_->MarkFinishedBootstrapping();
  listener_->StatusMessage(message);
  *rebuilt_tablet = std::move(tablet_);
  rebuilt_log->swap(log_);
  return Status::OK();
}

Result<bool> TabletBootstrap::OpenTablet() {
  CleanupSnapshots();

  auto tablet = std::make_shared<Tablet>(data_.tablet_init_data);
  // Doing nothing for now except opening a tablet locally.
  LOG_TIMING_PREFIX(INFO, LogPrefix(), "opening tablet") {
    RETURN_NOT_OK(tablet->Open());
  }
  Result<bool> has_ss_tables = tablet->HasSSTables();

  // In theory, an error can happen in case of tablet Shutdown or in RocksDB object replacement
  // operation like RestoreSnapshot or Truncate. However, those operations can't really be happening
  // concurrently as we haven't opened the tablet yet.
  RETURN_NOT_OK(has_ss_tables);
  tablet_ = std::move(tablet);
  return has_ss_tables.get();
}

Status TabletBootstrap::PrepareToReplay(bool* needs_recovery) {
  *needs_recovery = false;

  const string& log_dir = tablet_->metadata()->wal_dir();

  // If the recovery directory exists, then we crashed mid-recovery.  Throw away any logs from the
  // previous recovery attempt and restart the log replay process from the beginning using the same
  // recovery dir as last time.
  const string recovery_path = FsManager::GetTabletWalRecoveryDir(log_dir);
  if (GetEnv()->FileExists(recovery_path)) {
    LOG_WITH_PREFIX(INFO) << "Previous recovery directory found at " << recovery_path << ": "
                          << "Replaying log files from this location instead of " << log_dir;

    // Since we have a recovery directory, clear out the log_dir by recursively deleting it and
    // creating a new one so that we don't end up with remnants of old WAL segments or indexes after
    // replay.
    if (GetEnv()->FileExists(log_dir)) {
      LOG_WITH_PREFIX(INFO) << "Deleting old log files from previous recovery attempt in "
                            << log_dir;
      RETURN_NOT_OK_PREPEND(GetEnv()->DeleteRecursively(log_dir),
                            "Could not recursively delete old log dir " + log_dir);
    }

    RETURN_NOT_OK_PREPEND(env_util::CreateDirIfMissing(GetEnv(), DirName(log_dir)),
                          "Failed to create table log directory " + DirName(log_dir));

    RETURN_NOT_OK_PREPEND(env_util::CreateDirIfMissing(GetEnv(), log_dir),
                          "Failed to create tablet log directory " + log_dir);

    *needs_recovery = true;
    return Status::OK();
  }

  // If we made it here, there was no pre-existing recovery dir.  Now we look for log files in
  // log_dir, and if we find any then we rename the whole log_dir to a recovery dir and return
  // needs_recovery = true.
  RETURN_NOT_OK_PREPEND(env_util::CreateDirIfMissing(GetEnv(), DirName(log_dir)),
                        "Failed to create table log directory " + DirName(log_dir));

  RETURN_NOT_OK_PREPEND(env_util::CreateDirIfMissing(GetEnv(), log_dir),
                        "Failed to create tablet log directory " + log_dir);

  vector<string> children;
  RETURN_NOT_OK_PREPEND(GetEnv()->GetChildren(log_dir, &children),
                        "Couldn't list log segments.");
  for (const string& child : children) {
    if (!log::IsLogFileName(child)) {
      continue;
    }

    *needs_recovery = true;
    string source_path = JoinPathSegments(log_dir, child);
    if (skip_wal_rewrite_) {
      LOG_WITH_PREFIX(INFO) << "Will attempt to recover log segment " << source_path;
      continue;
    }

    string dest_path = JoinPathSegments(recovery_path, child);
    LOG_WITH_PREFIX(INFO) << "Will attempt to recover log segment " << source_path
                          << " to " << dest_path;
  }

  if (!skip_wal_rewrite_ && *needs_recovery) {
    // Atomically rename the log directory to the recovery directory and then re-create the log
    // directory.
    LOG_WITH_PREFIX(INFO) << "Moving log directory " << log_dir << " to recovery directory "
                          << recovery_path << " in preparation for log replay";
    RETURN_NOT_OK_PREPEND(GetEnv()->RenameFile(log_dir, recovery_path),
                          Substitute("Could not move log directory $0 to recovery dir $1",
                                     log_dir, recovery_path));
    RETURN_NOT_OK_PREPEND(GetEnv()->CreateDir(log_dir),
                          "Failed to recreate log directory " + log_dir);
  }
  return Status::OK();
}

Status TabletBootstrap::OpenLogReader() {
  auto wal_dir = tablet_->metadata()->wal_dir();
  auto wal_path = skip_wal_rewrite_ ? wal_dir :
      meta_->fs_manager()->GetTabletWalRecoveryDir(wal_dir);
  VLOG_WITH_PREFIX(1) << "Opening log reader in log recovery dir " << wal_path;
  // Open the reader.
  scoped_refptr<LogIndex> index(nullptr);
  RETURN_NOT_OK_PREPEND(LogReader::Open(GetEnv(),
                                        index,
                                        tablet_->metadata()->raft_group_id(),
                                        wal_path,
                                        tablet_->metadata()->fs_manager()->uuid(),
                                        tablet_->GetMetricEntity().get(),
                                        &log_reader_), "Could not open LogReader. Reason");
  return Status::OK();
}

Status TabletBootstrap::RemoveRecoveryDir() {
  const string recovery_path = FsManager::GetTabletWalRecoveryDir(tablet_->metadata()->wal_dir());
  if (!GetEnv()->FileExists(recovery_path)) {
    VLOG(1) << "Tablet WAL recovery dir " << recovery_path << " does not exist.";
    if (!skip_wal_rewrite_) {
      return STATUS(IllegalState, "Expected recovery dir, none found.");
    }
    return Status::OK();
  }

  LOG_WITH_PREFIX(INFO) << "Preparing to delete log recovery files and directory " << recovery_path;

  string tmp_path = Substitute("$0-$1", recovery_path, GetCurrentTimeMicros());
  LOG_WITH_PREFIX(INFO) << "Renaming log recovery dir from "  << recovery_path
                        << " to " << tmp_path;
  RETURN_NOT_OK_PREPEND(GetEnv()->RenameFile(recovery_path, tmp_path),
                        Substitute("Could not rename old recovery dir from: $0 to: $1",
                                   recovery_path, tmp_path));

  if (FLAGS_skip_remove_old_recovery_dir) {
    LOG_WITH_PREFIX(INFO) << "--skip_remove_old_recovery_dir enabled. NOT deleting " << tmp_path;
    return Status::OK();
  }
  LOG_WITH_PREFIX(INFO) << "Deleting all files from renamed log recovery directory " << tmp_path;
  RETURN_NOT_OK_PREPEND(GetEnv()->DeleteRecursively(tmp_path),
                        "Could not remove renamed recovery dir " + tmp_path);
  LOG_WITH_PREFIX(INFO) << "Completed deletion of old log recovery files and directory "
                        << tmp_path;
  return Status::OK();
}

Status TabletBootstrap::OpenNewLog() {
  auto log_options = LogOptions();
  const auto& metadata = *tablet_->metadata();
  log_options.retention_secs = metadata.wal_retention_secs();
  log_options.env = GetEnv();
  if (tablet_->metadata()->table_type() == TableType::TRANSACTION_STATUS_TABLE_TYPE) {
    auto log_segment_size = FLAGS_transaction_status_tablet_log_segment_size_bytes;
    if (log_segment_size) {
      log_options.segment_size_bytes = log_segment_size;
    }
  }
  RETURN_NOT_OK(Log::Open(log_options,
                          tablet_->tablet_id(),
                          metadata.wal_dir(),
                          metadata.fs_manager()->uuid(),
                          *tablet_->schema(),
                          metadata.schema_version(),
                          tablet_->GetMetricEntity(),
                          append_pool_,
                          metadata.cdc_min_replicated_index(),
                          &log_));
  // Disable sync temporarily in order to speed up appends during the bootstrap process.
  log_->DisableSync();
  return Status::OK();
}

// Handle the given log entry. If OK is returned, then takes ownership of 'entry'.  Otherwise,
// caller frees.
Status TabletBootstrap::HandleEntry(
    LogEntryMetadata entry_metadata, std::unique_ptr<LogEntryPB>* entry_ptr) {
  auto& entry = **entry_ptr;
  if (VLOG_IS_ON(1)) {
    VLOG_WITH_PREFIX(1) << "Handling entry: " << entry.ShortDebugString();
  }

  switch (entry.type()) {
    case log::REPLICATE:
      RETURN_NOT_OK(HandleReplicateMessage(entry_metadata, entry_ptr));
      break;
    default:
      return STATUS(Corruption, Substitute("Unexpected log entry type: $0", entry.type()));
  }
  MAYBE_FAULT(FLAGS_fault_crash_during_log_replay);
  return Status::OK();
}

// Takes ownership of 'replicate_entry' on OK status.
Status TabletBootstrap::HandleReplicateMessage(
    LogEntryMetadata entry_metadata, std::unique_ptr<LogEntryPB>* replicate_entry_ptr) {
  auto& replicate_entry = **replicate_entry_ptr;
  stats_.ops_read++;

  const ReplicateMsg& replicate = replicate_entry.replicate();
  RETURN_NOT_OK(replay_state_->CheckSequentialReplicateId(replicate));
  DCHECK(replicate.has_hybrid_time());
  UpdateClock(replicate.hybrid_time());

  // This sets the monotonic counter to at least replicate.monotonic_counter() atomically.
  tablet_->UpdateMonotonicCounter(replicate.monotonic_counter());

  const OpId op_id = replicate_entry.replicate().id();
  if (op_id.index() == replay_state_->regular_stored_op_id.index()) {
    // We need to set the committed OpId to be at least what's been applied to RocksDB. The reason
    // we could not do it before starting log replay is that we don't know the term number of the
    // last write operation flushed into a RocksDB SSTable, even though we know its Raft index
    // (rocksdb_max_persistent_index).
    //
    // In fact, there could be multiple log entries with the Raft index equal to
    // rocksdb_max_persistent_index, but with different terms, in case a higher term's leader
    // "truncated" and overwrote uncommitted log entries from a lower term. Note that such
    // "truncation" only happens in memory, not on disk: the leader keeps appending new entries to
    // the log, but for all intents and purposes we can think of it as of real log truncation.
    //
    // Even in the above case, with index jumping back as entries get overwritten, it is always
    // safe to bump committed_op_id to at least the current OpId here. We will never apply entries
    // from pending_replicates that are not known to be committed after bumping up committed_op_id
    // here, because pending_replicates always contains entries with monotonically increasing
    // consecutive indexes, ending with the current index, which is equal to
    // rocksdb_max_persistent_index, and we only ever apply entries with an index greater than that.
    //
    // Also see the other place where we update state->committed_op_id in the end of this function.
    replay_state_->UpdateCommittedOpId(replicate.id());
  }

  // Append the replicate message to the log as is if we are not skipping wal rewrite. If we are
  // skipping, set consensus_state_only to true.
  RETURN_NOT_OK(log_->Append(replicate_entry_ptr->get(), entry_metadata, skip_wal_rewrite_));

  auto min_index = std::min(
      replay_state_->regular_stored_op_id.index(), replay_state_->intents_stored_op_id.index());
  if (op_id.index() <= min_index) {
    // Do not update the bootstrap in-memory state for log records that have already been applied to
    // RocksDB, or were overwritten by a later entry with a higher term that has already been
    // applied to both regular and provisional record RocksDB.
    replicate_entry_ptr->reset();
    return Status::OK();
  }

  auto iter = replay_state_->pending_replicates.lower_bound(op_id.index());

  // If there was a entry with the same index we're overwriting then we need to delete that entry
  // and all entries with higher indexes.
  if (iter != replay_state_->pending_replicates.end() && iter->first == op_id.index()) {
    auto& existing_entry = iter->second;
    auto& last_entry = replay_state_->pending_replicates.rbegin()->second;

    LOG_WITH_PREFIX(INFO) << "Overwriting operations starting at: "
                          << existing_entry.entry->replicate().id()
                          << " up to: " << last_entry.entry->replicate().id()
                          << " with operation: " << replicate.id();
    stats_.ops_overwritten += std::distance(iter, replay_state_->pending_replicates.end());
    replay_state_->pending_replicates.erase(iter, replay_state_->pending_replicates.end());
  }

  DCHECK(entry_metadata.entry_time != RestartSafeCoarseTimePoint());
  CHECK(replay_state_->pending_replicates.emplace(
      op_id.index(), Entry{std::move(*replicate_entry_ptr), entry_metadata.entry_time}).second);

  CHECK(replicate.has_committed_op_id())
      << "Replicate message has no committed_op_id for table type "
      << TableType_Name(tablet_->table_type()) << ". Replicate message:\n"
      << replicate.DebugString();

  // We include the commit index as of the time a REPLICATE entry was added to the leader's log into
  // that entry. This allows us to decide when we can replay a REPLICATE entry during bootstrap.
  replay_state_->UpdateCommittedOpId(replicate.committed_op_id());

  return replay_state_->ApplyCommittedPendingReplicates(
      std::bind(&TabletBootstrap::HandleEntryPair, this, _1, _2));
}

Status TabletBootstrap::HandleOperation(consensus::OperationType op_type,
                                        ReplicateMsg* replicate) {
  switch (op_type) {
    case consensus::WRITE_OP:
      PlayWriteRequest(replicate);
      return Status::OK();

    case consensus::CHANGE_METADATA_OP:
      return PlayChangeMetadataRequest(replicate);

    case consensus::CHANGE_CONFIG_OP:
      return PlayChangeConfigRequest(replicate);

    case consensus::TRUNCATE_OP:
      return PlayTruncateRequest(replicate);

    case consensus::NO_OP:
      return PlayNoOpRequest(replicate);

    case consensus::UPDATE_TRANSACTION_OP:
      return PlayUpdateTransactionRequest(replicate, AlreadyApplied::kFalse);

    case consensus::SNAPSHOT_OP:
      return PlayTabletSnapshotOpRequest(replicate);

    case consensus::HISTORY_CUTOFF_OP:
      return PlayHistoryCutoffRequest(replicate);

    case consensus::SPLIT_OP:
      return PlaySplitOpRequest(replicate);

    // Unexpected cases:
    case consensus::UNKNOWN_OP:
      return STATUS(IllegalState, Substitute("Unsupported operation type: $0", op_type));
  }

  FATAL_INVALID_ENUM_VALUE(consensus::OperationType, op_type);
}

Status TabletBootstrap::PlayTabletSnapshotOpRequest(ReplicateMsg* replicate_msg) {
  TabletSnapshotOpRequestPB* const snapshot = replicate_msg->mutable_snapshot_request();

  SnapshotOperationState tx_state(tablet_.get(), snapshot);
  tx_state.set_hybrid_time(HybridTime(replicate_msg->hybrid_time()));

  return tx_state.Apply(/* leader_term= */ yb::OpId::kUnknownTerm);
}

Status TabletBootstrap::PlayHistoryCutoffRequest(ReplicateMsg* replicate_msg) {
  HistoryCutoffOperationState state(
      tablet_.get(), replicate_msg->mutable_history_cutoff());

  return state.Replicated(/* leader_term= */ yb::OpId::kUnknownTerm);
}

Status TabletBootstrap::PlaySplitOpRequest(ReplicateMsg* replicate_msg) {
  tserver::SplitTabletRequestPB* const split_request = replicate_msg->mutable_split_request();
  RETURN_NOT_OK(replay_state_->UpdateSplitOpId(*replicate_msg, tablet_->tablet_id()));
  // We might be asked to replay SPLIT_OP even if it was applied and flushed when
  // FLAGS_force_recover_flushed_frontier is set.
  if (split_request->tablet_id() != tablet_->tablet_id()) {
    // Ignore SPLIT_OP designated for ancestor tablet(s).
    return Status::OK();
  } else if (tablet_->metadata()->tablet_data_state() ==
             TabletDataState::TABLET_DATA_SPLIT_COMPLETED) {
    // Ignore SPLIT_OP if tablet has been already split.
    return Status::OK();
  }
  SplitOperationState state(
      tablet_.get(), nullptr /* consensus_for_abort */, data_.tablet_init_data.tablet_splitter,
      split_request);
  return data_.tablet_init_data.tablet_splitter->ApplyTabletSplit(&state);

  // TODO(tsplit): In scope of https://github.com/yugabyte/yugabyte-db/issues/1461 add integration
  // tests for:
  // - tablet bootstrap of original tablet which hasn't been yet split and replaying split
  // operation.
  // - tablet bootstrap of original tablet which has been already successfully split and replaying
  // split operation.
  // - tablet bootstrap of new after-split tablet replaying split operation.
}

// Never deletes 'replicate_entry' or 'commit_entry'.
Status TabletBootstrap::HandleEntryPair(
    LogEntryPB* replicate_entry, RestartSafeCoarseTimePoint entry_time) {
  ReplicateMsg* replicate = replicate_entry->mutable_replicate();
  const auto op_type = replicate_entry->replicate().op_type();

  int64_t flushed_index;
  if (op_type == consensus::UPDATE_TRANSACTION_OP) {
    if (replicate->transaction_state().status() == TransactionStatus::APPLYING) {
      auto index = replicate->id().index();
      if (index <= replay_state_->regular_stored_op_id.index() &&
          index > replay_state_->intents_stored_op_id.index()) {
        // We are in a state when committed intents were applied and flushed to regular DB, but
        // intents store was not flushed.
        return PlayUpdateTransactionRequest(replicate, AlreadyApplied::kTrue);
      }
    }
    flushed_index = replay_state_->regular_stored_op_id.index();
  } else if (op_type == consensus::WRITE_OP &&
             replicate->write_request().write_batch().has_transaction()) {
    flushed_index = replay_state_->intents_stored_op_id.index();
  } else {
    flushed_index = replay_state_->regular_stored_op_id.index();
  }

  if (data_.retryable_requests && replicate->has_write_request()) {
    data_.retryable_requests->Bootstrap(*replicate, entry_time);
  }

  if (replicate->id().index() > flushed_index) {
    const auto status = HandleOperation(op_type, replicate);
    if (!status.ok()) {
      return status.CloneAndAppend(Format(
          "Failed to play $0 request. ReplicateMsg: { $1 }",
          OperationType_Name(op_type), *replicate));
    }
    replay_state_->max_committed_hybrid_time.MakeAtLeast(HybridTime(replicate->hybrid_time()));
  }

  return Status::OK();
}

void TabletBootstrap::DumpReplayStateToLog() {
  // Dump the replay state, this will log the pending replicates, which might be useful for
  // debugging.
  vector<string> state_dump;
  replay_state_->DumpReplayStateToStrings(&state_dump);
  constexpr int kMaxLinesToDump = 1000;
  static_assert(kMaxLinesToDump % 2 == 0, "Expected kMaxLinesToDump to be even");
  if (state_dump.size() <= kMaxLinesToDump) {
    for (const string& line : state_dump) {
      LOG_WITH_PREFIX(INFO) << line;
    }
  } else {
    int i = 0;
    for (const string& line : state_dump) {
      LOG_WITH_PREFIX(INFO) << line;
      if (++i >= kMaxLinesToDump / 2) break;
    }
    LOG_WITH_PREFIX(INFO) << "(" << state_dump.size() - kMaxLinesToDump << " lines skipped)";
    for (i = state_dump.size() - kMaxLinesToDump / 2; i < state_dump.size(); ++i) {
      LOG_WITH_PREFIX(INFO) << state_dump[i];
    }
  }
}

Status TabletBootstrap::PlaySegments(ConsensusBootstrapInfo* consensus_info) {
  auto flushed_op_id = VERIFY_RESULT(tablet_->MaxPersistentOpId());
  if (FLAGS_force_recover_flushed_frontier) {
    LOG_WITH_PREFIX(WARNING)
        << "--force_recover_flushed_frontier specified, ignoring existing flushed frontiers from "
        << "RocksDB metadata (will replay all log records): " << flushed_op_id.ToString();
    flushed_op_id = DocDbOpIds();
  }

  consensus::OpId regular_op_id;
  regular_op_id.set_term(flushed_op_id.regular.term);
  regular_op_id.set_index(flushed_op_id.regular.index);
  consensus::OpId intents_op_id;
  intents_op_id.set_term(flushed_op_id.intents.term);
  intents_op_id.set_index(flushed_op_id.intents.index);
  replay_state_ = std::make_unique<ReplayState>(regular_op_id, intents_op_id);
  replay_state_->max_committed_hybrid_time = VERIFY_RESULT(tablet_->MaxPersistentHybridTime());

  if (FLAGS_force_recover_flushed_frontier) {
    LOG_WITH_PREFIX(WARNING)
        << "--force_recover_flushed_frontier specified, ignoring max committed hybrid time from  "
        << "RocksDB metadata (will replay all log records): "
        << replay_state_->max_committed_hybrid_time;
    replay_state_->max_committed_hybrid_time = HybridTime::kMin;
  } else {
    LOG_WITH_PREFIX(INFO) << "Max persistent index in RocksDB's SSTables before bootstrap: "
                          << replay_state_->regular_stored_op_id.ShortDebugString() << "/"
                          << replay_state_->intents_stored_op_id.ShortDebugString();
  }

  // Open a new log. If skip_wal_rewrite is false, append each replayed entry to this new log.
  // Otherwise, defer appending to this log until bootstrap is finished to preserve the state of
  // old log.
  RETURN_NOT_OK_PREPEND(OpenNewLog(), "Failed to open new log");

  log::SegmentSequence segments;
  RETURN_NOT_OK(log_->GetSegmentsSnapshot(&segments));

  // Find the earliest log segment we need to read, so the rest can be ignored
  auto iter = FLAGS_skip_flushed_entries ? segments.end() : segments.begin();
  if (FLAGS_skip_flushed_entries) {
    // Lower bound on op IDs that need to be replayed
    yb::OpId regular_op_id = yb::OpId::FromPB(replay_state_->regular_stored_op_id);
    yb::OpId intents_op_id = yb::OpId::FromPB(replay_state_->intents_stored_op_id);
    yb::OpId op_id_replay_lowest = regular_op_id;
    if (tablet_->doc_db().intents) {
      op_id_replay_lowest = std::min(regular_op_id,
                                     intents_op_id);
    }
    LOG(INFO) << "Bootstrap optimizer: op_id_replay_lowest=" << op_id_replay_lowest;

    // Time point of the last WAL entry and
    //    how far back in time from it we should retain other entries
    bool read_last_time = false;
    RestartSafeCoarseTimePoint last_time;
    RestartSafeCoarseDuration retain_limit =
        std::chrono::seconds(GetAtomicFlag(&FLAGS_retryable_request_timeout_secs));

    while (iter != segments.begin()) {
      --iter;
      const scoped_refptr <ReadableLogSegment>& segment = *iter;

      auto res = segment->ReadFirstEntryMetadata();
      if (res.ok()) {
        yb::OpId op_id = res->committed_op_id;
        RestartSafeCoarseTimePoint time = res->mono_time;

        // This is the first entry
        if (!read_last_time) {
            last_time = time;
            read_last_time = true;
        }

        // Previous segment would have op_id and time less than required,
        // so we can ignore it.
        if (op_id <= op_id_replay_lowest && time <= last_time - retain_limit) {
          LOG(INFO) << "Bootstrap optimizer, found first mandatory segment op id: " << op_id
                    << ", time: " << time.ToString() << ", last time: " << last_time.ToString()
                    << ", number of segments to be skipped: " << (iter - segments.begin());
          break;
        }
      }
    }
  }

  yb::OpId last_committed_op_id;
  RestartSafeCoarseTimePoint last_entry_time;
  for (; iter != segments.end(); ++iter) {
    const scoped_refptr<ReadableLogSegment>& segment = *iter;

    auto read_result = segment->ReadEntries();
    last_committed_op_id = std::max(last_committed_op_id, read_result.committed_op_id);
    for (int entry_idx = 0; entry_idx < read_result.entries.size(); ++entry_idx) {
      Status s = HandleEntry(
          read_result.entry_metadata[entry_idx], &read_result.entries[entry_idx]);
      if (!s.ok()) {
        LOG(INFO) << "Dumping replay state to log: " << s;
        DumpReplayStateToLog();
        RETURN_NOT_OK_PREPEND(s, DebugInfo(tablet_->tablet_id(),
                                           segment->header().sequence_number(),
                                           entry_idx, segment->path(),
                                           read_result.entries[entry_idx].get()));
      }
    }
    if (!read_result.entry_metadata.empty()) {
      last_entry_time = read_result.entry_metadata.back().entry_time;
    }

    // If the LogReader failed to read for some reason, we'll still try to replay as many entries as
    // possible, and then fail with Corruption.
    // TODO: this is sort of scary -- why doesn't LogReader expose an entry-by-entry iterator-like
    // API instead? Seems better to avoid exposing the idea of segments to callers.
    if (PREDICT_FALSE(!read_result.status.ok())) {
      return STATUS_FORMAT(Corruption,
                           "Error reading Log Segment of tablet $0: $1 "
                               "(Read up to entry $2 of segment $3, in path $4)",
                           tablet_->tablet_id(),
                           read_result.status,
                           read_result.entries.size(),
                           segment->header().sequence_number(),
                           segment->path());
    }

    // TODO: could be more granular here and log during the segments as well, plus give info about
    // number of MB processed, but this is better than nothing.
    auto status = Format(
        "Bootstrap replayed $0/$1 log segments. $2. Pending: $3 replicates. "
            "Last read committed op id: $4",
        (iter - segments.begin()) + 1, segments.size(), stats_,
        replay_state_->pending_replicates.size(), read_result.committed_op_id);
    if (read_result.entry_metadata.empty()) {
      status += ", no entries in last segment";
    } else {
      status += ", last entry metadata: " + read_result.entry_metadata.back().ToString();
    }
    listener_->StatusMessage(status);
  }

  if (replay_state_->UpdateCommittedFromStored()) {
    RETURN_NOT_OK(replay_state_->ApplyCommittedPendingReplicates(
        std::bind(&TabletBootstrap::HandleEntryPair, this, _1, _2)));
  }

  if (last_committed_op_id.index > replay_state_->committed_op_id.index()) {
    auto it = replay_state_->pending_replicates.find(last_committed_op_id.index);
    if (it != replay_state_->pending_replicates.end()) {
      // That should be guaranteed by RAFT protocol. If record is committed, it cannot
      // be overriden by a new leader.
      if (last_committed_op_id.term == it->second.entry->replicate().id().term()) {
        replay_state_->UpdateCommittedOpId(last_committed_op_id.ToPB<consensus::OpId>());
        RETURN_NOT_OK(replay_state_->ApplyCommittedPendingReplicates(
            std::bind(&TabletBootstrap::HandleEntryPair, this, _1, _2)));
      } else {
        LOG_WITH_PREFIX(DFATAL)
            << "Invalid last committed op id: " << last_committed_op_id
            << ", record with this index has another term: " << it->second.entry->replicate().id();
      }
    } else {
      LOG_WITH_PREFIX(DFATAL)
          << "Does not have an entry for the last committed index: " << last_committed_op_id
          << ", entries: " << yb::ToString(replay_state_->pending_replicates);
    }
  }

  LOG_WITH_PREFIX(INFO) << "Dumping replay state to log at the end of " << __FUNCTION__;
  DumpReplayStateToLog();

  // Set up the ConsensusBootstrapInfo structure for the caller.
  for (auto& e : replay_state_->pending_replicates) {
    // We only allow log entries with an index later than the index of the last log entry already
    // applied to RocksDB to be passed to the tablet as "orphaned replicates". This will make sure
    // we don't try to write to RocksDB with non-monotonic sequence ids, but still create
    // ConsensusRound instances for writes that have not been persisted into RocksDB.
    consensus_info->orphaned_replicates.emplace_back(e.second.entry->release_replicate());
  }
  LOG_WITH_PREFIX(INFO)
      << "Number of orphaned replicates: " << consensus_info->orphaned_replicates.size()
      << ", last id: " << replay_state_->prev_op_id
      << ", commited id: " << replay_state_->committed_op_id;
  CHECK(replay_state_->prev_op_id.term() >= replay_state_->committed_op_id.term() &&
        replay_state_->prev_op_id.index() >= replay_state_->committed_op_id.index())
      << "Last: " << replay_state_->prev_op_id.ShortDebugString()
      << ", committed: " << replay_state_->committed_op_id;

  tablet_->mvcc_manager()->SetLastReplicated(replay_state_->max_committed_hybrid_time);
  consensus_info->last_id = replay_state_->prev_op_id;
  consensus_info->last_committed_id = replay_state_->committed_op_id;
  consensus_info->split_op_id = replay_state_->split_op_id;

  if (data_.retryable_requests) {
    data_.retryable_requests->Clock().Adjust(last_entry_time);
  }

  return Status::OK();
}

void TabletBootstrap::PlayWriteRequest(ReplicateMsg* replicate_msg) {
  DCHECK(replicate_msg->has_hybrid_time());

  WriteRequestPB* write = replicate_msg->mutable_write_request();

  DCHECK(write->has_write_batch());

  WriteOperationState operation_state(nullptr, write, nullptr);
  operation_state.mutable_op_id()->CopyFrom(replicate_msg->id());
  HybridTime hybrid_time(replicate_msg->hybrid_time());
  operation_state.set_hybrid_time(hybrid_time);

  tablet_->mvcc_manager()->AddPending(&hybrid_time);

  tablet_->StartOperation(&operation_state);

  // Use committed OpId for mem store anchoring.
  operation_state.mutable_op_id()->CopyFrom(replicate_msg->id());

  WARN_NOT_OK(tablet_->ApplyRowOperations(&operation_state), "ApplyRowOperations failed: ");

  tablet_->mvcc_manager()->Replicated(hybrid_time);
}

Status TabletBootstrap::PlayChangeMetadataRequest(ReplicateMsg* replicate_msg) {
  ChangeMetadataRequestPB* request = replicate_msg->mutable_change_metadata_request();

  // Decode schema
  Schema schema;
  if (request->has_schema()) {
    RETURN_NOT_OK(SchemaFromPB(request->schema(), &schema));
  }

  ChangeMetadataOperationState operation_state(request);

  RETURN_NOT_OK(tablet_->CreatePreparedChangeMetadata(
      &operation_state, request->has_schema() ? &schema : nullptr));

  if (request->has_schema()) {
    // Apply the alter schema to the tablet.
    RETURN_NOT_OK_PREPEND(tablet_->AlterSchema(&operation_state), "Failed to AlterSchema:");

    // Also update the log information. Normally, the AlterSchema() call above takes care of this,
    // but our new log isn't hooked up to the tablet yet.
    log_->SetSchemaForNextLogSegment(schema, operation_state.schema_version());
  }

  if (request->has_wal_retention_secs()) {
    RETURN_NOT_OK_PREPEND(tablet_->AlterWalRetentionSecs(&operation_state),
                          "Failed to alter wal retention secs");
    log_->set_wal_retention_secs(request->wal_retention_secs());
  }

  return Status::OK();
}

Status TabletBootstrap::PlayChangeConfigRequest(ReplicateMsg* replicate_msg) {
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

  return Status::OK();
}

Status TabletBootstrap::PlayNoOpRequest(ReplicateMsg* replicate_msg) {
  return Status::OK();
}

Status TabletBootstrap::PlayTruncateRequest(ReplicateMsg* replicate_msg) {
  TruncateRequestPB* req = replicate_msg->mutable_truncate_request();

  TruncateOperationState operation_state(nullptr, req);

  Status s = tablet_->Truncate(&operation_state);

  RETURN_NOT_OK_PREPEND(s, "Failed to Truncate:");

  return Status::OK();
}

Status TabletBootstrap::PlayUpdateTransactionRequest(
    ReplicateMsg* replicate_msg, AlreadyApplied already_applied) {
  DCHECK(replicate_msg->has_hybrid_time());

  UpdateTxnOperationState operation_state(
      nullptr, replicate_msg->mutable_transaction_state());
  operation_state.mutable_op_id()->CopyFrom(replicate_msg->id());
  HybridTime hybrid_time(replicate_msg->hybrid_time());
  operation_state.set_hybrid_time(hybrid_time);

  tablet_->mvcc_manager()->AddPending(&hybrid_time);
  auto scope_exit = ScopeExit([this, hybrid_time] {
    tablet_->mvcc_manager()->Replicated(hybrid_time);
  });

  auto transaction_participant = tablet_->transaction_participant();
  if (transaction_participant) {
    TransactionParticipant::ReplicatedData replicated_data = {
        .leader_term = yb::OpId::kUnknownTerm,
        .state = *operation_state.request(),
        .op_id = operation_state.op_id(),
        .hybrid_time = operation_state.hybrid_time(),
        .sealed = operation_state.request()->sealed(),
        .already_applied = already_applied
    };
    return transaction_participant->ProcessReplicated(replicated_data);
  } else {
    auto transaction_coordinator = tablet_->transaction_coordinator();
    TransactionCoordinator::ReplicatedData replicated_data = {
        yb::OpId::kUnknownTerm,
        *operation_state.request(),
        operation_state.op_id(),
        operation_state.hybrid_time()
    };
    return transaction_coordinator->ProcessReplicated(replicated_data);
  }
}

void TabletBootstrap::UpdateClock(uint64_t hybrid_time) {
  data_.tablet_init_data.clock->Update(HybridTime(hybrid_time));
}

string TabletBootstrap::LogPrefix() const {
  return consensus::MakeTabletLogPrefix(meta_->raft_group_id(), meta_->fs_manager()->uuid());
}

Env* TabletBootstrap::GetEnv() {
  if (data_.tablet_init_data.tablet_options.env) {
    return data_.tablet_init_data.tablet_options.env;
  }
  return meta_->fs_manager()->env();
}

void TabletBootstrap::CleanupSnapshots() {
  // Disk clean-up: deleting temporary/incomplete snapshots.
  const string top_snapshots_dir = TabletSnapshots::SnapshotsDirName(meta_->rocksdb_dir());

  if (meta_->fs_manager()->env()->FileExists(top_snapshots_dir)) {
    vector<string> snapshot_dirs;
    Status s = meta_->fs_manager()->env()->GetChildren(
        top_snapshots_dir, ExcludeDots::kTrue, &snapshot_dirs);

    if (!s.ok()) {
      LOG(WARNING) << "Cannot get list of snapshot directories in "
                   << top_snapshots_dir << ": " << s;
    } else {
      for (const string& dir_name : snapshot_dirs) {
        const string snapshot_dir = JoinPathSegments(top_snapshots_dir, dir_name);

        if (TabletSnapshots::IsTempSnapshotDir(snapshot_dir)) {
          LOG(INFO) << "Deleting old temporary snapshot directory " << snapshot_dir;

          s = meta_->fs_manager()->env()->DeleteRecursively(snapshot_dir);
          if (!s.ok()) {
            LOG(WARNING) << "Cannot delete old temporary snapshot directory "
                         << snapshot_dir << ": " << s;
          }

          s = meta_->fs_manager()->env()->SyncDir(top_snapshots_dir);
          if (!s.ok()) {
            LOG(WARNING) << "Cannot sync top snapshots dir " << top_snapshots_dir << ": " << s;
          }
        }
      }
    }
  }
}

// ============================================================================
//  Class TabletBootstrap::Stats.
// ============================================================================
string TabletBootstrap::Stats::ToString() const {
  return Format("Read operations: $0, overwritten operations: $1",
                ops_read, ops_overwritten);
}

} // namespace tablet
} // namespace yb
