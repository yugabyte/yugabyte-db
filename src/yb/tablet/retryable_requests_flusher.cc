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

#include "yb/tablet/retryable_requests_flusher.h"
#include "yb/consensus/raft_consensus.h"

#include "yb/util/debug-util.h"
#include "yb/util/scope_exit.h"

DEFINE_RUNTIME_AUTO_bool(enable_flush_retryable_requests, kLocalPersisted, false, true,
    "If enabled, will flush retryable requests structure to the disk when roll the log segment, "
    "which helps speedup bootstrap process");

DEFINE_test_flag(bool, pause_before_flushing_retryable_requests, false,
    "Test only: whether to pause before actually doing FlushRetryableRequests");

DEFINE_test_flag(bool, pause_before_submitting_flush_retryable_requests, false,
    "Test only: whether to pause before actually doing SubmitFlushRetryableRequestsTask");

DEFINE_test_flag(bool, pause_before_copying_retryable_requests, false,
    "Test only: whether to pause before actually doing CopyRetryableRequestsTo");

using namespace std::literals;

namespace yb {
namespace tablet {

// Transfer flush_state_ from old_state to new_state.
// If it failed, old_state will be updated to the current value of flush_state_.
bool RetryableRequestsFlusher::TransferState(
    RetryableRequestsFlushState* old_state, RetryableRequestsFlushState new_state) {
  return flush_state_.compare_exchange_strong(
      *old_state, new_state, std::memory_order_acq_rel);
}

bool RetryableRequestsFlusher::SetFlushing(
    bool expect_idle, RetryableRequestsFlushState* old_state) {
  *old_state = expect_idle ?
      RetryableRequestsFlushState::kFlushIdle : RetryableRequestsFlushState::kFlushSubmitted;
  return TransferState(old_state, RetryableRequestsFlushState::kFlushing);
}

bool RetryableRequestsFlusher::SetSubmitted(RetryableRequestsFlushState* old_state) {
  *old_state = RetryableRequestsFlushState::kFlushIdle;
  return TransferState(old_state, RetryableRequestsFlushState::kFlushSubmitted);
}

void RetryableRequestsFlusher::SetIdle() {
  flush_state_.store(RetryableRequestsFlushState::kFlushIdle, std::memory_order_release);
}

bool RetryableRequestsFlusher::SetReading(RetryableRequestsFlushState* old_state) {
  *old_state = RetryableRequestsFlushState::kFlushIdle;
  return TransferState(old_state, RetryableRequestsFlushState::kReading);
}

bool RetryableRequestsFlusher::SetShutdown() {
  RetryableRequestsFlushState old_state = RetryableRequestsFlushState::kFlushIdle;
  return TransferState(&old_state, RetryableRequestsFlushState::kShutdown);
}

void RetryableRequestsFlusher::SetIdleAndNotifyAll() {
  SetIdle();
  {
    std::unique_lock<std::mutex> lock(flush_mutex_);
  }
  flush_cond_.notify_all();
}

Status RetryableRequestsFlusher::FlushRetryableRequests(RetryableRequestsFlushState expected) {
  // Should do flush exclusively. Also if there's already an active flush,
  // should be fine to skip this one.
  RetryableRequestsFlushState old_state;
  while(!SetFlushing(expected == RetryableRequestsFlushState::kFlushIdle, &old_state)) {
    DCHECK(expected != RetryableRequestsFlushState::kFlushSubmitted);
    if (old_state == RetryableRequestsFlushState::kFlushSubmitted ||
        old_state == RetryableRequestsFlushState::kFlushing) {
      const auto msg = Format("Tablet $0 has a flush task still in progress", tablet_id_);
      VLOG(1) << msg;
      return STATUS(AlreadyPresent, msg);
    }
    if (old_state == RetryableRequestsFlushState::kShutdown) {
      const auto msg = Format("Tablet $0 is shutting down", tablet_id_);
      VLOG(1) << msg;
      return STATUS(ShutdownInProgress, msg);
    }
    SCHECK_NE(old_state,
              RetryableRequestsFlushState::kFlushIdle,
              IllegalState,
              "Should succeed to set state if old_state=kFlushIdle");
    // If there's no ongoing flush but there's read event, wait until it's done and retry.
    WaitForFlushIdleOrShutdown();
  }
  auto se = ScopeExit([this] {
    SetIdleAndNotifyAll();
  });
  TEST_PAUSE_IF_FLAG(TEST_pause_before_flushing_retryable_requests);
  return raft_consensus_->FlushRetryableRequests();
}

Status RetryableRequestsFlusher::SubmitFlushRetryableRequestsTask() {
  if (!flush_retryable_requests_pool_token_) {
    return Status::OK();
  }
  RetryableRequestsFlushState old_state;
  while (!SetSubmitted(&old_state)) {
    if (old_state == RetryableRequestsFlushState::kFlushSubmitted ||
        old_state == RetryableRequestsFlushState::kFlushing) {
      return STATUS_FORMAT(
          AlreadyPresent, "Tablet $0 has a flush task still in progress", tablet_id_);
    }
    if (old_state == RetryableRequestsFlushState::kShutdown) {
      return STATUS_FORMAT(ShutdownInProgress, "Tablet $0 is shutting down", tablet_id_);
    }
    SCHECK_NE(old_state,
              RetryableRequestsFlushState::kFlushIdle,
              IllegalState,
              "Should succeed to set state if old_state=kFlushIdle");
    // If there's no ongoing flush but there's read event, wait until it's done and retry.
    WaitForFlushIdleOrShutdown();
  }
  LOG(INFO) << "Tablet " << tablet_id_ << " is submitting flush retryable requests task...";
  TEST_PAUSE_IF_FLAG(TEST_pause_before_submitting_flush_retryable_requests);
  Status s = flush_retryable_requests_pool_token_->SubmitFunc(
      std::bind(&RetryableRequestsFlusher::FlushRetryableRequests,
                   shared_from_this(),
                   RetryableRequestsFlushState::kFlushSubmitted));
  if (!s.ok()) {
    SetIdleAndNotifyAll();
  }
  return s;
}

// Copy retryable requests file to dest_path and return the last flushed op_id.
Result<OpId> RetryableRequestsFlusher::CopyRetryableRequestsTo(const std::string& dest_path) {
  RetryableRequestsFlushState old_state;
  while (!SetReading(&old_state)) {
    if (old_state == RetryableRequestsFlushState::kShutdown) {
      return STATUS_FORMAT(ShutdownInProgress, "Tablet $0 is shutting down", tablet_id_);
    }
    SCHECK_NE(old_state,
              RetryableRequestsFlushState::kFlushIdle,
              IllegalState,
              "oShould succeed to set state if old_state=kFlushIdle");
    WaitForFlushIdleOrShutdown();
  }
  auto se = ScopeExit([this] {
    SetIdleAndNotifyAll();
  });
  TEST_PAUSE_IF_FLAG(TEST_pause_before_copying_retryable_requests);
  RETURN_NOT_OK(raft_consensus_->CopyRetryableRequestsTo(dest_path));
  return raft_consensus_->GetLastFlushedOpIdInRetryableRequests();
}

void RetryableRequestsFlusher::Shutdown() {
  // Wait ongoing flush to be done.
  while(!SetShutdown()) {
    WaitForFlushIdleOrShutdown();
  }
  {
    std::unique_lock<std::mutex> lock(flush_mutex_);
  }
  flush_cond_.notify_all();
  flush_retryable_requests_pool_token_.reset();
}

void RetryableRequestsFlusher::WaitForFlushIdleOrShutdown() const {
  VLOG(3) << "Start to wait flush done. tablet: " << tablet_id_;
  {
    std::unique_lock<std::mutex> lock(flush_mutex_);
    flush_cond_.wait(lock, [this] {
      auto state = flush_state();
      return state == RetryableRequestsFlushState::kFlushIdle ||
          state == RetryableRequestsFlushState::kShutdown;
    });
  }
  VLOG(3) << "Succeed to wait flush done. tablet: " << tablet_id_;
}

bool RetryableRequestsFlusher::TEST_HasRetryableRequestsOnDisk() {
  auto se = ScopeExit([this] {
    SetIdleAndNotifyAll();
  });
  RetryableRequestsFlushState old_state;
  while (!SetReading(&old_state)) {
    if (old_state == RetryableRequestsFlushState::kShutdown) {
      return false;
    }
    WaitForFlushIdleOrShutdown();
  }
  return raft_consensus_->TEST_HasRetryableRequestsOnDisk();
}

} // namespace tablet
} // namespace yb
