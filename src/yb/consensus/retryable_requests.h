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
#include "yb/common/retryable_request.h"
#include "yb/consensus/consensus_fwd.h"

#include "yb/fs/fs_manager.h"

#include "yb/server/server_fwd.h"
#include "yb/tablet/operations/operation.h"

#include "yb/util/mem_tracker.h"
#include "yb/util/pb_util.h"
#include "yb/util/restart_safe_clock.h"
#include "yb/util/status_fwd.h"

namespace yb {

class MetricEntity;
struct OpId;

namespace consensus {

struct RetryableRequestsCounts {
  size_t running = 0;
  size_t replicated = 0;
};

// Holds information about retryable requests.
class RetryableRequests {
 public:
  explicit RetryableRequests(const MemTrackerPtr& tablet_mem_tracker,
                             std::string log_prefix = std::string());
  ~RetryableRequests();

  RetryableRequests(const RetryableRequests& rhs);

  RetryableRequests(RetryableRequests&& rhs);
  void operator=(RetryableRequests&& rhs);

  void CopyFrom(const RetryableRequests& rhs);

  OpId GetMaxReplicatedOpId() const;
  void SetLastFlushedOpId(const OpId& op_id);
  OpId GetLastFlushedOpId() const;
  void ToPB(RetryableRequestsPB* pb) const;
  void FromPB(const RetryableRequestsPB& pb);
  bool HasUnflushedData() const;

  // Tries to register a new running retryable request.
  // Returns error or false if request with such id is already present.
  Result<bool> Register(
      const ConsensusRoundPtr& round,
      tablet::IsLeaderSide is_leader_side,
      RestartSafeCoarseTimePoint entry_time = RestartSafeCoarseTimePoint());

  // Cleans expires replicated requests and returns min op id of running request.
  OpId CleanExpiredReplicatedAndGetMinOpId();

  // Mark appropriate request as replicated, i.e. move it from set of running requests to
  // replicated.
  void ReplicationFinished(
      const LWReplicateMsg& replicate_msg, const Status& status, int64_t leader_term);

  // Adds new replicated request that was loaded during tablet bootstrap.
  void Bootstrap(const LWReplicateMsg& replicate_msg, RestartSafeCoarseTimePoint entry_time);

  RestartSafeCoarseMonoClock& Clock();

  // Returns number or running requests and number of ranges of replicated requests.
  RetryableRequestsCounts TEST_Counts();

  Result<RetryableRequestId> MinRunningRequestId(const ClientId& client_id) const;

  void SetMetricEntity(const scoped_refptr<MetricEntity>& metric_entity);

  void set_log_prefix(const std::string& log_prefix);

  void SetServerClock(const server::ClockPtr& clock);

  void SetRequestTimeout(int timeout_secs);
  int request_timeout_secs();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

// This class is not thread safe, the upper layer should have correct control.
class RetryableRequestsManager {
 public:
  RetryableRequestsManager() {}

  RetryableRequestsManager(
      const TabletId& tablet_id, FsManager* const fs_manager, const std::string& wal_dir,
      const MemTrackerPtr& mem_tracker, const std::string log_prefix);

  Status Init(const server::ClockPtr& clock);

  FsManager* fs_manager() const { return fs_manager_; }
  const RetryableRequests& retryable_requests() const { return *retryable_requests_; }
  RetryableRequests& retryable_requests() { return *retryable_requests_; }

  static std::string FilePath(const std::string& path) {
    return JoinPathSegments(path, FileName());
  }

  bool HasUnflushedData() const { return retryable_requests_->HasUnflushedData(); }

  bool has_file_on_disk() const {
    return has_file_on_disk_;
  }

  // Flush the pb as the latest version.
  Status SaveToDisk(std::unique_ptr<RetryableRequests> retryable_requests);

  // Load the latest version from disk if any.
  Status LoadFromDisk();

  // Copy the latest version to dest_path.
  Status CopyTo(const std::string& dest_path);

  // Take the snapshot of the retryable requests, return the copy if success.
  std::unique_ptr<RetryableRequests> TakeSnapshotOfRetryableRequests();

  void set_log_prefix(const std::string& log_prefix);

  std::string LogPrefix() const;

 private:
  std::string CurrentFilePath() {
    return FilePath(dir_);
  }

  std::string NewFilePath() {
    return JoinPathSegments(dir_, NewFileName());
  }

  static std::string FileName() {
    return kRetryableRequestsFileName;
  }

  static std::string NewFileName() {
    return FileName() + kSuffixNew;
  }

  // Do the actual initialization.
  // Find the valid retryable requests file from disk and delete other versions.
  Status DoInit();

  bool has_file_on_disk_ = false;
  TabletId tablet_id_;
  FsManager* fs_manager_ = nullptr;
  std::string dir_;
  std::unique_ptr<RetryableRequests> retryable_requests_;
  std::string log_prefix_;

  static constexpr char kSuffixNew[] = ".NEW";
  static constexpr char kRetryableRequestsFileName[] = "retryable_requests";
};

} // namespace consensus
} // namespace yb
