// Copyright (c) YugaByte, Inc.
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

#include "yb/consensus/raft_consensus.h"
#include "yb/consensus/retryable_requests.h"
#include "yb/consensus/opid_util.h"

#include "yb/util/debug-util.h"
#include "yb/util/env_util.h"

namespace yb::tablet {

TabletBootstrapStateManager::TabletBootstrapStateManager() { }

TabletBootstrapStateManager::TabletBootstrapStateManager(
    const TabletId& tablet_id, FsManager* const fs_manager, const std::string& wal_dir)
    : tablet_id_(tablet_id), fs_manager_(fs_manager), dir_(wal_dir) { }

Status TabletBootstrapStateManager::Init() {
  CHECK(!dir_.empty());
  if (!fs_manager_->Exists(dir_)) {
    LOG(INFO) << "Wal dir is not created, skip initializing TabletBootstrapStateManager for "
              << tablet_id_;
    // For first startup.
    has_file_on_disk_ = false;
    return Status::OK();
  }
  RETURN_NOT_OK(DoInit());
  LOG(INFO) << "Initialized TabletBootstrapStateManager, found a file ? "
            << (has_file_on_disk_ ? "yes" : "no")
            << ", wal dir=" << dir_;
  return Status::OK();
}

Status TabletBootstrapStateManager::SaveToDisk(consensus::RaftConsensus& raft_consensus) {
  auto retryable_requests = VERIFY_RESULT(raft_consensus.TakeSnapshotOfRetryableRequests());
  if (!retryable_requests) {
    LOG(INFO) << "Nothing to save";
    return Status::OK();
  }

  consensus::TabletBootstrapStatePB pb;
  retryable_requests->ToPB(&pb);

  auto path = NewFilePath();
  LOG(INFO) << "Saving bootstrap state up to " << pb.last_op_id() << " to " << path;
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
  LOG(INFO) << "Renaming " << NewFileName() << " to " << FileName();
  RETURN_NOT_OK(env->RenameFile(NewFilePath(), CurrentFilePath()));
  has_file_on_disk_ = true;
  RETURN_NOT_OK(env->SyncDir(dir_));

  auto max_replicated_op_id = retryable_requests->GetMaxReplicatedOpId();
  return raft_consensus.SetLastFlushedOpIdInRetryableRequests(max_replicated_op_id);
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
  LOG(INFO) << Format("Loaded tablet ($0) bootstrap state "
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
  LOG(INFO) << "Copying bootstrap state from " << path << " to " << dest_path;
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
