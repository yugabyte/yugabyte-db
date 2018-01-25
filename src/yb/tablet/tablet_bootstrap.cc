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
#include "yb/consensus/log_anchor_registry.h"
#include "yb/consensus/log_reader.h"
#include "yb/server/hybrid_clock.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/operations/alter_schema_operation.h"
#include "yb/tablet/operations/truncate_operation.h"
#include "yb/tablet/operations/update_txn_operation.h"
#include "yb/tablet/operations/write_operation.h"
#include "yb/util/fault_injection.h"
#include "yb/util/flag_tags.h"
#include "yb/util/opid.h"
#include "yb/util/logging.h"
#include "yb/util/stopwatch.h"

DEFINE_bool(skip_remove_old_recovery_dir, false,
            "Skip removing WAL recovery dir after startup. (useful for debugging)");
TAG_FLAG(skip_remove_old_recovery_dir, hidden);

DEFINE_test_flag(double, fault_crash_during_log_replay, 0.0,
                 "Fraction of the time when the tablet will crash immediately "
                 "after processing a log entry during log replay.");

DECLARE_uint64(max_clock_sync_error_usec);

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
using consensus::ChangeConfigRecordPB;
using consensus::RaftConfigPB;
using consensus::ConsensusBootstrapInfo;
using consensus::ConsensusMetadata;
using consensus::MinimumOpId;
using consensus::OperationType;
using consensus::OpId;
using consensus::OpIdEquals;
using consensus::OpIdToString;
using consensus::ReplicateMsg;
using strings::Substitute;
using tserver::AlterSchemaRequestPB;
using tserver::TruncateRequestPB;
using tserver::WriteRequestPB;

