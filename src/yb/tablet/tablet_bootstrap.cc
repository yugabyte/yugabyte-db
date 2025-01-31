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

#include <map>
#include <set>

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>

#include "yb/common/common_fwd.h"
#include "yb/common/opid.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/schema.h"

#include "yb/client/session.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/consensus_meta.h"
#include "yb/consensus/consensus_util.h"
#include "yb/consensus/log.h"
#include "yb/consensus/log.messages.h"
#include "yb/consensus/log_anchor_registry.h"
#include "yb/consensus/log_index.h"
#include "yb/consensus/log_reader.h"
#include "yb/consensus/log_util.h"
#include "yb/consensus/opid_util.h"
#include "yb/consensus/retryable_requests.h"

#include "yb/docdb/consensus_frontier.h"
#include "yb/dockv/value_type.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/master/sys_catalog_constants.h"
#include "yb/rpc/rpc_fwd.h"
#include "yb/rpc/lightweight_message.h"

#include "yb/tablet/tablet_fwd.h"
#include "yb/tablet/mvcc.h"
#include "yb/tablet/operations/change_auto_flags_config_operation.h"
#include "yb/tablet/operations/change_metadata_operation.h"
#include "yb/tablet/operations/clone_operation.h"
#include "yb/tablet/operations/history_cutoff_operation.h"
#include "yb/tablet/operations/snapshot_operation.h"
#include "yb/tablet/operations/split_operation.h"
#include "yb/tablet/operations/truncate_operation.h"
#include "yb/tablet/operations/update_txn_operation.h"
#include "yb/tablet/operations/write_operation.h"
#include "yb/tablet/snapshot_coordinator.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_bootstrap_state_manager.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_options.h"
#include "yb/tablet/tablet_snapshots.h"
#include "yb/tablet/tablet_splitter.h"
#include "yb/tablet/transaction_coordinator.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/tserver/backup.pb.h"

#include "yb/util/atomic.h"
#include "yb/util/env_util.h"
#include "yb/util/fault_injection.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/metric_entity.h"
#include "yb/util/monotime.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/status.h"
#include "yb/util/stopwatch.h"
#include "yb/util/to_stream.h"

DEFINE_UNKNOWN_bool(skip_remove_old_recovery_dir, false,
            "Skip removing WAL recovery dir after startup. (useful for debugging)");
TAG_FLAG(skip_remove_old_recovery_dir, hidden);

// This is technically runtime, but it only affects tablets that start bootstrap after the flag
// value is changed.
DEFINE_RUNTIME_bool(skip_wal_rewrite, true, "Skip rewriting WAL files during bootstrap.");
TAG_FLAG(skip_wal_rewrite, experimental);

DEFINE_test_flag(double, fault_crash_during_log_replay, 0.0,
                 "Fraction of the time when the tablet will crash immediately "
                 "after processing a log entry during log replay.");

DECLARE_uint64(max_clock_sync_error_usec);

DEFINE_UNKNOWN_bool(force_recover_flushed_frontier, false,
            "Could be used to ignore the flushed frontier metadata from RocksDB manifest and "
            "recover it from the log instead.");
TAG_FLAG(force_recover_flushed_frontier, hidden);
TAG_FLAG(force_recover_flushed_frontier, advanced);

DEFINE_UNKNOWN_bool(skip_flushed_entries, true,
            "Only replay WAL entries that are not flushed to RocksDB or necessary to bootstrap "
            "retryable requests (unflushed and within retryable request timeout).");

DEFINE_RUNTIME_bool(skip_flushed_entries_in_first_replayed_segment, true,
            "If applicable, only replay entries that are not flushed to RocksDB or necessary "
            "to bootstrap retryable requests in the first replayed wal segment.");

DEFINE_RUNTIME_bool(use_bootstrap_intent_ht_filter, true,
                    "Use min replay txn start time filter for bootstrap.");

DECLARE_int32(retryable_request_timeout_secs);

DEFINE_UNKNOWN_uint64(transaction_status_tablet_log_segment_size_bytes, 4_MB,
              "The segment size for transaction status tablet log roll-overs, in bytes.");
DEFINE_test_flag(int32, tablet_bootstrap_delay_ms, 0,
                 "Time (in ms) to delay tablet bootstrap by.");

DEFINE_test_flag(bool, dump_docdb_before_tablet_bootstrap, false,
                 "Dump the contents of DocDB before tablet bootstrap. Should only be used when "
                 "data is small.");

DEFINE_test_flag(bool, dump_docdb_after_tablet_bootstrap, false,
                 "Dump the contents of DocDB after tablet bootstrap. Should only be used when "
                 "data is small.");

DEFINE_test_flag(bool, play_pending_uncommitted_entries, false,
                 "Play all the pending entries present in the log even if they are uncommitted.");

DEFINE_NON_RUNTIME_bool(skip_wal_replay_from_beginning_with_cdc, true,
                        "If false, read all the WAL segments starting from the "
                        "beginning instead of starting post the flushed entries.");

DECLARE_bool(enable_flush_retryable_requests);

namespace yb {
namespace tablet {

using namespace std::literals; // NOLINT
using namespace std::placeholders;
using std::shared_ptr;
using std::string;
using std::vector;

using log::Log;
using log::LogOptions;
using log::LogReader;
using log::ReadableLogSegment;
using log::LogEntryMetadata;
using log::LogIndex;
using log::CreateNewSegment;
using log::SegmentSequence;
using consensus::RaftConfigPB;
using consensus::ConsensusBootstrapInfo;
using consensus::ConsensusMetadata;
using consensus::MinimumOpId;
using consensus::MakeOpIdPB;
using strings::Substitute;

static string DebugInfo(const string& tablet_id,
                        uint64_t segment_seqno,
                        size_t entry_idx,
                        const string& segment_path,
                        const log::LWLogEntryPB* entry) {
  // Truncate the debug string to a reasonable length for logging.  Otherwise, glog will truncate
  // for us and we may miss important information which came after this long string.
  string debug_str = entry ? entry->ShortDebugString() : "<nullptr>"s;
  if (debug_str.size() > 500) {
    debug_str.resize(500);
    debug_str.append("...");
  }
  return Substitute("Debug Info: Error playing entry $0 of segment $1 of tablet $2. "
                    "Segment path: $3. Entry: $4", entry_idx, segment_seqno, tablet_id,
                    segment_path, debug_str);
}

// ================================================================================================
// Class ReplayState.
// ================================================================================================

struct Entry {
  std::shared_ptr<log::LWLogEntryPB> entry;
  RestartSafeCoarseTimePoint entry_time;

  std::string ToString() const {
    return Format("{ entry: $0 entry_time: $1 }", entry, entry_time);
  }
};

typedef std::map<int64_t, Entry> OpIndexToEntryMap;

// State kept during replay.
struct ReplayState {
  ReplayState(
      const DocDbOpIds& op_ids_,
      const std::string& log_prefix_)
      : stored_op_ids(op_ids_),
        log_prefix(log_prefix_) {
  }

  // Return true if 'b' is allowed to immediately follow 'a' in the log.
  static bool IsValidSequence(const OpId& a, const OpId& b);

  // Return a Corruption status if 'id' seems to be out-of-sequence in the log.
  Status CheckSequentialReplicateId(const consensus::LWReplicateMsg& msg);

  void UpdateCommittedOpId(const OpId& id);

  // half_limit is half the limit on the number of entries added
  void AddEntriesToStrings(
      const OpIndexToEntryMap& entries, std::vector<std::string>* strings, size_t half_limit) const;

  // half_limit is half the limit on the number of entries to be dumped
  void DumpReplayStateToStrings(std::vector<std::string>* strings, int half_limit) const;

  bool CanApply(const log::LWLogEntryPB& entry);

  const std::string& LogPrefix() const { return log_prefix; }

  void UpdateCommittedFromStored();

  // Determines the lowest possible OpId we have to replay. This is based on OpIds of operations
  // flushed to regular and intents RocksDBs. Also logs some diagnostics.
  OpId GetLowestOpIdToReplay(bool has_intents_db, const char* extra_log_prefix) const;

  // ----------------------------------------------------------------------------------------------
  // ReplayState member fields
  // ----------------------------------------------------------------------------------------------

  // The last replicate message's ID.
  OpId prev_op_id;

  // The last operation known to be committed. All other operations with lower IDs are also
  // committed.
  OpId committed_op_id;

  // All REPLICATE entries that have not been applied to RocksDB yet. We decide what entries are
  // safe to apply and delete from this map based on the commit index included into each REPLICATE
  // message.
  //
  // The key in this map is the Raft index.
  OpIndexToEntryMap pending_replicates;

  // ----------------------------------------------------------------------------------------------
  // State specific to RocksDB-backed tables (not transaction status table)

  const DocDbOpIds stored_op_ids;

  // Total number of log entries applied to RocksDB.
  int64_t num_entries_applied_to_rocksdb = 0;

  // If we encounter the last entry flushed to a RocksDB SSTable (as identified by the max
  // persistent sequence number), we remember the hybrid time of that entry in this field.
  // We guarantee that we'll either see that entry or a latter entry we know is committed into Raft
  // during log replay. This is crucial for properly setting safe time at bootstrap.
  HybridTime max_committed_hybrid_time = HybridTime::kMin;

