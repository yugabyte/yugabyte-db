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

#pragma once

#include "yb/common/entity_ids_types.h"
#include "yb/common/hybrid_time.h"

#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/consensus_fwd.h"

#include "yb/fs/fs_manager.h"

#include "yb/server/server_fwd.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/util/pb_util.h"
#include "yb/util/status_fwd.h"

namespace yb::tablet {

class TabletBootstrapState {
 public:
  TabletBootstrapState() = default;
  ~TabletBootstrapState() = default;

  TabletBootstrapState(const TabletBootstrapState& rhs);

  void SetMinReplayTxnFirstWriteTime(HybridTime min_replay_txn_first_write_ht) {
    min_replay_txn_first_write_ht_.store(min_replay_txn_first_write_ht);
  }

  HybridTime GetMinReplayTxnFirstWriteTime() const { return min_replay_txn_first_write_ht_.load(); }

  void ToPB(consensus::TabletBootstrapStatePB* pb) const;
  void FromPB(const consensus::TabletBootstrapStatePB& pb);

 private:
  std::atomic<HybridTime> min_replay_txn_first_write_ht_{HybridTime::kInvalid};
};

class TabletBootstrapStateManager {
 public:
  TabletBootstrapStateManager();

  TabletBootstrapStateManager(
    const TabletId& tablet_id, FsManager* const fs_manager, const std::string& wal_dir);

  Status Init();

  FsManager* fs_manager() const { return fs_manager_; }
  const TabletBootstrapState& bootstrap_state() const { return bootstrap_state_; }
  TabletBootstrapState& bootstrap_state() { return bootstrap_state_; }

  static std::string FilePath(const std::string& path) {
    return JoinPathSegments(path, FileName());
  }

  bool has_file_on_disk() const {
    return has_file_on_disk_;
  }

  // Flush the pb as the latest version.
  Status SaveToDisk(const TabletWeakPtr& tablet_ptr, consensus::RaftConsensus& raft_consensus);

  // Load the latest version from disk if any.
  Result<consensus::TabletBootstrapStatePB> LoadFromDisk();

  // Copy the latest version to dest_path.
  Status CopyTo(const std::string& dest_path);

 private:
  std::string CurrentFilePath() {
    return FilePath(dir_);
  }

  std::string NewFilePath() {
    return JoinPathSegments(dir_, NewFileName());
  }

  static std::string FileName() {
    return kTabletBootstrapStateFileName;
  }

  static std::string NewFileName() {
    return FileName() + kSuffixNew;
  }

  // Do the actual initialization.
  // Find the valid bootstrap state file from disk and delete other versions.
  Status DoInit();

  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  TabletId tablet_id_;

  bool has_file_on_disk_ = false;
  FsManager* fs_manager_ = nullptr;
  std::string dir_;
  TabletBootstrapState bootstrap_state_;
  std::string log_prefix_;

  static constexpr char kSuffixNew[] = ".NEW";
  static constexpr char kTabletBootstrapStateFileName[] = "retryable_requests";
};

} // namespace yb::tablet