static string DebugInfo(const string& tablet_id,
                        int segment_seqno,
                        int entry_idx,
                        const string& segment_path,
                        const LogEntryPB& entry) {
  // Truncate the debug string to a reasonable length for logging.  Otherwise, glog will truncate
  // for us and we may miss important information which came after this long string.
  string debug_str = entry.ShortDebugString();
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
typedef std::map<int64_t, std::unique_ptr<log::LogEntryPB>> OpIndexToEntryMap;

// State kept during replay.
struct ReplayState {
  explicit ReplayState(const consensus::OpId& last_op_id);

  // Return true if 'b' is allowed to immediately follow 'a' in the log.
  static bool IsValidSequence(const consensus::OpId& a, const consensus::OpId& b);

  // Return a Corruption status if 'id' seems to be out-of-sequence in the log.
  Status CheckSequentialReplicateId(const consensus::ReplicateMsg& msg);

  void UpdateCommittedOpId(const consensus::OpId& id);

  void AddEntriesToStrings(
      const OpIndexToEntryMap& entries, std::vector<std::string>* strings) const;

  void DumpReplayStateToStrings(std::vector<std::string>* strings)  const;

  bool CanApply(log::LogEntryPB* entry);

  template<class Handler>
  void ApplyCommittedPendingReplicates(const Handler& handler) {
    auto iter = pending_replicates.begin();
    while (iter != pending_replicates.end() && CanApply(iter->second.get())) {
      std::unique_ptr<log::LogEntryPB> entry = std::move(iter->second);
      handler(entry.get());
      iter = pending_replicates.erase(iter);  // erase and advance the iterator (C++11)
      ++num_entries_applied_to_rocksdb;
    }
  }

  // The last replicate message's ID.
  consensus::OpId prev_op_id = consensus::MinimumOpId();

  // The last operation known to be committed.  All other operations with lower IDs are also
  // committed.
  consensus::OpId committed_op_id = consensus::MinimumOpId();

  // All REPLICATE entries that have not been applied to RocksDB yet. We decide what entries are
  // safe to apply and delete from this map based on the commit index included into each REPLICATE
  // message.
  //
  // The key in this map is the Raft index.
  OpIndexToEntryMap pending_replicates;

  // ----------------------------------------------------------------------------------------------
  // State specific to RocksDB-backed tables

  const consensus::OpId last_stored_op_id;

  // Total number of log entries applied to RocksDB.
  int64_t num_entries_applied_to_rocksdb = 0;

  // If we encounter the last entry flushed to a RocksDB SSTable (as identified by the max
  // persistent sequence number), we remember the hybrid time of that entry in this field.
  // We guarantee that we'll either see that entry or a latter entry we know is committed into Raft
  // during log replay. This is crucial for properly setting safe time at bootstrap.
  HybridTime rocksdb_last_entry_hybrid_time = HybridTime::kMin;
};

ReplayState::ReplayState(const OpId& last_op_id)
    : last_stored_op_id(last_op_id) {
  // If we know last flushed op id, then initialize committed_op_id with it.
  if (last_op_id.term() > yb::OpId::kUnknownTerm) {
    committed_op_id = last_op_id;
  }
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

void ReplayState::AddEntriesToStrings(const OpIndexToEntryMap& entries,
                                      std::vector<std::string>* strings) const {
  for (const OpIndexToEntryMap::value_type& map_entry : entries) {
    LogEntryPB* entry = DCHECK_NOTNULL(map_entry.second.get());
    strings->push_back(Substitute("   [$0] $1", map_entry.first, entry->ShortDebugString()));
  }
}

void ReplayState::DumpReplayStateToStrings(std::vector<std::string>* strings)  const {
  strings->push_back(Substitute(
      "ReplayState: "
      "Previous OpId: $0, "
      "Committed OpId: $1, "
      "Pending Replicates: $2, "
      "Flushed: $3",
      OpIdToString(prev_op_id),
      OpIdToString(committed_op_id),
      pending_replicates.size(),
      OpIdToString(last_stored_op_id)));
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
      meta_(data.meta),
      mem_tracker_(data.mem_tracker),
      metric_registry_(data.metric_registry),
      listener_(data.listener),
      log_anchor_registry_(data.log_anchor_registry),
      tablet_options_(data.tablet_options) {}

TabletBootstrap::~TabletBootstrap() {}

Status TabletBootstrap::Bootstrap(shared_ptr<TabletClass>* rebuilt_tablet,
                                  scoped_refptr<Log>* rebuilt_log,
                                  ConsensusBootstrapInfo* consensus_info) {
  string tablet_id = meta_->tablet_id();

  // Replay requires a valid Consensus metadata file to exist in order to compare the committed
  // consensus configuration seqno with the log entries and also to persist committed but
  // unpersisted changes.
  RETURN_NOT_OK_PREPEND(ConsensusMetadata::Load(meta_->fs_manager(), tablet_id,
                                                meta_->fs_manager()->uuid(), &cmeta_),
                        "Unable to load Consensus metadata");

  // Make sure we don't try to locally bootstrap a tablet that was in the middle of a remote
  // bootstrap. It's likely that not all files were copied over successfully.
  TabletDataState tablet_data_state = meta_->tablet_data_state();
  if (tablet_data_state != TABLET_DATA_READY) {
    return STATUS(Corruption, "Unable to locally bootstrap tablet " + tablet_id + ": " +
                              "TabletMetadata bootstrap state is " +
                              TabletDataState_Name(tablet_data_state));
  }

  listener_->StatusMessage("Bootstrap starting.");

  if (VLOG_IS_ON(1)) {
    TabletSuperBlockPB super_block;
    RETURN_NOT_OK(meta_->ToSuperBlock(&super_block));
    VLOG_WITH_PREFIX(1) << "Tablet Metadata: " << super_block.DebugString();
  }

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

  // If there were blocks, there must be segments to replay. This is required by Raft, since we
  // always need to know the term and index of the last logged op in order to vote, know how to
  // respond to AppendEntries(), etc.
  if (has_blocks && !needs_recovery) {
    return STATUS(IllegalState, Substitute("Tablet $0: Found rowsets but no log "
                                           "segments could be found.",
                                           tablet_id));
  }

  RETURN_NOT_OK_PREPEND(PlaySegments(consensus_info), "Failed log replay. Reason");

  // Flush the consensus metadata once at the end to persist our changes, if any.
  RETURN_NOT_OK(cmeta_->Flush());

  RETURN_NOT_OK(RemoveRecoveryDir());
  RETURN_NOT_OK(FinishBootstrap("Bootstrap complete.", rebuilt_log, rebuilt_tablet));

  return Status::OK();
}

Status TabletBootstrap::FinishBootstrap(const string& message,
                                        scoped_refptr<log::Log>* rebuilt_log,
                                        shared_ptr<TabletClass>* rebuilt_tablet) {
  tablet_->MarkFinishedBootstrapping();
  listener_->StatusMessage(message);
  rebuilt_tablet->reset(tablet_.release());
  rebuilt_log->swap(log_);
  return Status::OK();
}

Status TabletBootstrap::OpenTablet(bool* has_blocks) {
  auto tablet = std::make_unique<TabletClass>(
      meta_, data_.clock, mem_tracker_, metric_registry_, log_anchor_registry_, tablet_options_,
      data_.transaction_participant_context, data_.transaction_coordinator_context);
  // Doing nothing for now except opening a tablet locally.
  LOG_TIMING_PREFIX(INFO, LogPrefix(), "opening tablet") {
    RETURN_NOT_OK(tablet->Open());
  }
  Result<bool> has_ss_tables = tablet->HasSSTables();

  // In theory, an error can happen in case of tablet Shutdown or in RocksDB object replacement
  // operation like RestoreSnapshot or Truncate. However, those operations can't really be happening
  // concurrently as we haven't opened the tablet yet.
  RETURN_NOT_OK(has_ss_tables);
  *has_blocks = has_ss_tables.get();
  tablet_ = std::move(tablet);
  return Status::OK();
}

Status TabletBootstrap::PrepareRecoveryDir(bool* needs_recovery) {
  *needs_recovery = false;

  FsManager* fs_manager = tablet_->metadata()->fs_manager();
  const string& log_dir = tablet_->metadata()->wal_dir();

  // If the recovery directory exists, then we crashed mid-recovery.  Throw away any logs from the
  // previous recovery attempt and restart the log replay process from the beginning using the same
  // recovery dir as last time.
  const string recovery_path = fs_manager->GetTabletWalRecoveryDir(log_dir);
  if (fs_manager->Exists(recovery_path)) {
    LOG_WITH_PREFIX(INFO) << "Previous recovery directory found at " << recovery_path << ": "
                          << "Replaying log files from this location instead of " << log_dir;

    // Since we have a recovery directory, clear out the log_dir by recursively deleting it and
    // creating a new one so that we don't end up with remnants of old WAL segments or indexes after
    // replay.
    if (fs_manager->env()->FileExists(log_dir)) {
      LOG_WITH_PREFIX(INFO) << "Deleting old log files from previous recovery attempt in "
                            << log_dir;
      RETURN_NOT_OK_PREPEND(fs_manager->env()->DeleteRecursively(log_dir),
                            "Could not recursively delete old log dir " + log_dir);
    }

    RETURN_NOT_OK_PREPEND(fs_manager->CreateDirIfMissing(DirName(log_dir)),
                          "Failed to create table log directory " + DirName(log_dir));

    RETURN_NOT_OK_PREPEND(fs_manager->CreateDirIfMissing(log_dir),
                          "Failed to create tablet log directory " + log_dir);

    *needs_recovery = true;
    return Status::OK();
  }

  // If we made it here, there was no pre-existing recovery dir.  Now we look for log files in
  // log_dir, and if we find any then we rename the whole log_dir to a recovery dir and return
  // needs_recovery = true.
  RETURN_NOT_OK_PREPEND(fs_manager->CreateDirIfMissing(DirName(log_dir)),
                        "Failed to create table log directory " + DirName(log_dir));

  RETURN_NOT_OK_PREPEND(fs_manager->CreateDirIfMissing(log_dir),
                        "Failed to create tablet log directory " + log_dir);

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
    // Atomically rename the log directory to the recovery directory and then re-create the log
    // directory.
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
      << meta_->fs_manager()->GetTabletWalRecoveryDir(tablet_->metadata()->wal_dir());
  // Open the reader.
  RETURN_NOT_OK_PREPEND(LogReader::OpenFromRecoveryDir(tablet_->metadata()->fs_manager(),
                                                       tablet_->metadata()->tablet_id(),
                                                       tablet_->metadata()->wal_dir(),
                                                       tablet_->GetMetricEntity().get(),
                                                       &log_reader_),
                        "Could not open LogReader. Reason");
  return Status::OK();
}

Status TabletBootstrap::RemoveRecoveryDir() {
  FsManager* fs_manager = tablet_->metadata()->fs_manager();
  const string recovery_path = fs_manager->GetTabletWalRecoveryDir(tablet_->metadata()->wal_dir());
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
                          tablet_->metadata()->wal_dir(),
                          *tablet_->schema(),
                          tablet_->metadata()->schema_version(),
                          tablet_->GetMetricEntity(),
                          &log_));
  // Disable sync temporarily in order to speed up appends during the bootstrap process.
  log_->DisableSync();
  return Status::OK();
}

// Handle the given log entry. If OK is returned, then takes ownership of 'entry'.  Otherwise,
// caller frees.
Status TabletBootstrap::HandleEntry(ReplayState* state, std::unique_ptr<LogEntryPB>* entry_ptr) {
  auto& entry = **entry_ptr;
  if (VLOG_IS_ON(1)) {
    VLOG_WITH_PREFIX(1) << "Handling entry: " << entry.ShortDebugString();
  }

  switch (entry.type()) {
    case log::REPLICATE:
      RETURN_NOT_OK(HandleReplicateMessage(state, entry_ptr));
      break;
    default:
      return STATUS(Corruption, Substitute("Unexpected log entry type: $0", entry.type()));
  }
  MAYBE_FAULT(FLAGS_fault_crash_during_log_replay);
  return Status::OK();
}

// Takes ownership of 'replicate_entry' on OK status.
Status TabletBootstrap::HandleReplicateMessage(ReplayState* state,
                                               std::unique_ptr<LogEntryPB>* replicate_entry_ptr) {
  auto& replicate_entry = **replicate_entry_ptr;
  stats_.ops_read++;

  const ReplicateMsg& replicate = replicate_entry.replicate();
  RETURN_NOT_OK(state->CheckSequentialReplicateId(replicate));
  DCHECK(replicate.has_hybrid_time());
  UpdateClock(replicate.hybrid_time());

  // This sets the monotonic counter to at least replicate.monotonic_counter() atomically.
  tablet_->UpdateMonotonicCounter(replicate.monotonic_counter());

  const OpId op_id = replicate_entry.replicate().id();
  if (op_id.index() == state->last_stored_op_id.index()) {
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
    state->UpdateCommittedOpId(replicate.id());

    // We also update the MVCC safe time to make sure this committed entry is visible to readers
    // as every committed entry should be. Unlike the committed op id, though, we can't just update
    // the safe time here, as this entry could be overwritten by a later entry with a later term but
    // an earlier hybrid time (TODO: would that still be possible when we have leader leases?).
    // Instead, we only keep the last value of hybrid time of the entry at this index, and update
    // safe time based on it in the end. We do require that we keep at least one committed entry
    // in the log, though.
    state->rocksdb_last_entry_hybrid_time = HybridTime(replicate.hybrid_time());
  }

  // Append the replicate message to the log as is
  RETURN_NOT_OK(log_->Append(replicate_entry_ptr->get()));

  if (op_id.index() <= state->last_stored_op_id.index()) {
    // Do not update the bootstrap in-memory state for log records that have already been applied to
    // RocksDB, or were overwritten by a later entry with a higher term that has already been
    // applied to RocksDB.
    replicate_entry_ptr->reset();
    return Status::OK();
  }

  auto iter = state->pending_replicates.lower_bound(op_id.index());

  // If there was a entry with the same index we're overwriting then we need to delete that entry
  // and all entries with higher indexes.
  if (iter != state->pending_replicates.end() && iter->first == op_id.index()) {
    auto& existing_entry = iter->second;
    auto& last_entry = state->pending_replicates.rbegin()->second;

    LOG_WITH_PREFIX(INFO) << "Overwriting operations starting at: "
                          << existing_entry->replicate().id()
                          << " up to: " << last_entry->replicate().id()
                          << " with operation: " << replicate.id();
    stats_.ops_overwritten += std::distance(iter, state->pending_replicates.end());
    state->pending_replicates.erase(iter, state->pending_replicates.end());
  }

  CHECK(state->pending_replicates.emplace(op_id.index(), std::move(*replicate_entry_ptr)).second);

  CHECK(replicate.has_committed_op_id())
      << "Replicate message has no committed_op_id for table type "
      << TableType_Name(tablet_->table_type()) << ". Replicate message:\n"
      << replicate.DebugString();

  // We include the commit index as of the time a REPLICATE entry was added to the leader's log into
  // that entry. This allows us to decide when we can replay a REPLICATE entry during bootstrap.
  state->UpdateCommittedOpId(replicate.committed_op_id());

  state->ApplyCommittedPendingReplicates(std::bind(&TabletBootstrap::HandleEntryPair, this, _1));

  return Status::OK();
}

Status TabletBootstrap::HandleOperation(OperationType op_type,
                                        ReplicateMsg* replicate) {
  switch (op_type) {
    case consensus::WRITE_OP:
      PlayWriteRequest(replicate);
      return Status::OK();

    case consensus::ALTER_SCHEMA_OP:
      return PlayAlterSchemaRequest(replicate);

    case consensus::CHANGE_CONFIG_OP:
      return PlayChangeConfigRequest(replicate);

    case consensus::TRUNCATE_OP:
      return PlayTruncateRequest(replicate);

    case consensus::NO_OP:
      return PlayNoOpRequest(replicate);

    case consensus::UPDATE_TRANSACTION_OP:
      return PlayUpdateTransactionRequest(replicate);

    // Unexpected cases:
    case consensus::SNAPSHOT_OP:
      return STATUS(IllegalState, Substitute(
          "The operation is not supported in the community edition: $0", op_type));

    case consensus::UNKNOWN_OP:
      return STATUS(IllegalState, Substitute("Unsupported operation type: $0", op_type));
  }

  FATAL_INVALID_ENUM_VALUE(consensus::OperationType, op_type);
}

// Never deletes 'replicate_entry' or 'commit_entry'.
Status TabletBootstrap::HandleEntryPair(LogEntryPB* replicate_entry) {
  ReplicateMsg* replicate = replicate_entry->mutable_replicate();
  const OperationType op_type = replicate_entry->replicate().op_type();

  {
    const auto status = HandleOperation(op_type, replicate);
    if (!status.ok()) {
      return status.CloneAndAppend(Format(
          "Failed to play $0 request. ReplicateMsg: { $1 }",
          OperationType_Name(op_type), *replicate));
    }
  }

  return Status::OK();
}

void TabletBootstrap::DumpReplayStateToLog(const ReplayState& state) {
  // Dump the replay state, this will log the pending replicates, which might be useful for
  // debugging.
  vector<string> state_dump;
  state.DumpReplayStateToStrings(&state_dump);
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
  auto persistent_op_id = MinimumOpId();
  Result<yb::OpId> flushed_op_id = tablet_->MaxPersistentOpId();
  RETURN_NOT_OK(flushed_op_id);

  persistent_op_id.set_term(flushed_op_id.get_ptr()->term);
  persistent_op_id.set_index(flushed_op_id.get_ptr()->index);
  ReplayState state(persistent_op_id);

  LOG_WITH_PREFIX(INFO) << "Max persistent index in RocksDB's SSTables before bootstrap: "
                        << state.last_stored_op_id.ShortDebugString();

  log::SegmentSequence segments;
  RETURN_NOT_OK(log_reader_->GetSegmentsSnapshot(&segments));

  // We defer opening the log until here, so that we properly reproduce the point-in-time schema
  // from the log we're reading into the log we're writing.
  RETURN_NOT_OK_PREPEND(OpenNewLog(), "Failed to open new log");

  int segment_count = 0;
  for (const scoped_refptr<ReadableLogSegment>& segment : segments) {
    log::LogEntries entries;
    // TODO: Optimize this to not read the whole thing into memory?
    Status read_status = segment->ReadEntries(&entries);
    for (int entry_idx = 0; entry_idx < entries.size(); ++entry_idx) {
      Status s = HandleEntry(&state, &entries[entry_idx]);
      if (!s.ok()) {
        LOG(INFO) << "Dumping replay state to log";
        DumpReplayStateToLog(state);
        RETURN_NOT_OK_PREPEND(s, DebugInfo(tablet_->tablet_id(),
                                           segment->header().sequence_number(),
                                           entry_idx, segment->path(),
                                           *entries[entry_idx]));
      }
    }

    // If the LogReader failed to read for some reason, we'll still try to replay as many entries as
    // possible, and then fail with Corruption.
    // TODO: this is sort of scary -- why doesn't LogReader expose an entry-by-entry iterator-like
    // API instead? Seems better to avoid exposing the idea of segments to callers.
    if (PREDICT_FALSE(!read_status.ok())) {
      return STATUS(Corruption, Substitute("Error reading Log Segment of tablet $0: $1 "
                                           "(Read up to entry $2 of segment $3, in path $4)",
                                           tablet_->tablet_id(),
                                           read_status.ToString(),
                                           entries.size(),
                                           segment->header().sequence_number(),
                                           segment->path()));
    }

    // TODO: could be more granular here and log during the segments as well, plus give info about
    // number of MB processed, but this is better than nothing.
    listener_->StatusMessage(Substitute("Bootstrap replayed $0/$1 log segments. "
                                        "Stats: $2. Pending: $3 replicates",
                                        segment_count + 1, log_reader_->num_segments(),
                                        stats_.ToString(),
                                        state.pending_replicates.size()));
    segment_count++;
  }

  LOG(INFO) << "Dumping replay state to log at the end of " << __FUNCTION__;
  DumpReplayStateToLog(state);

  // Set up the ConsensusBootstrapInfo structure for the caller.
  for (auto& e : state.pending_replicates) {
    // We only allow log entries with an index later than the index of the last log entry already
    // applied to RocksDB to be passed to the tablet as "orphaned replicates". This will make sure
    // we don't try to write to RocksDB with non-monotonic sequence ids, but still create
    // ConsensusRound instances for writes that have not been persisted into RocksDB.
    consensus_info->orphaned_replicates.emplace_back(e.second->release_replicate());
  }
  LOG(INFO) << "Number of orphaned replicates: " << consensus_info->orphaned_replicates.size()
            << ", last id: " << state.prev_op_id << ", commited id: " << state.committed_op_id;
  CHECK(state.prev_op_id.term() >= state.committed_op_id.term() &&
        state.prev_op_id.index() >= state.committed_op_id.index())
      << "Last: " << state.prev_op_id.ShortDebugString()
      << ", committed: " << state.committed_op_id;

  tablet_->mvcc_manager()->SetLastReplicated(state.rocksdb_last_entry_hybrid_time);
  consensus_info->last_id = state.prev_op_id;
  consensus_info->last_committed_id = state.committed_op_id;

  return Status::OK();
}

void TabletBootstrap::PlayWriteRequest(ReplicateMsg* replicate_msg) {
  DCHECK(replicate_msg->has_hybrid_time());

  WriteRequestPB* write = replicate_msg->mutable_write_request();

  DCHECK(write->has_write_batch());

  WriteOperationState operation_state(nullptr, write, nullptr);
  operation_state.mutable_op_id()->CopyFrom(replicate_msg->id());
  operation_state.set_hybrid_time(HybridTime(replicate_msg->hybrid_time()));

  tablet_->StartOperation(&operation_state);

  // Use committed OpId for mem store anchoring.
  operation_state.mutable_op_id()->CopyFrom(replicate_msg->id());

  tablet_->ApplyRowOperations(&operation_state);

  tablet_->mvcc_manager()->Replicated(operation_state.hybrid_time());
}

Status TabletBootstrap::PlayAlterSchemaRequest(ReplicateMsg* replicate_msg) {
  AlterSchemaRequestPB* alter_schema = replicate_msg->mutable_alter_schema_request();

  // Decode schema
  Schema schema;
  RETURN_NOT_OK(SchemaFromPB(alter_schema->schema(), &schema));

  AlterSchemaOperationState operation_state(alter_schema);

  RETURN_NOT_OK(tablet_->CreatePreparedAlterSchema(&operation_state, &schema));

  // Apply the alter schema to the tablet.
  RETURN_NOT_OK_PREPEND(tablet_->AlterSchema(&operation_state), "Failed to AlterSchema:");

  // Also update the log information. Normally, the AlterSchema() call above takes care of this, but
  // our new log isn't hooked up to the tablet yet.
  log_->SetSchemaForNextLogSegment(schema, operation_state.schema_version());

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

Status TabletBootstrap::PlayUpdateTransactionRequest(ReplicateMsg* replicate_msg) {
  DCHECK(replicate_msg->has_hybrid_time());

  UpdateTxnOperationState operation_state(
      nullptr, replicate_msg->mutable_transaction_state());
  operation_state.mutable_op_id()->CopyFrom(replicate_msg->id());
  operation_state.set_hybrid_time(HybridTime(replicate_msg->hybrid_time()));

  auto transaction_coordinator = tablet_->transaction_coordinator();

  TransactionCoordinator::ReplicatedData replicated_data = {
      ProcessingMode::NON_LEADER,
      tablet_.get(),
      *operation_state.request(),
      operation_state.op_id(),
      operation_state.hybrid_time()
  };
  RETURN_NOT_OK(transaction_coordinator->ProcessReplicated(replicated_data));

  return Status::OK();
}

void TabletBootstrap::UpdateClock(uint64_t hybrid_time) {
  data_.clock->Update(HybridTime(hybrid_time));
}

string TabletBootstrap::LogPrefix() const {
  return Substitute("T $0 P $1: ", meta_->tablet_id(), meta_->fs_manager()->uuid());
}

// ============================================================================
//  Class TabletBootstrap::Stats.
// ============================================================================
string TabletBootstrap::Stats::ToString() const {
  return Substitute("ops{read=$0 overwritten=$1} "
                    "inserts{seen=$2 ignored=$3} "
                    "mutations{seen=$4 ignored=$5}",
                    ops_read, ops_overwritten,
                    inserts_seen, inserts_ignored,
                    mutations_seen, mutations_ignored);
}

} // namespace tablet
} // namespace yb
