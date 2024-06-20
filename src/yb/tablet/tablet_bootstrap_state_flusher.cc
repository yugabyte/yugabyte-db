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

#include "yb/tablet/tablet_bootstrap_state_flusher.h"
#include "yb/consensus/raft_consensus.h"

#include "yb/util/callsite_profiling.h"
#include "yb/util/debug-util.h"
#include "yb/util/scope_exit.h"

DEFINE_RUNTIME_AUTO_bool(enable_flush_retryable_requests, kLocalPersisted, false, true,
    "If enabled, will flush bootstrap state structure to the disk when roll the log segment, "
    "which helps speedup bootstrap process");

DEFINE_test_flag(bool, pause_before_flushing_bootstrap_state, false,
    "Test only: whether to pause before actually doing FlushBootstrapState");

DEFINE_test_flag(bool, pause_before_submitting_flush_bootstrap_state, false,
    "Test only: whether to pause before actually doing SubmitFlushBootstrapStateTask");

DEFINE_test_flag(bool, pause_before_copying_bootstrap_state, false,
    "Test only: whether to pause before actually doing CopyBootstrapStateTo");

using namespace std::literals;

namespace yb {
namespace tablet {

// Transfer flush_state_ from old_state to new_state.
// If it failed, old_state will be updated to the current value of flush_state_.
bool TabletBootstrapStateFlusher::TransferState(
    TabletBootstrapFlushState* old_state, TabletBootstrapFlushState new_state) {
  return flush_state_.compare_exchange_strong(
      *old_state, new_state, std::memory_order_acq_rel);
}

bool TabletBootstrapStateFlusher::SetFlushing(
    bool expect_idle, TabletBootstrapFlushState* old_state) {
  *old_state = expect_idle ?
      TabletBootstrapFlushState::kFlushIdle : TabletBootstrapFlushState::kFlushSubmitted;
  return TransferState(old_state, TabletBootstrapFlushState::kFlushing);
}

bool TabletBootstrapStateFlusher::SetSubmitted(TabletBootstrapFlushState* old_state) {
  *old_state = TabletBootstrapFlushState::kFlushIdle;
  return TransferState(old_state, TabletBootstrapFlushState::kFlushSubmitted);
}

void TabletBootstrapStateFlusher::SetIdle() {
  flush_state_.store(TabletBootstrapFlushState::kFlushIdle, std::memory_order_release);
}

bool TabletBootstrapStateFlusher::SetReading(TabletBootstrapFlushState* old_state) {
  *old_state = TabletBootstrapFlushState::kFlushIdle;
  return TransferState(old_state, TabletBootstrapFlushState::kReading);
}

bool TabletBootstrapStateFlusher::SetShutdown() {
  TabletBootstrapFlushState old_state = TabletBootstrapFlushState::kFlushIdle;
  return TransferState(&old_state, TabletBootstrapFlushState::kShutdown);
}

void TabletBootstrapStateFlusher::SetIdleAndNotifyAll() {
  SetIdle();
  {
    std::unique_lock<std::mutex> lock(flush_mutex_);
  }
  YB_PROFILE(flush_cond_.notify_all());
}

Status TabletBootstrapStateFlusher::FlushBootstrapState(TabletBootstrapFlushState expected) {
  // Should do flush exclusively. Also if there's already an active flush,
  // should be fine to skip this one.
  TabletBootstrapFlushState old_state;
  while(!SetFlushing(expected == TabletBootstrapFlushState::kFlushIdle, &old_state)) {
    DCHECK(expected != TabletBootstrapFlushState::kFlushSubmitted);
    if (old_state == TabletBootstrapFlushState::kFlushSubmitted ||
        old_state == TabletBootstrapFlushState::kFlushing) {
      const auto msg = Format("Tablet $0 has a flush task still in progress", tablet_id_);
      VLOG(1) << msg;
      return STATUS(AlreadyPresent, msg);
    }
    if (old_state == TabletBootstrapFlushState::kShutdown) {
      const auto msg = Format("Tablet $0 is shutting down", tablet_id_);
      VLOG(1) << msg;
      return STATUS(ShutdownInProgress, msg);
    }
    SCHECK_NE(old_state,
              TabletBootstrapFlushState::kFlushIdle,
              IllegalState,
              "Should succeed to set state if old_state=kFlushIdle");
    // If there's no ongoing flush but there's read event, wait until it's done and retry.
    WaitForFlushIdleOrShutdown();
  }
  auto se = ScopeExit([this] {
    SetIdleAndNotifyAll();
  });
  TEST_PAUSE_IF_FLAG(TEST_pause_before_flushing_bootstrap_state);
  return bootstrap_state_manager_->SaveToDisk(*raft_consensus_);
}

Status TabletBootstrapStateFlusher::SubmitFlushBootstrapStateTask() {
  if (!flush_bootstrap_state_pool_token_) {
    return Status::OK();
  }
  TabletBootstrapFlushState old_state;
  while (!SetSubmitted(&old_state)) {
    if (old_state == TabletBootstrapFlushState::kFlushSubmitted ||
        old_state == TabletBootstrapFlushState::kFlushing) {
      return STATUS_FORMAT(
          AlreadyPresent, "Tablet $0 has a flush task still in progress", tablet_id_);
    }
    if (old_state == TabletBootstrapFlushState::kShutdown) {
      return STATUS_FORMAT(ShutdownInProgress, "Tablet $0 is shutting down", tablet_id_);
    }
    SCHECK_NE(old_state,
              TabletBootstrapFlushState::kFlushIdle,
              IllegalState,
              "Should succeed to set state if old_state=kFlushIdle");
    // If there's no ongoing flush but there's read event, wait until it's done and retry.
    WaitForFlushIdleOrShutdown();
  }
  LOG(INFO) << "Tablet " << tablet_id_ << " is submitting flush bootstrap state task...";
  TEST_PAUSE_IF_FLAG(TEST_pause_before_submitting_flush_bootstrap_state);
  Status s = flush_bootstrap_state_pool_token_->SubmitFunc(
      std::bind(&TabletBootstrapStateFlusher::FlushBootstrapState,
                   shared_from_this(),
                   TabletBootstrapFlushState::kFlushSubmitted));
  if (!s.ok()) {
    SetIdleAndNotifyAll();
  }
  return s;
}

// Copy bootstrap state file to dest_path and return the last flushed op_id.
Result<OpId> TabletBootstrapStateFlusher::CopyBootstrapStateTo(const std::string& dest_path) {
  TabletBootstrapFlushState old_state;
  while (!SetReading(&old_state)) {
    if (old_state == TabletBootstrapFlushState::kShutdown) {
      return STATUS_FORMAT(ShutdownInProgress, "Tablet $0 is shutting down", tablet_id_);
    }
    SCHECK_NE(old_state,
              TabletBootstrapFlushState::kFlushIdle,
              IllegalState,
              "oShould succeed to set state if old_state=kFlushIdle");
    WaitForFlushIdleOrShutdown();
  }
  auto se = ScopeExit([this] {
    SetIdleAndNotifyAll();
  });
  TEST_PAUSE_IF_FLAG(TEST_pause_before_copying_bootstrap_state);
  RETURN_NOT_OK(bootstrap_state_manager_->CopyTo(dest_path));
  return raft_consensus_->GetLastFlushedOpIdInRetryableRequests();
}

void TabletBootstrapStateFlusher::Shutdown() {
  // Wait ongoing flush to be done.
  while(!SetShutdown()) {
    WaitForFlushIdleOrShutdown();
  }
  {
    std::unique_lock<std::mutex> lock(flush_mutex_);
  }
  YB_PROFILE(flush_cond_.notify_all());
  flush_bootstrap_state_pool_token_.reset();
}

void TabletBootstrapStateFlusher::WaitForFlushIdleOrShutdown() const {
  VLOG(3) << "Start to wait flush done. tablet: " << tablet_id_;
  {
    std::unique_lock<std::mutex> lock(flush_mutex_);
    flush_cond_.wait(lock, [this] {
      auto state = flush_state();
      return state == TabletBootstrapFlushState::kFlushIdle ||
          state == TabletBootstrapFlushState::kShutdown;
    });
  }
  VLOG(3) << "Succeed to wait flush done. tablet: " << tablet_id_;
}

bool TabletBootstrapStateFlusher::TEST_HasBootstrapStateOnDisk() {
  auto se = ScopeExit([this] {
    SetIdleAndNotifyAll();
  });
  TabletBootstrapFlushState old_state;
  while (!SetReading(&old_state)) {
    if (old_state == TabletBootstrapFlushState::kShutdown) {
      return false;
    }
    WaitForFlushIdleOrShutdown();
  }
  return bootstrap_state_manager_->has_file_on_disk();
}

} // namespace tablet
} // namespace yb
