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

class RetryableRequestsFlusher : public std::enable_shared_from_this<RetryableRequestsFlusher> {
 public:
  RetryableRequestsFlusher(
      const std::string& tablet_id,
      std::shared_ptr<consensus::RaftConsensus> raft_consensus,
      std::unique_ptr<ThreadPoolToken> flush_retryable_requests_pool_token)
      : tablet_id_(tablet_id),
        raft_consensus_(raft_consensus),
        flush_retryable_requests_pool_token_(std::move(flush_retryable_requests_pool_token)) {}

  Status FlushRetryableRequests();
  Status SubmitFlushRetryableRequestsTask();
  Result<OpId> CopyRetryableRequestsTo(const std::string& dest_path);
  OpId GetMaxReplicatedOpId();

  bool TEST_HasRetryableRequestsOnDisk();

 private:
  bool SetFlushing() {
    bool flushing = false;
    return flushing_.compare_exchange_strong(flushing, true, std::memory_order_acq_rel);
  }

  bool UnsetFlushing() {
    bool flushing = true;
    return flushing_.compare_exchange_strong(flushing, false, std::memory_order_acq_rel);
  }

  std::atomic<bool> flushing_{false};
  TabletId tablet_id_;
  std::shared_ptr<consensus::RaftConsensus> raft_consensus_;
  std::unique_ptr<ThreadPoolToken> flush_retryable_requests_pool_token_;
};

} // namespace tablet
} // namespace yb
