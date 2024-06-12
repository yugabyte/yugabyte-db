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

#include "yb/tablet/tablet_bootstrap_state_manager.h"

namespace yb {
namespace tablet {

// State change:
// submit flush task: IDLE -> SUBMITTED
// submit task failed: SUBMITTED -> IDLE
// do flush work in thread pool: SUMITTED -> FLUSHING
// do flush work synchronously: IDLE -> FLUSHING
// flush is done: FLUSHING -> IDLE
// read bootstrap state: IDLE -> READING
// read is done: READING -> IDLE
// shutdown: IDLE -> SHUTDOWN
YB_DEFINE_ENUM(TabletBootstrapFlushState,
               (kFlushIdle)(kFlushSubmitted)(kFlushing)(kReading)(kShutdown));

class TabletBootstrapStateFlusher :
    public std::enable_shared_from_this<TabletBootstrapStateFlusher> {
 public:
  TabletBootstrapStateFlusher(
      const std::string& tablet_id,
      std::shared_ptr<consensus::RaftConsensus> raft_consensus,
      std::shared_ptr<TabletBootstrapStateManager> bootstrap_state_manager,
      std::unique_ptr<ThreadPoolToken> flush_bootstrap_state_pool_token)
      : tablet_id_(tablet_id),
        raft_consensus_(raft_consensus),
        bootstrap_state_manager_(bootstrap_state_manager),
        flush_bootstrap_state_pool_token_(std::move(flush_bootstrap_state_pool_token)) {}

  Status FlushBootstrapState(
      TabletBootstrapFlushState expected = TabletBootstrapFlushState::kFlushIdle);
  Status SubmitFlushBootstrapStateTask();
  Result<OpId> CopyBootstrapStateTo(const std::string& dest_path);
  OpId GetMaxReplicatedOpId();

  void Shutdown();

  bool TEST_HasBootstrapStateOnDisk();

  TabletBootstrapFlushState flush_state() const {
    return flush_state_.load(std::memory_order_acquire);
  }

 private:
  bool TransferState(TabletBootstrapFlushState* old_state, TabletBootstrapFlushState new_state);
  bool SetFlushing(bool expect_idle, TabletBootstrapFlushState* old_state);
  bool SetSubmitted(TabletBootstrapFlushState* old_state);
  void SetIdle();
  bool SetReading(TabletBootstrapFlushState* old_state);
  bool SetShutdown();
  void WaitForFlushIdleOrShutdown() const;
  void SetIdleAndNotifyAll();

  // Used to notify waiters when each flush is done.
  mutable std::mutex flush_mutex_;
  mutable std::condition_variable flush_cond_;
  std::atomic<TabletBootstrapFlushState> flush_state_{TabletBootstrapFlushState::kFlushIdle};
  TabletId tablet_id_;
  std::shared_ptr<consensus::RaftConsensus> raft_consensus_;
  std::shared_ptr<TabletBootstrapStateManager> bootstrap_state_manager_;
  std::unique_ptr<ThreadPoolToken> flush_bootstrap_state_pool_token_;
};

} // namespace tablet
} // namespace yb
