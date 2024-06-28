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

class TabletBootstrapStateManager {
 public:
  TabletBootstrapStateManager();

  TabletBootstrapStateManager(
    const TabletId& tablet_id, FsManager* const fs_manager, const std::string& wal_dir);

  Status Init();

  FsManager* fs_manager() const { return fs_manager_; }

  static std::string FilePath(const std::string& path) {
    return JoinPathSegments(path, FileName());
  }

  bool has_file_on_disk() const {
    return has_file_on_disk_;
  }

  // Flush the pb as the latest version.
  Status SaveToDisk(consensus::RaftConsensus& raft_consensus);

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

  TabletId tablet_id_;

  bool has_file_on_disk_ = false;
  FsManager* fs_manager_ = nullptr;
  std::string dir_;

  static constexpr char kSuffixNew[] = ".NEW";
  static constexpr char kTabletBootstrapStateFileName[] = "retryable_requests";
};

} // namespace yb::tablet
