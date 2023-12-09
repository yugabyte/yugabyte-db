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

#include "yb/consensus/retryable_requests.h"

#include "yb/util/status_fwd.h"
#include "yb/util/threadpool.h"

namespace yb {
namespace tablet {

// State change:
// submit flush task: IDLE -> SUBMITTED
// submit task failed: SUBMITTED -> IDLE
// do flush work in thread pool: SUMITTED -> FLUSHING
// do flush work synchronously: IDLE -> FLUSHING
// flush is done: FLUSHING -> IDLE
// read retryable requests: IDLE -> READING
// read is done: READING -> IDLE
// shutdown: IDLE -> SHUTDOWN
YB_DEFINE_ENUM(RetryableRequestsFlushState,
               (kFlushIdle)(kFlushSubmitted)(kFlushing)(kReading)(kShutdown));

class RetryableRequestsFlusher : public std::enable_shared_from_this<RetryableRequestsFlusher> {
 public:
  RetryableRequestsFlusher(
      const std::string& tablet_id,
      std::shared_ptr<consensus::RaftConsensus> raft_consensus,
      std::unique_ptr<ThreadPoolToken> flush_retryable_requests_pool_token)
      : tablet_id_(tablet_id),
        raft_consensus_(raft_consensus),
        flush_retryable_requests_pool_token_(std::move(flush_retryable_requests_pool_token)) {}

  Status FlushRetryableRequests(
      RetryableRequestsFlushState expected = RetryableRequestsFlushState::kFlushIdle);
  Status SubmitFlushRetryableRequestsTask();
  Result<OpId> CopyRetryableRequestsTo(const std::string& dest_path);
  OpId GetMaxReplicatedOpId();

  void Shutdown();

  bool TEST_HasRetryableRequestsOnDisk();

  bool TEST_IsFlushing() {
    return flush_state() == RetryableRequestsFlushState::kFlushing;
  }

 private:
  bool TransferState(RetryableRequestsFlushState old_state, RetryableRequestsFlushState new_state);
  bool SetFlushing(bool expect_idle, RetryableRequestsFlushState* old_value);
  bool SetSubmitted();
  void SetIdle();
  bool SetReading();
  bool SetShutdown();
  void WaitForFlushIdle() const;

  RetryableRequestsFlushState flush_state() const {
    return flush_state_.load(std::memory_order_acquire);
  }

  // Used to notify waiters when each flush is done.
  mutable std::mutex flush_mutex_;
  mutable std::condition_variable flush_cond_;
  std::atomic<RetryableRequestsFlushState> flush_state_{RetryableRequestsFlushState::kFlushIdle};
  TabletId tablet_id_;
  std::shared_ptr<consensus::RaftConsensus> raft_consensus_;
  std::unique_ptr<ThreadPoolToken> flush_retryable_requests_pool_token_;
};

} // namespace tablet
} // namespace yb