  const std::string log_prefix;
};

void ReplayState::UpdateCommittedFromStored() {
  if (stored_op_ids.regular > committed_op_id) {
    committed_op_id = stored_op_ids.regular;
  }

  if (stored_op_ids.intents > committed_op_id) {
    committed_op_id = stored_op_ids.intents;
  }
}

// Return true if 'b' is allowed to immediately follow 'a' in the log.
bool ReplayState::IsValidSequence(const OpId& a, const OpId& b) {
  if (a.term == 0 && a.index == 0) {
    // Not initialized - can start with any opid.
    return true;
  }

  // Within the same term, we should never skip entries.
  // We can, however go backwards (see KUDU-783 for an example)
  if (b.term == a.term && b.index > a.index + 1) {
    return false;
  }

  // TODO: check that the term does not decrease.
  // https://github.com/yugabyte/yugabyte-db/issues/5115

  return true;
}

// Return a Corruption status if 'id' seems to be out-of-sequence in the log.
Status ReplayState::CheckSequentialReplicateId(const consensus::LWReplicateMsg& msg) {
  SCHECK(msg.has_id(), Corruption, "A REPLICATE message must have an id");
  const auto msg_op_id = OpId::FromPB(msg.id());
  if (PREDICT_FALSE(!IsValidSequence(prev_op_id, msg_op_id))) {
    string op_desc = Format(
        "$0 REPLICATE (Type: $1)", msg_op_id, OperationType_Name(msg.op_type()));
    return STATUS_FORMAT(Corruption,
                         "Unexpected op id following op id $0. Operation: $1",
                         prev_op_id, op_desc);
  }

  prev_op_id = msg_op_id;
  return Status::OK();
}

void ReplayState::UpdateCommittedOpId(const OpId& id) {
  if (committed_op_id < id) {
    VLOG_WITH_PREFIX(1) << "Updating committed op id to " << id;
    committed_op_id = id;
  }
}

void ReplayState::AddEntriesToStrings(const OpIndexToEntryMap& entries,
                                      std::vector<std::string>* strings,
                                      size_t half_limit) const {
  const auto n = entries.size();
  const bool overflow = n > 2 * half_limit;
  size_t index = 0;
  for (const auto& entry : entries) {
    if (!overflow || (index < half_limit || index >= n - half_limit)) {
      const auto& replicate = entry.second.entry.get()->replicate();
      strings->push_back(Format(
          "    [$0] op_id: $1 hybrid_time: $2 op_type: $3 committed_op_id: $4",
          index + 1,
          OpId::FromPB(replicate.id()),
          replicate.hybrid_time(),
          consensus::OperationType_Name(replicate.op_type()),
          OpId::FromPB(replicate.committed_op_id())));
    }
    if (overflow && index == half_limit - 1) {
      strings->push_back(Format("($0 lines skipped)", n - 2 * half_limit));
    }
    index++;
  }
}

void ReplayState::DumpReplayStateToStrings(
    std::vector<std::string>* strings,
    int half_limit) const {
  strings->push_back(Format(
      "ReplayState: "
      "Previous OpId: $0, "
      "Committed OpId: $1, "
      "Pending Replicates: $2, "
      "Flushed Regular: $3, "
      "Flushed Intents: $4",
      prev_op_id,
      committed_op_id,
      pending_replicates.size(),
      stored_op_ids.regular,
      stored_op_ids.intents));
  if (num_entries_applied_to_rocksdb > 0) {
    strings->push_back(Substitute("Log entries applied to RocksDB: $0",
                                  num_entries_applied_to_rocksdb));
  }
  if (!pending_replicates.empty()) {
    strings->push_back(Substitute("Dumping REPLICATES ($0 items):", pending_replicates.size()));
    AddEntriesToStrings(pending_replicates, strings, half_limit);
  }
}

bool ReplayState::CanApply(const log::LWLogEntryPB& entry) {
  return OpId::FromPB(entry.replicate().id()) <= committed_op_id;
}

OpId ReplayState::GetLowestOpIdToReplay(bool has_intents_db, const char* extra_log_prefix) const {
  const auto op_id_replay_lowest =
      has_intents_db ? std::min(stored_op_ids.regular, stored_op_ids.intents)
                     : stored_op_ids.regular;
  LOG_WITH_PREFIX(INFO)
      << extra_log_prefix
      << "op_id_replay_lowest=" << op_id_replay_lowest
      << " (regular_op_id=" << stored_op_ids.regular
      << ", intents_op_id=" << stored_op_ids.intents
      << ", has_intents_db=" << has_intents_db << ")";
  return op_id_replay_lowest;
}

// ================================================================================================
// Class TabletBootstrap.
// ================================================================================================

namespace {

struct ReplayDecision {
  bool should_replay = false;

  // This is true for transaction update operations that have already been applied to the regular
  // RocksDB but not to the intents RocksDB.
  docdb::StorageSet apply_to_storages = docdb::StorageSet::All();

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(should_replay, apply_to_storages);
  }
};

bool WriteOpHasTransaction(const consensus::LWReplicateMsg& replicate) {
  if (!replicate.has_write()) {
    return false;
  }
  const auto& write_request = replicate.write();
  if (!write_request.has_write_batch()) {
    return false;
  }
  const auto& write_batch = write_request.write_batch();
  if (write_batch.has_transaction()) {
    return true;
  }
  for (const auto& pair : write_batch.write_pairs()) {
    if (!pair.key().empty() && pair.key()[0] == dockv::KeyEntryTypeAsChar::kExternalTransactionId) {
      return true;
    }
  }
  return false;
}

}  // anonymous namespace

YB_STRONGLY_TYPED_BOOL(NeedsRecovery);

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
// This class is not thread-safe.
class TabletBootstrap {
 public:
  explicit TabletBootstrap(const BootstrapTabletData& data)
      : data_(data),
        meta_(data.tablet_init_data.metadata),
        mem_tracker_(data.tablet_init_data.parent_mem_tracker),
        listener_(data.listener),
        cmeta_(data.consensus_meta),
        append_pool_(data.append_pool),
        allocation_pool_(data.allocation_pool),
        log_sync_pool_(data.log_sync_pool),
        skip_wal_rewrite_(GetAtomicFlag(&FLAGS_skip_wal_rewrite)),
        test_hooks_(data.test_hooks) {
  }

  ~TabletBootstrap() {}

  Status Bootstrap(
      TabletPtr* rebuilt_tablet,
      scoped_refptr<log::Log>* rebuilt_log,
      consensus::ConsensusBootstrapInfo* consensus_info) {
    const string tablet_id = meta_->raft_group_id();

    // Replay requires a valid Consensus metadata file to exist in order to compare the committed
    // consensus configuration seqno with the log entries and also to persist committed but
    // unpersisted changes.
    if (!cmeta_) {
      RETURN_NOT_OK_PREPEND(ConsensusMetadata::Load(meta_->fs_manager(), tablet_id,
                                                    meta_->fs_manager()->uuid(), &cmeta_holder_),
                            "Unable to load Consensus metadata");
      cmeta_ = cmeta_holder_.get();
    }

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

    std::optional<consensus::TabletBootstrapStatePB> bootstrap_state_pb = std::nullopt;
    HybridTime min_replay_txn_start_ht = HybridTime::kInvalid;
    if (GetAtomicFlag(&FLAGS_enable_flush_retryable_requests) && data_.bootstrap_state_manager) {
      auto result = data_.bootstrap_state_manager->LoadFromDisk();
      if (result.ok()) {
        bootstrap_state_pb = std::move(*result);

        if (GetAtomicFlag(&FLAGS_use_bootstrap_intent_ht_filter)) {
          const auto& bootstrap_state = data_.bootstrap_state_manager->bootstrap_state();
          min_replay_txn_start_ht = bootstrap_state.GetMinReplayTxnStartTime();
        }
      } else if (!result.status().IsNotFound()) {
        return result.status();
      }
    }

    const bool has_blocks = VERIFY_RESULT(OpenTablet(min_replay_txn_start_ht));

    if (data_.retryable_requests) {
      const auto retryable_request_timeout_secs = meta_->IsSysCatalog()
          ? client::SysCatalogRetryableRequestTimeoutSecs()
          : client::RetryableRequestTimeoutSecs(tablet_->table_type());
      data_.retryable_requests->SetRequestTimeout(retryable_request_timeout_secs);
      data_.retryable_requests->SetMetricEntity(tablet_->GetTabletMetricsEntity());
    }

    // Load retryable requests after metrics entity has been instantiated.
    if (bootstrap_state_pb && data_.bootstrap_retryable_requests && data_.retryable_requests) {
      data_.retryable_requests->FromPB(*bootstrap_state_pb);
    }

    if (FLAGS_TEST_dump_docdb_before_tablet_bootstrap) {
      LOG_WITH_PREFIX(INFO) << "DEBUG: DocDB dump before tablet bootstrap:";
      tablet_->TEST_DocDBDumpToLog(IncludeIntents::kTrue);
    }

    const auto needs_recovery = VERIFY_RESULT(PrepareToReplay());
    if (needs_recovery && !skip_wal_rewrite_) {
      RETURN_NOT_OK(OpenLogReader());
    }

    // This is a new tablet, nothing left to do.
    if (!has_blocks && !needs_recovery) {
      LOG_WITH_PREFIX(INFO) << "No blocks or log segments found. Creating new log.";
      RETURN_NOT_OK_PREPEND(OpenLog(CreateNewSegment::kTrue), "Failed to open new log");
      RETURN_NOT_OK(FinishBootstrap("No bootstrap required, opened a new log",
                                    rebuilt_log,
                                    rebuilt_tablet));
      consensus_info->last_id = MinimumOpId();
      consensus_info->last_committed_id = MinimumOpId();
      return Status::OK();
    }

    // Only sleep if this isn't a new tablet, since we only want to delay on restart when testing.
    if (PREDICT_FALSE(FLAGS_TEST_tablet_bootstrap_delay_ms > 0)) {
      SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_tablet_bootstrap_delay_ms));
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
      // automatically on the first compaction, and this is a special mode for manual
      // troubleshooting.
      LOG_WITH_PREFIX(WARNING)
          << "--force_recover_flushed_frontier specified, forcefully setting "
          << "flushed frontier after bootstrap: " << new_consensus_frontier.ToString();
      RETURN_NOT_OK(tablet_->ModifyFlushedFrontier(
          new_consensus_frontier, rocksdb::FrontierModificationMode::kForce));
    }

    RETURN_NOT_OK(FinishBootstrap("Bootstrap complete.", rebuilt_log, rebuilt_tablet));

