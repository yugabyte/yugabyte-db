// Copyright (c) YugabyteDB, Inc.
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

#include "yb/tablet/tablet_bootstrap_state_manager.h"

#include "yb/ash/wait_state.h"

#include "yb/common/opid.h"

#include "yb/consensus/consensus_util.h"
#include "yb/consensus/raft_consensus.h"
#include "yb/consensus/retryable_requests.h"
#include "yb/consensus/opid_util.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/util/debug-util.h"
#include "yb/util/env_util.h"

namespace yb::tablet {

TabletBootstrapState::TabletBootstrapState(const TabletBootstrapState& rhs)
    : min_replay_txn_first_write_ht_(rhs.min_replay_txn_first_write_ht_.load()) {}

void TabletBootstrapState::ToPB(consensus::TabletBootstrapStatePB* pb) const {
  pb->set_min_replay_txn_first_write_ht(min_replay_txn_first_write_ht_.load().ToUint64());
}

void TabletBootstrapState::FromPB(const consensus::TabletBootstrapStatePB& pb) {
  min_replay_txn_first_write_ht_.store(HybridTime::FromPB(pb.min_replay_txn_first_write_ht()));
}

TabletBootstrapStateManager::TabletBootstrapStateManager() { }

TabletBootstrapStateManager::TabletBootstrapStateManager(
    const TabletId& tablet_id, FsManager* const fs_manager, const std::string& wal_dir)
    : tablet_id_(tablet_id), fs_manager_(fs_manager), dir_(wal_dir),
      log_prefix_(consensus::MakeTabletLogPrefix(tablet_id, fs_manager->uuid())) {
    }

Status TabletBootstrapStateManager::Init() {
  CHECK(!dir_.empty());
  if (!fs_manager_->Exists(dir_)) {
    LOG_WITH_PREFIX(INFO)
        << "Wal dir is not created, skip initializing TabletBootstrapStateManager";
    // For first startup.
    has_file_on_disk_ = false;
    return Status::OK();
  }
  RETURN_NOT_OK(DoInit());
  LOG_WITH_PREFIX(INFO) << "Initialized TabletBootstrapStateManager, found a file ? "
                        << (has_file_on_disk_ ? "yes" : "no")
                        << ", wal dir=" << dir_;
  return Status::OK();
}

Status TabletBootstrapStateManager::SaveToDisk(
    const TabletWeakPtr& tablet_ptr, consensus::RaftConsensus& raft_consensus) {
  auto retryable_requests = VERIFY_RESULT(raft_consensus.TakeSnapshotOfRetryableRequests());
  if (!retryable_requests) {
    LOG_WITH_PREFIX(INFO) << "Nothing to save";
    return Status::OK();
  }

  auto max_replicated_op_id = retryable_requests->GetMaxReplicatedOpId();

  TabletBootstrapState bootstrap_state(bootstrap_state_);

  // Set min replay txn start time to what it will be after this flush succeeds - this is safe
  // because if the flush succeeds, replay start op id will be calculated from the new value.
  auto tablet = tablet_ptr.lock();
  TransactionParticipant* participant = nullptr;
  if (tablet) {
    participant = tablet->transaction_participant();
    if (participant) {
      auto first_write_ht = VERIFY_RESULT(participant->SimulateProcessRecentlyAppliedTransactions(
          max_replicated_op_id));
      VLOG_WITH_PREFIX(1) << "Using min_replay_txn_first_write_ht = " << first_write_ht;
      bootstrap_state.SetMinReplayTxnFirstWriteTime(first_write_ht);
    }
  }

  consensus::TabletBootstrapStatePB pb;
  retryable_requests->ToPB(&pb);
  bootstrap_state.ToPB(&pb);

  auto path = NewFilePath();
  LOG_WITH_PREFIX(INFO) << "Saving bootstrap state up to " << pb.last_op_id() << " to " << path;
  auto* env = fs_manager()->env();
  SCOPED_WAIT_STATUS(RetryableRequests_SaveToDisk);
  RETURN_NOT_OK_PREPEND(pb_util::WritePBContainerToPath(
                            env, path, pb,
                            pb_util::OVERWRITE, pb_util::SYNC),
                            "Failed to write bootstrap state to disk");
  // Delete the current file and rename new file to current file.
  if (has_file_on_disk_) {
    RETURN_NOT_OK(env->DeleteFile(CurrentFilePath()));
  }
  LOG_WITH_PREFIX(INFO) << "Renaming " << NewFileName() << " to " << FileName();
  RETURN_NOT_OK(env->RenameFile(NewFilePath(), CurrentFilePath()));
  has_file_on_disk_ = true;
  RETURN_NOT_OK(env->SyncDir(dir_));

  RETURN_NOT_OK(raft_consensus.SetLastFlushedOpIdInRetryableRequests(max_replicated_op_id));

  if (participant) {
    VLOG_WITH_PREFIX(1)
        << "Bootstrap state saved to disk, triggering cleanup of recently applied transactions";
    participant->SetRetryableRequestsFlushedOpId(max_replicated_op_id);
    return participant->ProcessRecentlyAppliedTransactions();
  }

  return Status::OK();
}

Result<consensus::TabletBootstrapStatePB> TabletBootstrapStateManager::LoadFromDisk() {
  if (!has_file_on_disk_) {
    return STATUS(NotFound, "Bootstrap state has not been flushed");
  }
  consensus::TabletBootstrapStatePB pb;
  auto path = CurrentFilePath();
  RETURN_NOT_OK_PREPEND(
      pb_util::ReadPBContainerFromPath(fs_manager()->env(), path, &pb),
      Format("Could not load bootstrap state from $0", path));
  bootstrap_state_.FromPB(pb);
  LOG_WITH_PREFIX(INFO) << Format("Loaded tablet ($0) bootstrap state "
                      "(max_replicated_op_id_=$1) from $2",
                      tablet_id_, pb.last_op_id(), path);
  return pb;
}

Status TabletBootstrapStateManager::CopyTo(const std::string& dest_path) {
  if (!has_file_on_disk_) {
    return STATUS_FORMAT(NotFound, "Bootstrap state has not been flushed");
  }
  auto* env = fs_manager()->env();
  auto path = CurrentFilePath();
  auto dest_path_tmp = pb_util::MakeTempPath(dest_path);
  LOG_WITH_PREFIX(INFO) << "Copying bootstrap state from " << path << " to " << dest_path;
  DCHECK(fs_manager()->Exists(path));

  WritableFileOptions options;
  options.sync_on_close = true;
  RETURN_NOT_OK(env_util::CopyFile(
      fs_manager()->env(), path, dest_path_tmp, options));
  RETURN_NOT_OK(env->RenameFile(dest_path_tmp, dest_path));
  return env->SyncDir(dir_);
}

Status TabletBootstrapStateManager::DoInit() {
  auto* env = fs_manager_->env();
  // Do cleanup - delete temp new file if it exists.
  auto temp_file_path = pb_util::MakeTempPath(NewFilePath());
  if (env->FileExists(temp_file_path)) {
    RETURN_NOT_OK(env->DeleteFile(temp_file_path));
  }
  bool has_current = env->FileExists(CurrentFilePath());
  bool has_new = env->FileExists(NewFilePath());
  if (has_new) {
    // Should always load from the new file if it exists.
    if (has_current) {
      // If the current file exists, should delete it and rename the
      // new file to current file.
      RETURN_NOT_OK(env->DeleteFile(CurrentFilePath()));
    }
    RETURN_NOT_OK(env->RenameFile(NewFilePath(), CurrentFilePath()));
  }
  has_file_on_disk_ = has_new || has_current;
  return env->SyncDir(dir_);
}

} // namespace yb::tablet