    return Status::OK();
  }

 private:
  // Finishes bootstrap, setting 'rebuilt_log' and 'rebuilt_tablet'.
  Status FinishBootstrap(
      const std::string& message,
      scoped_refptr<log::Log>* rebuilt_log,
      TabletPtr* rebuilt_tablet) {
    tablet_->MarkFinishedBootstrapping();
    listener_->StatusMessage(message);
    if (FLAGS_TEST_dump_docdb_after_tablet_bootstrap) {
      LOG_WITH_PREFIX(INFO) << "DEBUG: DocDB debug dump after tablet bootstrap:\n";
      tablet_->TEST_DocDBDumpToLog(IncludeIntents::kTrue);
    }

    *rebuilt_tablet = std::move(tablet_);
    RETURN_NOT_OK(log_->EnsureSegmentInitialized());
    rebuilt_log->swap(log_);
    return Status::OK();
  }

  // Sets result to true if there was any data on disk for this tablet.
  Result<bool> OpenTablet(HybridTime min_replay_txn_start_ht) {
    CleanupSnapshots();
    // Use operator new instead of make_shared for creating the shared_ptr. That way, we would have
    // the shared_ptr's control block hold a raw pointer to the Tablet object as opposed to the
    // object itself being allocated on the control block.
    //
    // Since we create weak_ptr from this shared_ptr and store it in other classes like WriteQuery,
    // any leaked weak_ptr wouldn't prevent the underlying object's memory deallocation after the
    // reference count drops to 0. With make_shared, there's a risk of a leaked weak_ptr holding up
    // the object's memory even after all shared_ptrs go out of scope.
    std::shared_ptr<Tablet> tablet(new Tablet(data_.tablet_init_data));

    auto participant = tablet->transaction_participant();
    if (participant) {
      participant->SetMinReplayTxnStartTimeLowerBound(min_replay_txn_start_ht);
    }

    // Doing nothing for now except opening a tablet locally.
    LOG_TIMING_PREFIX(INFO, LogPrefix(), "opening tablet") {
      RETURN_NOT_OK(tablet->Open());
    }

    // In theory, an error can happen in case of tablet Shutdown or in RocksDB object replacement
    // operation like RestoreSnapshot or Truncate. However, those operations can't really be
    // happening concurrently as we haven't opened the tablet yet.
    const bool has_ss_tables = VERIFY_RESULT(tablet->HasSSTables());

    // Tablet meta data may require some updates after tablet is opened.
    RETURN_NOT_OK(MaybeUpdateMetaAfterTabletHasBeenOpened(*tablet));

    tablet_ = std::move(tablet);
    return has_ss_tables;
  }

  // Makes updates to tablet meta if required.
  Status MaybeUpdateMetaAfterTabletHasBeenOpened(const Tablet& tablet) {
    // For backward compatibility: allow old tablets to use benefits of one-file-at-a-time
    // post split compaction algorithm by explicitly setting the value for
    // post_split_compaction_file_number_upper_bound.
    if (tablet.regular_db() && tablet.key_bounds().IsInitialized() &&
        !meta_->parent_data_compacted() &&
        !meta_->post_split_compaction_file_number_upper_bound().has_value()) {
      meta_->set_post_split_compaction_file_number_upper_bound(
          tablet.regular_db()->GetNextFileNumber());
      RETURN_NOT_OK(meta_->Flush());
    }

    return Status::OK();
  }

  // Checks if a previous log recovery directory exists. If so, it deletes any files in the log dir
  // and sets 'needs_recovery' to true, meaning that the previous recovery attempt should be retried
  // from the recovery dir.
  //
  // Otherwise, if there is a log directory with log files in it, renames that log dir to the log
  // recovery dir and creates a new, empty log dir so that log replay can proceed. 'needs_recovery'
  // is also returned as true in this case.
  //
  // If no log segments are found, 'needs_recovery' is set to false.
  Result<NeedsRecovery> PrepareToReplay() {
    const string& log_dir = tablet_->metadata()->wal_dir();

    // If the recovery directory exists, then we crashed mid-recovery.  Throw away any logs from the
    // previous recovery attempt and restart the log replay process from the beginning using the
    // same recovery dir as last time.
    const string recovery_path = FsManager::GetTabletWalRecoveryDir(log_dir);
    if (GetEnv()->FileExists(recovery_path)) {
      LOG_WITH_PREFIX(INFO) << "Previous recovery directory found at " << recovery_path << ": "
                            << "Replaying log files from this location instead of " << log_dir;

      // Since we have a recovery directory, clear out the log_dir by recursively deleting it and
      // creating a new one so that we don't end up with remnants of old WAL segments or indexes
      // after replay.
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

      return NeedsRecovery::kTrue;
    }

    // If we made it here, there was no pre-existing recovery dir.  Now we look for log files in
    // log_dir, and if we find any then we rename the whole log_dir to a recovery dir and return
    // needs_recovery = true.
    RETURN_NOT_OK_PREPEND(env_util::CreateDirIfMissing(GetEnv(), DirName(log_dir)),
                          "Failed to create table log directory " + DirName(log_dir));

    RETURN_NOT_OK_PREPEND(env_util::CreateDirIfMissing(GetEnv(), log_dir),
                          "Failed to create tablet log directory " + log_dir);

    vector<string> log_dir_children = VERIFY_RESULT_PREPEND(
        GetEnv()->GetChildren(log_dir, ExcludeDots::kTrue), "Couldn't list log segments.");

    // To ensure consistent order of log messages. Note: this does not affect the replay order
    // of segments, only the order of INFO log messages below.
    sort(log_dir_children.begin(), log_dir_children.end());

    bool needs_recovery = false;
    for (const string& log_dir_child : log_dir_children) {
      if (!log::IsLogFileName(log_dir_child)) {
        continue;
      }

      needs_recovery = true;
      string source_path = JoinPathSegments(log_dir, log_dir_child);
      if (skip_wal_rewrite_) {
        LOG_WITH_PREFIX(INFO) << "Will attempt to recover log segment " << source_path;
        continue;
      }

      string dest_path = JoinPathSegments(recovery_path, log_dir_child);
      LOG_WITH_PREFIX(INFO) << "Will attempt to recover log segment " << source_path
                            << " to " << dest_path;
    }

    if (!skip_wal_rewrite_ && needs_recovery) {
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
    return NeedsRecovery(needs_recovery);
  }

  // Opens the latest log segments for the Tablet that will allow to rebuild the tablet's soft
  // state. If there are existing log segments in the tablet's log directly they are moved to a
  // "log-recovery" directory which is deleted when the replay process is completed (as they have
  // been duplicated in the current log directory).
  //
  // If a "log-recovery" directory is already present, we will continue to replay from the
  // "log-recovery" directory. Tablet metadata is updated once replay has finished from the
  // "log-recovery" directory.
  Status OpenLogReader() {
    auto wal_dir = tablet_->metadata()->wal_dir();
    auto wal_path = skip_wal_rewrite_ ? wal_dir :
        meta_->fs_manager()->GetTabletWalRecoveryDir(wal_dir);
    VLOG_WITH_PREFIX(1) << "Opening log reader in log recovery dir " << wal_path;
    // Open the reader.
    scoped_refptr<LogIndex> index(nullptr);
    RETURN_NOT_OK_PREPEND(
        LogReader::Open(
            GetEnv(),
            index,
            LogPrefix(),
            wal_path,
            tablet_->GetTableMetricsEntity().get(),
            tablet_->GetTabletMetricsEntity().get(),
            &log_reader_),
        "Could not open LogReader. Reason");
    return Status::OK();
  }

  // Removes the recovery directory and all files contained therein.  Intended to be invoked after
  // log replay successfully completes.
  Status RemoveRecoveryDir() {
    const string recovery_path = FsManager::GetTabletWalRecoveryDir(tablet_->metadata()->wal_dir());
    if (!GetEnv()->FileExists(recovery_path)) {
      VLOG(1) << "Tablet WAL recovery dir " << recovery_path << " does not exist.";
      if (!skip_wal_rewrite_) {
        return STATUS(IllegalState, "Expected recovery dir, none found.");
      }
      return Status::OK();
    }

    LOG_WITH_PREFIX(INFO) << "Preparing to delete log recovery files and directory "
                          << recovery_path;

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

  // Opens log in the tablet's log directory, create_new_segment flag is used to decide
  // whether to create new log or open existing.
  Status OpenLog(log::CreateNewSegment create_new_segment) {
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

    // When lazy superblock flush is enabled, instead of flushing the superblock on every
    // CHANGE_METADATA_OP, we update the metadata only in-memory and flush it to disk when a new WAL
    // segment is allocated. This reduces the latency of applying a CHANGE_METADATA_OP.
    //
    // Currently, this feature is applicable only on colocated table creation.
    // Reference: https://github.com/yugabyte/yugabyte-db/issues/16116
    log::NewSegmentAllocationCallback noop = {};
    auto new_segment_allocation_callback =
        metadata.IsLazySuperblockFlushEnabled()
            ? std::bind(&RaftGroupMetadata::Flush, tablet_->metadata(), OnlyIfDirty::kTrue)
            : noop;
    RETURN_NOT_OK(Log::Open(
        log_options,
        tablet_->tablet_id(),
        metadata.wal_dir(),
        metadata.fs_manager()->uuid(),
        *tablet_->schema(),
        metadata.primary_table_schema_version(),
        tablet_->GetTableMetricsEntity(),
        tablet_->GetTabletMetricsEntity(),
        append_pool_,
        allocation_pool_,
        log_sync_pool_,
        &log_,
        data_.pre_log_rollover_callback,
        new_segment_allocation_callback,
        create_new_segment,
        data_.min_start_ht_running_txns_callback));
    // Disable sync temporarily in order to speed up appends during the bootstrap process.
    log_->DisableSync();
    return Status::OK();
  }

  // Handle the given log entry. Validates entry.type() (it can only be REPLICATE), optionally
  // injects latency in tests, and delegates to HandleReplicateMessage.
  Status HandleEntry(
      log::LogEntryMetadata entry_metadata, std::shared_ptr<log::LWLogEntryPB>* entry_ptr) {
    auto& entry = **entry_ptr;
    VLOG_WITH_PREFIX(2) << "Handling entry: " << entry.ShortDebugString();

    switch (entry.type()) {
      case log::REPLICATE:
        RETURN_NOT_OK(HandleReplicateMessage(entry_metadata, entry_ptr));
        break;
      default:
        return STATUS(Corruption, Substitute("Unexpected log entry type: $0", entry.type()));
    }
    MAYBE_FAULT(FLAGS_TEST_fault_crash_during_log_replay);
    return Status::OK();
  }

  // HandleReplicateMessage implements these important pieces of logic:
  //   - Removes the "tail" of pending_replicates overwritten by a new leader's operations when
  //     encountering an entry with an index lower than or equal to the index of an operation that
  //     is already present in pending_replicates.
  //   - Ignores entries that have already been flushed into regular and intents RocksDBs.
  //   - Updates committed OpId based on the committed OpId from the entry and calls
  //     ApplyCommittedPendingReplicates.
  //   - Updates the "monotonic counter" used for assigning internal keys in YCQL arrays.
  Status HandleReplicateMessage(
      LogEntryMetadata entry_metadata, std::shared_ptr<log::LWLogEntryPB>* replicate_entry_ptr) {
    auto& replicate_entry = **replicate_entry_ptr;
    stats_.ops_read++;

    const auto& replicate = replicate_entry.replicate();
    VLOG_WITH_PREFIX(1) << "HandleReplicateMessage: " << entry_metadata.ToString()
                        << ", op id: " << OpId::FromPB(replicate.id())
                        << ", committed op id: " << OpId::FromPB(replicate.committed_op_id());
    RETURN_NOT_OK(replay_state_->CheckSequentialReplicateId(replicate));
    SCHECK(replicate.has_hybrid_time(), Corruption, "A REPLICATE message must have a hybrid time");
    UpdateClock(replicate.hybrid_time());

    // This sets the monotonic counter to at least replicate.monotonic_counter() atomically.
    tablet_->UpdateMonotonicCounter(replicate.monotonic_counter());

    const auto op_id = OpId::FromPB(replicate_entry.replicate().id());

    // Append the replicate message to the log as is if we are not skipping wal rewrite. If we are
    // skipping, set consensus_state_only to true.
    RETURN_NOT_OK(log_->Append(*replicate_entry_ptr, entry_metadata, skip_wal_rewrite_));

    auto iter = replay_state_->pending_replicates.lower_bound(op_id.index);

    // If there was an entry with the same or higher index as the entry we're adding, then we need
    // to delete that entry and all entries with higher indexes.
    if (iter != replay_state_->pending_replicates.end()) {
      auto& existing_entry = iter->second;
      auto& last_entry = replay_state_->pending_replicates.rbegin()->second;

      LOG_WITH_PREFIX(INFO) << "Overwriting operations starting at: "
                            << OpId::FromPB(existing_entry.entry->replicate().id())
                            << " up to: " << OpId::FromPB(last_entry.entry->replicate().id())
                            << " with operation: " << OpId::FromPB(replicate.id());
      stats_.ops_overwritten += std::distance(iter, replay_state_->pending_replicates.end());
      if (test_hooks_) {
        // Tell the test framework about overwritten OpIds.
        for (auto callback_iter = iter;
             callback_iter != replay_state_->pending_replicates.end();
             callback_iter++) {
          test_hooks_->Overwritten(
              OpId::FromPB(callback_iter->second.entry->replicate().id()));
        }
      }
      replay_state_->pending_replicates.erase(iter, replay_state_->pending_replicates.end());
    }

    // We expect entry_metadata.entry_time to always be set for newly written WAL entries. However,
    // for some very old WALs, it might be missing.
    LOG_IF_WITH_PREFIX(DFATAL, entry_metadata.entry_time == RestartSafeCoarseTimePoint())
        << "Entry metadata must have a restart-safe time. OpId: " << OpId::FromPB(replicate.id());

    CHECK(replay_state_->pending_replicates.emplace(
        op_id.index, Entry{std::move(*replicate_entry_ptr), entry_metadata.entry_time}).second);

    CHECK(replicate.has_committed_op_id())
        << "Replicate message has no committed_op_id for table type "
        << TableType_Name(tablet_->table_type()) << ". Replicate message:\n"
        << replicate.ShortDebugString();

    // We include the commit index as of the time a REPLICATE entry was added to the leader's log
    // into that entry. This allows us to decide when we can replay a REPLICATE entry during
    // bootstrap.
    replay_state_->UpdateCommittedOpId(OpId::FromPB(replicate.committed_op_id()));

    // Test only flag to replay pending uncommitted entries.
    if (FLAGS_TEST_play_pending_uncommitted_entries) {
      LOG(INFO) << "Playing pending uncommitted entires for test only scenario";
      replay_state_->UpdateCommittedOpId(op_id);
    }

    return ApplyCommittedPendingReplicates();
  }

  // Replays the given committed operation.
  Status PlayAnyRequest(
      consensus::LWReplicateMsg* replicate,
      const docdb::StorageSet& apply_to_storages) {
    const auto op_type = replicate->op_type();
    if (test_hooks_) {
      test_hooks_->Replayed(OpId::FromPB(replicate->id()), apply_to_storages);
    }
    switch (op_type) {
      case consensus::WRITE_OP:
        return PlayWriteRequest(replicate, apply_to_storages);

      case consensus::CHANGE_METADATA_OP:
        return PlayChangeMetadataRequest(replicate);

      case consensus::CHANGE_CONFIG_OP:
        return PlayChangeConfigRequest(replicate);

      case consensus::TRUNCATE_OP:
        return PlayTruncateRequest(replicate);

      case consensus::NO_OP:
        return Status::OK();  // This is why it is a no-op!

      case consensus::UPDATE_TRANSACTION_OP:
        return PlayUpdateTransactionRequest(replicate, apply_to_storages);

      case consensus::SNAPSHOT_OP:
        return PlayTabletSnapshotRequest(replicate);

      case consensus::HISTORY_CUTOFF_OP:
        return PlayHistoryCutoffRequest(replicate);

      case consensus::SPLIT_OP:
        return PlaySplitOpRequest(replicate);

      case consensus::CHANGE_AUTO_FLAGS_CONFIG_OP:
        return PlayChangeAutoFlagsConfigRequest(replicate);

      case consensus::CLONE_OP:
        return PlayCloneOpRequest(replicate);

      // Unexpected cases:
      case consensus::UNKNOWN_OP:
        return STATUS(IllegalState, Substitute("Unsupported operation type: $0", op_type));
    }

    LOG_WITH_PREFIX(DFATAL) << "Invalid operation type " << op_type
                            << "for a REPLICATE operation: " << replicate->ShortDebugString();
    return STATUS_FORMAT(Corruption, "Invalid operation type: $0", op_type);
  }

  Status PlayTabletSnapshotRequest(consensus::LWReplicateMsg* replicate_msg) {
    SnapshotOperation operation(tablet_, replicate_msg->mutable_snapshot_request());
    operation.set_hybrid_time(HybridTime(replicate_msg->hybrid_time()));
    operation.set_op_id(OpId::FromPB(replicate_msg->id()));

    return operation.Replicated(/* leader_term= */ OpId::kUnknownTerm,
                                WasPending::kFalse);
  }

  Status PlayHistoryCutoffRequest(consensus::LWReplicateMsg* replicate_msg) {
    HistoryCutoffOperation operation(tablet_, replicate_msg->mutable_history_cutoff());

    return operation.Apply(/* leader_term= */ OpId::kUnknownTerm);
  }

  Status PlaySplitOpRequest(consensus::LWReplicateMsg* replicate_msg) {
    auto& split_request = *replicate_msg->mutable_split_request();
    // We might be asked to replay SPLIT_OP even if it was applied and flushed when
    // FLAGS_force_recover_flushed_frontier is set.
    if (split_request.tablet_id() != tablet_->tablet_id()) {
      // Ignore SPLIT_OP designated for ancestor tablet(s).
      return Status::OK();
    }

    if (tablet_->metadata()->tablet_data_state() == TabletDataState::TABLET_DATA_SPLIT_COMPLETED) {
      // Ignore SPLIT_OP if tablet has been already split.
      VLOG_WITH_PREFIX_AND_FUNC(1) << "Tablet has been already split.";
      return Status::OK();
    }

    SplitOperation operation(tablet_, data_.tablet_init_data.tablet_splitter, &split_request);
    operation.set_op_id(OpId::FromPB(replicate_msg->id()));
    operation.set_hybrid_time(HybridTime(replicate_msg->hybrid_time()));
    return data_.tablet_init_data.tablet_splitter->ApplyTabletSplit(
        &operation, log_.get(), cmeta_->committed_config());

    // TODO(tsplit): In scope of https://github.com/yugabyte/yugabyte-db/issues/1461 add integration
    // tests for:
    // - tablet bootstrap of original tablet which hasn't been yet split and replaying split
    // operation.
    // - tablet bootstrap of original tablet which has been already successfully split and replaying
    // split operation.
    // - tablet bootstrap of new after-split tablet replaying split operation.
  }

  Status PlayCloneOpRequest(consensus::LWReplicateMsg* replicate_msg) {
    CloneOperation operation(
        tablet_, data_.tablet_init_data.tablet_splitter, replicate_msg->mutable_clone_tablet());
    operation.set_op_id(OpId::FromPB(replicate_msg->id()));
    operation.set_hybrid_time(HybridTime(replicate_msg->hybrid_time()));
    // We might be asked to replay CLONE_OP even if it was applied and flushed when
    // FLAGS_force_recover_flushed_frontier is set. ApplyCloneTablet will still succeed in this case
    // because it is a no-op if a clone with the same seq_no is already applied according to the
    // source tablet's metadata.
    return data_.tablet_init_data.tablet_splitter->ApplyCloneTablet(
        &operation, log_.get(), cmeta_->committed_config());
  }

  Status PlayChangeAutoFlagsConfigRequest(consensus::LWReplicateMsg* replicate_msg) {
    if (!tablet_->is_sys_catalog()) {
      // This should never happen. We use WAL to propagate AutoFlags config only to other masters.
      // For tablet servers we use heartbeats.
      LOG_WITH_PREFIX_AND_FUNC(DFATAL)
          << "AutoFlags config request ignored on non-sys_catalog tablet";
      return Status::OK();
    }

    ChangeAutoFlagsConfigOperation operation(
        tablet_, replicate_msg->mutable_auto_flags_config());

    return operation.Apply();
  }

  void HandleRetryableRequest(
      const consensus::LWReplicateMsg& replicate, RestartSafeCoarseTimePoint entry_time) {
    if (!replicate.has_write())
      return;

    if (data_.bootstrap_retryable_requests && data_.retryable_requests) {
      data_.retryable_requests->Bootstrap(replicate, entry_time);
    }

    // In a test, we might not have data_.retryable_requests, but we still want to tell the test
    // that we would submit this OpId to retryable_requests.
    if (test_hooks_) {
      test_hooks_->RetryableRequest(OpId::FromPB(replicate.id()));
    }
  }

  ReplayDecision ShouldReplayOperation(
      consensus::OperationType op_type,
      const int64_t index,
      const DocDbOpIds& flushed_op_ids,
      const int64_t metadata_flushed_index,
      TransactionStatus txn_status,
      bool write_op_has_transaction) {
    if (op_type == consensus::UPDATE_TRANSACTION_OP) {
      if (txn_status == TransactionStatus::APPLYING) {
        auto apply_to_storages = docdb::StorageSet::All();
        if (index <= flushed_op_ids.regular.index) {
          apply_to_storages.ResetRegularDB();
        }
        for (size_t idx = 0; idx != flushed_op_ids.vector_indexes.size(); ++idx) {
          if (index <= flushed_op_ids.vector_indexes[idx].index) {
            apply_to_storages.ResetVectorIndex(idx);
          }
        }
        // This was added as part of D17730 / #12730 to ensure we don't clean up transactions
        // before they are replicated to the CDC destination.
        //
        // TODO: Replaying even transactions that are flushed to both regular and intents RocksDB is
        // a temporary change. The long term change is to track write and apply operations
        // separately instead of a tracking a single "intents_flushed_index".
        VLOG_WITH_PREFIX_AND_FUNC(3)
            << "index: " << index << " flushed_op_ids: " << flushed_op_ids.ToString()
            << ", apply_to_storages: " << apply_to_storages.ToString();
        return {true, apply_to_storages};
      }
      // For other types of transaction updates, we ignore them if they have been flushed to the
      // regular RocksDB.
      VLOG_WITH_PREFIX_AND_FUNC(3)
          << "index: " << index << " flushed_op_ids: " << flushed_op_ids.ToString();
      return {index > flushed_op_ids.regular.index};
    }

    if (metadata_flushed_index >= 0 && op_type == consensus::CHANGE_METADATA_OP) {
      VLOG_WITH_PREFIX_AND_FUNC(3) << "CHANGE_METADATA_OP - index: " << index
                                   << " metadata_flushed_index: " << metadata_flushed_index;
      return {index > metadata_flushed_index};
    }
    // For upgrade scenarios where metadata_flushed_index < 0, follow the pre-existing logic.

    // In most cases we assume that intents_flushed_index <= regular_flushed_index but here we are
    // trying to be resilient to violations of that assumption.
    if (index <= std::min(flushed_op_ids.regular.index, flushed_op_ids.intents.index)) {
      // Never replay anything that is flushed to both regular and intents RocksDBs in a
      // transactional table.
      VLOG_WITH_PREFIX_AND_FUNC(3)
          << "index: " << index << " flushed_op_ids: " << flushed_op_ids.ToString();
      return {false};
    }

    if (op_type == consensus::WRITE_OP && write_op_has_transaction) {
      // Write intents that have not been flushed into the intents DB.
      VLOG_WITH_PREFIX_AND_FUNC(3)
          << "index: " << index << " flushed_op_ids: " << flushed_op_ids.ToString();
      return {index > flushed_op_ids.intents.index};
    }

    VLOG_WITH_PREFIX_AND_FUNC(3)
        << "index: " << index << " flushed_op_ids: " << flushed_op_ids.ToString();
    return {index > flushed_op_ids.regular.index};
  }

  // Performs various checks based on the OpId, and decides whether to replay the given operation.
  // If so, calls PlayAnyRequest, or sometimes calls PlayUpdateTransactionRequest directly.
  Status MaybeReplayCommittedEntry(
      const std::shared_ptr<log::LWLogEntryPB>& replicate_entry,
      RestartSafeCoarseTimePoint entry_time) {
    auto& replicate = *replicate_entry->mutable_replicate();
    const auto op_type = replicate.op_type();
    const auto decision = ShouldReplayOperation(
        op_type,
        replicate.id().index(),
        replay_state_->stored_op_ids,
        meta_->LastFlushedChangeMetadataOperationOpId().index,
        // txn_status
        replicate.has_transaction_state()
            ? replicate.transaction_state().status()
            : TransactionStatus::ABORTED,  // should not be used
        // write_op_has_transaction
        WriteOpHasTransaction(replicate));

    HandleRetryableRequest(replicate, entry_time);
    VLOG_WITH_PREFIX_AND_FUNC(3) << "decision: " << AsString(decision);
    if (decision.should_replay) {
      RETURN_NOT_OK_PREPEND(
          PlayAnyRequest(&replicate, decision.apply_to_storages),
          Format(
            "Failed to play $0 request, replicate: { $1 }",
            OperationType_Name(op_type), replicate));
      replay_state_->max_committed_hybrid_time.MakeAtLeast(HybridTime(replicate.hybrid_time()));
    }

    return Status::OK();
  }

  void DumpReplayStateToLog() {
    // Dump the replay state, this will log the pending replicates, which might be useful for
    // debugging.
    vector<string> state_dump;
    constexpr int kMaxLinesToDump = 1000;
    replay_state_->DumpReplayStateToStrings(&state_dump, kMaxLinesToDump / 2);
    for (const string& line : state_dump) {
      LOG_WITH_PREFIX(INFO) << line;
    }
  }

  Result<DocDbOpIds> GetFlushedOpIds() {
    const auto flushed_op_ids = VERIFY_RESULT(tablet_->MaxPersistentOpId());

    if (FLAGS_force_recover_flushed_frontier) {
      // This is used very rarely to replay all log entries and recover RocksDB flushed OpId
      // metadata.
      LOG_WITH_PREFIX(WARNING)
          << "--force_recover_flushed_frontier specified, ignoring existing flushed frontiers "
          << "from RocksDB metadata (will replay all log records): " << flushed_op_ids.ToString();
      return DocDbOpIds();
    }

    if (test_hooks_) {
      const auto docdb_flushed_op_ids_override = test_hooks_->GetFlushedOpIdsOverride();
      if (docdb_flushed_op_ids_override.is_initialized()) {
        LOG_WITH_PREFIX(INFO) << "Using test values of flushed DocDB OpIds: "
                              << docdb_flushed_op_ids_override->ToString();
        return *docdb_flushed_op_ids_override;
      }
    }

    // Production codepath.
    LOG_WITH_PREFIX(INFO) << "Flushed DocDB OpIds: " << flushed_op_ids.ToString();
    return flushed_op_ids;
  }

  // Determines the first segment to replay based two criteria:
  // - The first OpId of the segment must be less than or equal to (in terms of OpId comparison
  //   where term is compared first and index second) the "flushed OpId". This "flushed OpId" is
  //   determined as the minimum of intents and regular RocksDBs' flushed OpIds for transactional
  //   tables, and just the regular RocksDB's flushed OpId for non-transactional tables. Note that
  //   in practice the flushed OpId of the intents RocksDB should also be less than or equal to the
  //   flushed OpId of the regular RocksDB or this would be an invariant violation.
  //
  // - The first OpId of the segment is less than or equal to persisted retryable requests
  //   file's last_flushed_op_id_ or the "restart safe time" of the first operation in the segment
  //   we choose to start the replay with is older than retryable_request_timeout seconds.
  //   The value of retryable_request_timeout depends on table type of the tablet, which is 60s
  //   for YCQL tables and 600s for YSQL tables by default (see RetryableRequestTimeoutSecs).
  //   This can guarantee that retryable requests started no later than retryable_request_timeout
  //   seconds ago can be rebuilt in memory. This is needed to allow deduplicating
  //   automatic retries from YCQL and YSQL query layer and avoid Jepsen-type consistency
  //   violations. We satisfy this constraint by taking the last segment's first operation's
  //   restart-safe time, subtracting retryable_request_timeout seconds from it, and
  //   finding a segment that has that time or earlier as its first operation's restart-safe time.
  //   This also means we are never allowed to start replay with the last segment, as long as
  //   retryable_request_timeout is greater than 0.
  //
  //   This "restart safe time" is similar to the regular Linux monotonic clock time, but is
  //   maintained across tablet server restarts. See RestartSafeCoarseMonoClock for details.
  //
  //   See https://github.com/yugabyte/yugabyte-db/commit/5cf01889a1b4589a82085e578b5f4746c6614a5d
  //   and the Git history of retryable_requests.cc for more context on this requirement.
  //
  // As long as the two conditions above are satisfied, it is advantageous to us to pick the latest
  // possible segment to start replay with. That way we can skip the maximum number of segments.
  //
  // Returns the iterator pointing to the first segment to start replay with. Also produces a number
  // of diagnostic log messages.
  //
  // This functionality was originally introduced in
  // https://github.com/yugabyte/yugabyte-db/commit/41ef3f75e3c68686595c7613f53b649823b84fed
  SegmentSequence::const_iterator SkipFlushedEntries(SegmentSequence* segments_ptr) {
    static const char* kBootstrapOptimizerLogPrefix =
        "Bootstrap optimizer (skip_flushed_entries): ";

    // Lower bound on op IDs that need to be replayed. This is the "flushed OpId" that this
    // function's comment mentions.
    auto op_id_replay_lowest = replay_state_->GetLowestOpIdToReplay(
        // Determine whether we have an intents DB.
        tablet_->intents_db() || (test_hooks_ && test_hooks_->HasIntentsDB()),
        kBootstrapOptimizerLogPrefix);

    // OpId::Max() can avoid bootstrapping the retryable requests.
    OpId last_op_id_in_retryable_requests = OpId::Max();
    if (data_.bootstrap_retryable_requests && data_.retryable_requests) {
      // If it is required, bootstrap the retryable requests starting from max replicated op id
      // in the structure.
      // If it's OpId::Min(), then should replay all data in last retryable_requests_timeout_secs.
      last_op_id_in_retryable_requests =
          data_.retryable_requests->GetMaxReplicatedOpId();
    }

    SegmentSequence& segments = *segments_ptr;

    // Time point of the first entry of the last WAL segment, and how far back in time from it we
    // should retain other entries.
    boost::optional<RestartSafeCoarseTimePoint> retryable_requests_replay_from_this_or_earlier_time;

    RestartSafeCoarseDuration retryable_requests_retain_interval =
        data_.bootstrap_retryable_requests && data_.retryable_requests
            ? std::chrono::seconds(
                  data_.retryable_requests->request_timeout_secs())
            : 0s;
    // If retryable_requests_retain_interval is 0s, set last_op_id_in_retryable_requests to
    // OpId::Max() to avoid replaying logs from last_op_id_in_retryable_requests if
    // last_op_id_in_retryable_requests < op_id_replay_lowest.
    if (retryable_requests_retain_interval == 0s) {
      last_op_id_in_retryable_requests = OpId::Max();
    }

    if (test_hooks_) {
      last_op_id_in_retryable_requests = test_hooks_->GetFlushedRetryableRequestsOpIdOverride()
                                             .value_or(last_op_id_in_retryable_requests);
    }

    // When lazy superblock flush is enabled, superblock is flushed on a new segment allocation
    // instead of doing it for every CHANGE_METADATA_OP. This reduces the latency of applying
    // a CHANGE_METADATA_OP. In this approach, the committed but unflushed CHANGE_METADATA_OP WAL
    // entries are guaranteed to be limited to the last two segments:
    //  1. Say there are two wal segments: seg0, seg1 (active segment).
    //  2. When seg1 is about to exceed the max size, seg2 is asynchronously allocated. Writes
    //     continue to go seg1 in the meantime.
    //  3. Before completing the seg2 allocation, we flush the superblock. This guarantees
    //     that all the CHANGE_METADATA_OPs in seg0 are flushed to superblock on disk (as
    //     seg0 closed). We can't say the same about seg1 as it is still open and potentially
    //     appending entries.
    //  4. Log rolls over, seg1 is closed and writes now go to seg2.
    //  At this point, the committed unflushed metadata entries are limited to seg1 and seg2.
    //
    // To ensure persistence of such unflushed operations, we replay a minimum of two WAL segments
    // (if present) on tablet bootstrap. Currently, this feature is applicable only on colocated
    // table creation. Reference: https://github.com/yugabyte/yugabyte-db/issues/16116.
    //
    // We should be able to get rid of this requirement when we address:
    // https://github.com/yugabyte/yugabyte-db/issues/16684.
    const bool is_lazy_superblock_flush_enabled = meta_->IsLazySuperblockFlushEnabled();
    const auto kMinSegmentsToReplayWithLazySuperblockFlush = 2;

    auto iter = segments.end();
    while (iter != segments.begin()) {
      --iter;
      ReadableLogSegment& segment = **iter;
      const std::string& segment_path = segment.path();

      const auto first_op_metadata_result = segment.ReadFirstEntryMetadata();
      if (!first_op_metadata_result.ok()) {
        if (test_hooks_) {
          test_hooks_->FirstOpIdOfSegment(segment_path, OpId::Invalid());
        }
        LOG_WITH_PREFIX(WARNING)
            << kBootstrapOptimizerLogPrefix
            << "Could not read the first entry's metadata of log segment " << segment_path << ". "
            << "Simply continuing to earlier segments to determine the first segment "
            << "to start the replay at. The error was: " << first_op_metadata_result.status();
        continue;
      }
      const auto& first_op_metadata = *first_op_metadata_result;

      const auto op_id = first_op_metadata.op_id;
      if (test_hooks_) {
        test_hooks_->FirstOpIdOfSegment(segment_path, op_id);
      }
      const RestartSafeCoarseTimePoint first_op_time = first_op_metadata.entry_time;
      const auto retryable_requests_replay_from_this_or_earlier_time_was_initialized =
          retryable_requests_replay_from_this_or_earlier_time.is_initialized();

      if (!retryable_requests_replay_from_this_or_earlier_time_was_initialized) {
        retryable_requests_replay_from_this_or_earlier_time =
            first_op_time - retryable_requests_retain_interval;
      }

      const auto is_first_op_id_low_enough = op_id <= op_id_replay_lowest;
      const auto is_first_op_id_low_enough_for_retryable_requests =
          op_id <= last_op_id_in_retryable_requests;
      const auto is_first_op_time_early_enough_for_retryable_requests =
          first_op_time <= retryable_requests_replay_from_this_or_earlier_time;

      const auto common_details_str = [&]() {
        std::ostringstream ss;
        ss << YB_EXPR_TO_STREAM_COMMA_SEPARATED(
            op_id_replay_lowest,
            last_op_id_in_retryable_requests,
            first_op_time,
            retryable_requests_retain_interval,
            *retryable_requests_replay_from_this_or_earlier_time);
        return ss.str();
      };

      if (is_first_op_id_low_enough &&
          (is_first_op_id_low_enough_for_retryable_requests ||
              is_first_op_time_early_enough_for_retryable_requests)) {
        // If lazy superblock flush enabled, make sure at least two segments are replayed
        // if segments.size() >= kMinSegmentsToReplayWithLazySuperblockFlush.
        const auto older_segment_may_contain_unflushed_change_metadata_op =
            is_lazy_superblock_flush_enabled &&
            segments.end() - iter < kMinSegmentsToReplayWithLazySuperblockFlush;
        // Continue to older segment if it exists and it may contain unflushed change metadata op.
        if (older_segment_may_contain_unflushed_change_metadata_op &&
            iter != segments.begin()) {
          continue;
        }
        // If lazy superblock flush is enabled for colocated tables, it needs to replay at least
        // the last two segments so if the first segment to replay is the penultimate or the last
        // segment, then there's no need to get the corresponding log entry index to start with.
        // If is_first_op_id_low_enough_for_retryable_requests = true, it means the first segment
        // can be replayed from min(op_id_replay_lowest, last_op_id_in_retryable_requests) instead
        // of very beginning of it. This condition is always true if avoid bootstrapping retryable
        // requests where last_op_id_in_retryable_requests=OpId::Max().
        // If the first segment is unclosed, it needs read all entries to build footer anyways, so
        // it's also unnecessary to get the starting offset to start replaying.
        const auto first_segment = *iter;
        const auto current_segment_may_contain_unflushed_change_metadata_op =
            is_lazy_superblock_flush_enabled &&
            segments.end() - iter <= kMinSegmentsToReplayWithLazySuperblockFlush;
        if (FLAGS_skip_flushed_entries_in_first_replayed_segment &&
            is_first_op_id_low_enough_for_retryable_requests &&
            !current_segment_may_contain_unflushed_change_metadata_op &&
            first_segment->HasLogIndexInFooter()) {
          const auto first_op_index_to_replay =
              std::min(op_id_replay_lowest.index, last_op_id_in_retryable_requests.index);
          // Get the offset of the first mandatory op in the segment.
          const auto index_entry = log_->GetLogReader()->GetIndexEntry(
              first_op_index_to_replay, first_segment.get());
          if (index_entry.ok()) {
            DCHECK_EQ(index_entry->segment_sequence_number,
                      first_segment->header().sequence_number());
            first_op_to_replay_offset_ = index_entry->offset_in_segment;
            LOG_WITH_PREFIX(INFO) << Format("Found log index (seqno: $0 offset: $1) of the "
                                            "first mandatory op(index: $2) to replay in segment $3",
                                            index_entry->segment_sequence_number,
                                            index_entry->offset_in_segment,
                                            first_op_index_to_replay,
                                            first_segment->header().sequence_number());
          } else {
            LOG_WITH_PREFIX(DFATAL) << "Could not get index entry of op_index "
                                     << first_op_index_to_replay << ", status: "
                                     << index_entry.status();
          }
        }
        LOG_IF_WITH_PREFIX(INFO,
            !is_first_op_id_low_enough_for_retryable_requests &&
                is_first_op_time_early_enough_for_retryable_requests)
            << "Retryable requests file is too old, ignore the expired retryable requests. "
            << YB_EXPR_TO_STREAM_COMMA_SEPARATED(
                is_first_op_id_low_enough_for_retryable_requests,
                is_first_op_time_early_enough_for_retryable_requests);
        LOG_WITH_PREFIX(INFO)
            << kBootstrapOptimizerLogPrefix
            << "found first mandatory segment op id: " << op_id << ", "
            << common_details_str() << ", "
            << "number of segments to be skipped: " << (iter - segments.begin());
        return iter;
      }

      LOG_WITH_PREFIX(INFO)
          << "Segment " << segment_path << " cannot be used as the first segment to start replay "
          << "with according to our OpId and retention criteria. "
          << (iter == segments.begin()
                  ? "However, this is already the earliest segment so we have to start replay "
                    "here. We should probably investigate how we got into this situation. "
                  : "Continuing to earlier segments.")
          << YB_EXPR_TO_STREAM(op_id) << ", "
          << common_details_str() << ", "
          << YB_EXPR_TO_STREAM_COMMA_SEPARATED(
              is_first_op_id_low_enough_for_retryable_requests,
              is_first_op_time_early_enough_for_retryable_requests,
              is_first_op_id_low_enough);
    }

    LOG_WITH_PREFIX(INFO)
        << kBootstrapOptimizerLogPrefix
        << "will replay all segments starting from the very first one.";

    return iter;
  }

  // Plays the log segments into the tablet being built.  The process of playing the segments can
  // work in two modes:
  //
  // - With skip_wal_rewrite enabled (default mode):
  //   Reuses existing segments of the log, rebuilding log segment footers when necessary.
  //
  // - With skip_wal_rewrite disabled (legacy mode):
  //   Moves the old log to a "recovery directory" and replays entries from the old into a new log.
  //   This is very I/O-intensive. We should probably get rid of this mode eventually.
  //
  // The resulting log can be continued later on when then tablet is rebuilt and starts accepting
  // writes from clients.
  Status PlaySegments(ConsensusBootstrapInfo* consensus_info) {
    const auto flushed_op_ids = VERIFY_RESULT(GetFlushedOpIds());

    if (tablet_->snapshot_coordinator()) {
      // We should load transaction aware snapshots before replaying logs, because we need them
      // during this replay.
      RETURN_NOT_OK(tablet_->snapshot_coordinator()->Load(tablet_.get()));
    }

    replay_state_ = std::make_unique<ReplayState>(flushed_op_ids, LogPrefix());
    replay_state_->max_committed_hybrid_time = VERIFY_RESULT(tablet_->MaxPersistentHybridTime());

    if (FLAGS_force_recover_flushed_frontier) {
      LOG_WITH_PREFIX(WARNING)
          << "--force_recover_flushed_frontier specified, ignoring max committed hybrid time from  "
          << "RocksDB metadata (will replay all log records): "
          << replay_state_->max_committed_hybrid_time;
      replay_state_->max_committed_hybrid_time = HybridTime::kMin;
    } else {
      LOG_WITH_PREFIX(INFO) << "Max persistent index in RocksDB's SSTables before bootstrap: "
                            << "regular RocksDB: "
                            << replay_state_->stored_op_ids.regular << "; "
                            << "intents RocksDB: "
                            << replay_state_->stored_op_ids.intents;
    }

    // Open the log.
    //
    // If skip_wal_rewrite is true (default case), defer appending to this log until bootstrap is
    // finished to preserve the state of old log. In that case we don't need to create a new
    // segment until bootstrap is done.
    //
    // If skip_wal_rewrite is false, create a new segment and append each replayed entry to this
    // new log.
    RETURN_NOT_OK_PREPEND(
        OpenLog(log::CreateNewSegment(!GetAtomicFlag(&FLAGS_skip_wal_rewrite))),
          "Failed to open new log");

    log::SegmentSequence segments;
    RETURN_NOT_OK(log_->GetSegmentsSnapshot(&segments));

    // If any cdc stream is active for this tablet, we will read WAL from beginning when
    // FLAGS_skip_wal_replay_from_beginning_with_cdc is set to false.
    bool should_skip_flushed_entries = FLAGS_skip_flushed_entries;
    if (!GetAtomicFlag(&FLAGS_skip_wal_replay_from_beginning_with_cdc) &&
        should_skip_flushed_entries && tablet_->transaction_participant()) {
      if (tablet_->transaction_participant()->GetRetainOpId() != OpId::Invalid()) {
        should_skip_flushed_entries = false;
        LOG_WITH_PREFIX(WARNING) << "Ignoring skip_flushed_entries even though it is set, because "
                                 << "we need to scan all segments when any cdc stream is active "
                                 << "for this tablet.";
      }
    }
    // Find the earliest log segment we need to read, so the rest can be ignored.
    auto iter = should_skip_flushed_entries ? SkipFlushedEntries(&segments) : segments.begin();

    OpId last_committed_op_id;
    OpId last_read_entry_op_id;
    // All ops covered by retryable requests file are committed.
    OpId last_op_id_in_retryable_requests =
        data_.bootstrap_retryable_requests && data_.retryable_requests
            ? data_.retryable_requests->GetMaxReplicatedOpId()
            : OpId::Min();
    RestartSafeCoarseTimePoint last_entry_time;

    // If the segment is the first one to replay, start replaying from the last flushed op id
    // instead of very beginning of the segment.
    auto replay_from_offset = first_op_to_replay_offset_;
    for (; iter != segments.end(); ++iter, replay_from_offset = std::nullopt) {
      const scoped_refptr<ReadableLogSegment>& segment = *iter;
      auto read_result = segment->ReadEntries(
          /* max_entries_to_read = */ std::numeric_limits<int64_t>::max(),
          log::EntriesToRead::kAll, replay_from_offset);

      last_committed_op_id = std::max(
          std::max(last_committed_op_id, read_result.committed_op_id),
          last_op_id_in_retryable_requests);

      if (!read_result.entries.empty()) {
        last_read_entry_op_id = OpId::FromPB(read_result.entries.back()->replicate().id());
      }
      if (test_hooks_) {
        if (!read_result.entries.empty()) {
          const auto& first_read_entry_op_id = read_result.entries.front()->replicate().id();
          test_hooks_->FirstOpIdReadFromReplayedSegment(
              segment->path(), OpId::FromPB(first_read_entry_op_id));
        } else {
          test_hooks_->FirstOpIdReadFromReplayedSegment(segment->path(), OpId::Invalid());
        }
      }
      for (size_t entry_idx = 0; entry_idx < read_result.entries.size(); ++entry_idx) {
        const Status s = HandleEntry(
            read_result.entry_metadata[entry_idx], &read_result.entries[entry_idx]);
        if (!s.ok()) {
          LOG_WITH_PREFIX(INFO) << "Dumping replay state to log: " << s;
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

      // If the LogReader failed to read for some reason, we'll still try to replay as many entries
      // as possible, and then fail with Corruption.
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
        status += ", last entry metadata: " + read_result.entry_metadata.back().ToString() +
                  ", last read entry op id: " + last_read_entry_op_id.ToString();
      }
      listener_->StatusMessage(status);
    }

    replay_state_->UpdateCommittedFromStored();
    RETURN_NOT_OK(ApplyCommittedPendingReplicates());

    if (last_committed_op_id.index > replay_state_->committed_op_id.index) {
      auto it = replay_state_->pending_replicates.find(last_committed_op_id.index);
      if (it != replay_state_->pending_replicates.end()) {
        // That should be guaranteed by RAFT protocol. If record is committed, it cannot
        // be overriden by a new leader.
        if (last_committed_op_id.term == it->second.entry->replicate().id().term()) {
          replay_state_->UpdateCommittedOpId(last_committed_op_id);
          RETURN_NOT_OK(ApplyCommittedPendingReplicates());
        } else {
          DumpReplayStateToLog();
          LOG_WITH_PREFIX(DFATAL)
              << "Invalid last committed op id: " << last_committed_op_id
              << ", record with this index has another term: "
              << OpId::FromPB(it->second.entry->replicate().id());
        }
      } else {
        DumpReplayStateToLog();
        LOG_WITH_PREFIX(DFATAL)
            << "Does not have an entry for the last committed index: " << last_committed_op_id
            << ", entries: " << yb::ToString(replay_state_->pending_replicates);
      }
    }

    LOG_WITH_PREFIX(INFO) << "Dumping replay state to log at the end of " << __FUNCTION__;
    DumpReplayStateToLog();

    // Set up the ConsensusBootstrapInfo structure for the caller.
    for (auto& [index, entry] : replay_state_->pending_replicates) {
      // We only allow log entries with an index later than the index of the last log entry already
      // applied to RocksDB to be passed to the tablet as "orphaned replicates". This will make sure
      // we don't try to write to RocksDB with non-monotonic sequence ids, but still create
      // ConsensusRound instances for writes that have not been persisted into RocksDB.
      consensus_info->orphaned_replicates.emplace_back(
          entry.entry, entry.entry->mutable_replicate());
    }
    LOG_WITH_PREFIX(INFO)
        << "Number of orphaned replicates: " << consensus_info->orphaned_replicates.size()
        << ", last id: " << replay_state_->prev_op_id
        << ", committed id: " << replay_state_->committed_op_id;

    SCHECK_FORMAT(
        replay_state_->prev_op_id.term >= replay_state_->committed_op_id.term &&
            replay_state_->prev_op_id.index >= replay_state_->committed_op_id.index,
        IllegalState,
        "WAL files missing, or committed op id is incorrect. Expected both term and index "
            "of prev_op_id to be greater than or equal to the corresponding components of "
            "committed_op_id. prev_op_id=$0, committed_op_id=$1",
        replay_state_->prev_op_id, replay_state_->committed_op_id);

    tablet_->mvcc_manager()->SetLastReplicated(replay_state_->max_committed_hybrid_time);
    consensus_info->last_id = MakeOpIdPB(replay_state_->prev_op_id);
    consensus_info->last_committed_id = MakeOpIdPB(replay_state_->committed_op_id);

    if (data_.retryable_requests) {
      data_.retryable_requests->Clock().Adjust(last_entry_time);
    }

    // Update last_flushed_change_metadata_op_id if invalid so that next tablet bootstrap
    // takes advantage of the feature. After bootstrap, when orphaned replicates are applied,
    // we are sure that we are no worse than existing since their "Apply" goes
    // through the non-tablet-bootstrap change metadata route which is the same before/after.
    if (!tablet_->metadata()->LastFlushedChangeMetadataOperationOpId().valid()) {
      LOG(INFO) << "Updating last_flushed_change_metadata_op_id to "
                << replay_state_->committed_op_id
                << " so that subsequent bootstraps can start leveraging it";
      tablet_->metadata()->SetLastAppliedChangeMetadataOperationOpId(
          replay_state_->committed_op_id);
      RETURN_NOT_OK(tablet_->metadata()->Flush());
    }

    return Status::OK();
  }

  Status PlayWriteRequest(
      consensus::LWReplicateMsg* replicate_msg,
      const docdb::StorageSet& apply_to_storages) {
    SCHECK(replicate_msg->has_hybrid_time(), IllegalState,
           "A write operation with no hybrid time");

    auto* write = replicate_msg->mutable_write();

    SCHECK(write->has_write_batch(), Corruption, "A write request must have a write batch");

    WriteOperation operation(tablet_, write);
    operation.set_op_id(OpId::FromPB(replicate_msg->id()));
    HybridTime hybrid_time(replicate_msg->hybrid_time());
    operation.set_hybrid_time(hybrid_time);

    auto op_id = operation.op_id();
    tablet_->mvcc_manager()->AddFollowerPending(hybrid_time, op_id);

    if (test_hooks_ &&
        replicate_msg->has_write() &&
        replicate_msg->write().has_write_batch() &&
        replicate_msg->write().write_batch().has_transaction() &&
        test_hooks_->ShouldSkipWritingIntents()) {
      // Used in unit tests to avoid instantiating the entire transactional subsystem.
      tablet_->mvcc_manager()->Replicated(hybrid_time, op_id);
      return Status::OK();
    }

    auto apply_status = tablet_->ApplyRowOperations(&operation, apply_to_storages);
    // Failure is regular case, since could happen because transaction was aborted, while
    // replicating its intents.
    LOG_IF(INFO, !apply_status.ok()) << "Apply operation failed: " << apply_status;

    tablet_->mvcc_manager()->Replicated(hybrid_time, op_id);
    return Status::OK();
  }

  Status PlayChangeMetadataRequestDeprecated(consensus::LWReplicateMsg* replicate_msg) {
    LOG(INFO) << "last_flushed_change_metadata_op_id not set, replaying change metadata request"
              << " as before D19063";
    auto* request = replicate_msg->mutable_change_metadata_request();

    // Decode schema
    Schema schema;
    if (request->has_schema()) {
      RETURN_NOT_OK(SchemaFromPB(request->schema().ToGoogleProtobuf(), &schema));
    }

    ChangeMetadataOperation operation(request);

    // If table id isn't in metadata, ignore the replay as the table might've been dropped.
    auto table_info = meta_->GetTableInfo(operation.table_id().ToBuffer());
    if (!table_info.ok()) {
      LOG_WITH_PREFIX(WARNING) << "Table ID " << operation.table_id()
          << " not found in metadata, skipping this ChangeMetadataRequest";
      return Status::OK();
    }

    RETURN_NOT_OK(tablet_->CreatePreparedChangeMetadata(
        &operation, request->has_schema() ? &schema : nullptr, IsLeaderSide::kTrue));
    // Set invalid op id for the operation. This ensures that the operation(s)
    // replayed below do not update the last_applied_change_metadata_op_id thereby keeping
    // the old behavior for subsequent change metadata operation for this entire
    // round of tablet bootstrap.
    operation.set_op_id(OpId::Invalid());

    if (request->has_schema()) {
      // Apply the alter schema to the tablet.
      RETURN_NOT_OK_PREPEND(tablet_->AlterSchema(&operation), "Failed to AlterSchema:");

      // Also update the log information. Normally, the AlterSchema() call above takes care of this,
      // but our new log isn't hooked up to the tablet yet.
      log_->SetSchemaForNextLogSegment(schema, operation.schema_version());
    }

    if (request->has_wal_retention_secs()) {
      RETURN_NOT_OK_PREPEND(tablet_->AlterWalRetentionSecs(&operation),
                            "Failed to alter wal retention secs");
      log_->set_wal_retention_secs(request->wal_retention_secs());
    }

    return Status::OK();
  }

  Status PlayChangeMetadataRequest(consensus::LWReplicateMsg* replicate_msg) {
    // This is to handle the upgrade case when new code runs against old data.
    // In such cases, last_flushed_change_metadata_op_id won't be set and we want
    // to ensure that we are no worse than the behavior as of the older version.
    if (!meta_->LastFlushedChangeMetadataOperationOpId().valid()) {
      return PlayChangeMetadataRequestDeprecated(replicate_msg);
    }

    // If last_flushed_change_metadata_op_id is valid then new code gets executed
    // wherein we replay everything.
    const auto op_id = OpId::FromPB(replicate_msg->id());

    // Otherwise play.
    auto* request = replicate_msg->mutable_change_metadata_request();

    ChangeMetadataOperation operation(tablet_, log_.get(), request);
    operation.set_op_id(op_id);
    RETURN_NOT_OK(operation.Prepare(tablet::IsLeaderSide::kFalse));

    Status s;
    RETURN_NOT_OK(operation.Apply(OpId::kUnknownTerm, &s));
    return s;
  }

  Status PlayChangeConfigRequest(consensus::LWReplicateMsg* replicate_msg) {
    auto* change_config = replicate_msg->mutable_change_config_record();
    RaftConfigPB config = change_config->new_config().ToGoogleProtobuf();

    int64_t cmeta_opid_index =  cmeta_->committed_config().opid_index();
    if (replicate_msg->id().index() > cmeta_opid_index) {
      SCHECK(!config.has_opid_index(),
             Corruption,
             "A config change record must have an opid_index");
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

  Status PlayTruncateRequest(consensus::LWReplicateMsg* replicate_msg) {
    auto* req = replicate_msg->mutable_truncate();

    TruncateOperation operation(tablet_, req);

    operation.set_op_id(OpId::FromPB(replicate_msg->id()));
    operation.set_hybrid_time(HybridTime::FromPB(replicate_msg->hybrid_time()));

    Status s = tablet_->Truncate(&operation);

    RETURN_NOT_OK_PREPEND(s, "Failed to Truncate:");

    return Status::OK();
  }

  Status PlayUpdateTransactionRequest(
      consensus::LWReplicateMsg* replicate_msg,
      const docdb::StorageSet& apply_to_storages) {
    SCHECK(replicate_msg->has_hybrid_time(),
           Corruption, "A transaction update request must have a hybrid time");

    UpdateTxnOperation operation(
        /* tablet */ nullptr, replicate_msg->mutable_transaction_state());
    operation.set_op_id(OpId::FromPB(replicate_msg->id()));
    HybridTime hybrid_time(replicate_msg->hybrid_time());
    operation.set_hybrid_time(hybrid_time);

    auto op_id = OpId::FromPB(replicate_msg->id());
    tablet_->mvcc_manager()->AddFollowerPending(hybrid_time, op_id);
    auto scope_exit = ScopeExit([this, hybrid_time, op_id] {
      tablet_->mvcc_manager()->Replicated(hybrid_time, op_id);
    });

    if (test_hooks_ && test_hooks_->ShouldSkipTransactionUpdates()) {
      // Used in tests where we don't have transaction participant instantiated.
      return Status::OK();
    }

    auto transaction_participant = tablet_->transaction_participant();
    if (transaction_participant) {
      TransactionParticipant::ReplicatedData replicated_data = {
        .leader_term = OpId::kUnknownTerm,
        .state = *operation.request(),
        .op_id = operation.op_id(),
        .hybrid_time = operation.hybrid_time(),
        .sealed = operation.request()->sealed(),
        .apply_to_storages = apply_to_storages,
      };
      return transaction_participant->ProcessReplicated(replicated_data);
    }

    auto transaction_coordinator = tablet_->transaction_coordinator();
    if (!transaction_coordinator) {
      return STATUS(
          IllegalState,
          "No transaction coordinator or participant, cannot process a transaction update request");
    }
    TransactionCoordinator::ReplicatedData replicated_data = {
        .leader_term = OpId::kUnknownTerm,
        .state = *operation.request(),
        .op_id = operation.op_id(),
        .hybrid_time = operation.hybrid_time(),
    };
    return transaction_coordinator->ProcessReplicated(replicated_data);
  }

  // Decodes a HybridTime from the provided string and updates the clock with it.
  void UpdateClock(uint64_t hybrid_time) {
    data_.tablet_init_data.clock->Update(HybridTime(hybrid_time));
  }

  // Return a log prefix string in the standard "T xxx P yyy" format.
  std::string LogPrefix() const {
    return consensus::MakeTabletLogPrefix(meta_->raft_group_id(), meta_->fs_manager()->uuid());
  }

  Env* GetEnv() {
    if (data_.tablet_init_data.tablet_options.env) {
      return data_.tablet_init_data.tablet_options.env;
    }
    return meta_->fs_manager()->env();
  }

  void CleanupSnapshots() {
    // Disk clean-up: deleting temporary/incomplete snapshots.
    const string top_snapshots_dir = TabletSnapshots::SnapshotsDirName(meta_->rocksdb_dir());

    if (meta_->fs_manager()->env()->FileExists(top_snapshots_dir)) {
      vector<string> snapshot_dirs;
      Status s = meta_->fs_manager()->env()->GetChildren(
          top_snapshots_dir, ExcludeDots::kTrue, &snapshot_dirs);

      if (!s.ok()) {
        LOG_WITH_PREFIX(WARNING) << "Cannot get list of snapshot directories in "
                                 << top_snapshots_dir << ": " << s;
      } else {
        for (const string& dir_name : snapshot_dirs) {
          const string snapshot_dir = JoinPathSegments(top_snapshots_dir, dir_name);

          if (TabletSnapshots::IsTempSnapshotDir(snapshot_dir)) {
            LOG_WITH_PREFIX(INFO) << "Deleting old temporary snapshot directory " << snapshot_dir;

            s = meta_->fs_manager()->env()->DeleteRecursively(snapshot_dir);
            if (!s.ok()) {
              LOG_WITH_PREFIX(WARNING) << "Cannot delete old temporary snapshot directory "
                                       << snapshot_dir << ": " << s;
            }

            s = meta_->fs_manager()->env()->SyncDir(top_snapshots_dir);
            if (!s.ok()) {
              LOG_WITH_PREFIX(WARNING) << "Cannot sync top snapshots dir " << top_snapshots_dir
                                       << ": " << s;
            }
          }
        }
      }
    }
  }

  // Goes through the contiguous prefix of pending_replicates and applies those that are committed
  // by calling MaybeReplayCommittedEntry.
  Status ApplyCommittedPendingReplicates() {
    auto& pending_replicates = replay_state_->pending_replicates;
    auto iter = pending_replicates.begin();
    while (iter != pending_replicates.end() && replay_state_->CanApply(*iter->second.entry)) {
      VLOG_WITH_PREFIX(1) << "Applying committed pending replicate "
                          << OpId::FromPB(iter->second.entry->replicate().id());
      RETURN_NOT_OK(MaybeReplayCommittedEntry(iter->second.entry, iter->second.entry_time));
      iter = pending_replicates.erase(iter);  // erase and advance the iterator (C++11)
      ++replay_state_->num_entries_applied_to_rocksdb;
    }
    return Status::OK();
  }

  // ----------------------------------------------------------------------------------------------
  // Member fields
  // ----------------------------------------------------------------------------------------------

  BootstrapTabletData data_;
  RaftGroupMetadataPtr meta_;
  std::shared_ptr<MemTracker> mem_tracker_;
  TabletStatusListener* listener_;
  TabletPtr tablet_;
  scoped_refptr<log::Log> log_;
  std::unique_ptr<log::LogReader> log_reader_;
  std::unique_ptr<ReplayState> replay_state_;

  consensus::ConsensusMetadata* cmeta_;
  std::unique_ptr<consensus::ConsensusMetadata> cmeta_holder_;

  // Thread pool for append task for bootstrap.
  ThreadPool* append_pool_;

  ThreadPool* allocation_pool_;

  // Thread pool for executing log fsync tasks.
  ThreadPool* log_sync_pool_;

  // Statistics on the replay of entries in the log.
  struct Stats {
    std::string ToString() const;

    // Number of REPLICATE messages read from the log
    int ops_read = 0;

    // Number of REPLICATE messages which were overwritten by later entries.
    int ops_overwritten = 0;
  } stats_;

  HybridTime rocksdb_last_entry_hybrid_time_ = HybridTime::kMin;

  log::SkipWalWrite skip_wal_rewrite_;

  // A way to inject flushed OpIds for regular and intents RocksDBs.
  boost::optional<DocDbOpIds> TEST_docdb_flushed_op_ids_;

  bool TEST_collect_replayed_op_ids_;

  // This is populated if TEST_collect_replayed_op_ids is true.
  std::vector<OpId> TEST_replayed_op_ids_;

  std::shared_ptr<TabletBootstrapTestHooksIf> test_hooks_;

  // If this offset is specified, when replaying the first segment that satisfies the
  // bootstrap criteria, only read and apply entries from this offset.
  std::optional<int64_t> first_op_to_replay_offset_ = std::nullopt;

  DISALLOW_COPY_AND_ASSIGN(TabletBootstrap);
};

// ============================================================================
//  Class TabletBootstrap::Stats.
// ============================================================================

string TabletBootstrap::Stats::ToString() const {
  return Format("Read operations: $0, overwritten operations: $1",
                ops_read, ops_overwritten);
}

Status BootstrapTabletImpl(
    const BootstrapTabletData& data,
    TabletPtr* rebuilt_tablet,
    scoped_refptr<log::Log>* rebuilt_log,
    consensus::ConsensusBootstrapInfo* results) {
  TabletBootstrap tablet_bootstrap(data);
  auto bootstrap_status = tablet_bootstrap.Bootstrap(rebuilt_tablet, rebuilt_log, results);
  if (!bootstrap_status.ok()) {
    LOG(WARNING) << "T " << (*rebuilt_tablet ? (*rebuilt_tablet)->tablet_id() : "N/A")
                 << " Tablet bootstrap failed: " << bootstrap_status;
  }
  return bootstrap_status;
}

} // namespace tablet
} // namespace yb
