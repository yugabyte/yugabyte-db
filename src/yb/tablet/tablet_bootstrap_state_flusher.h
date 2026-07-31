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

#include <boost/preprocessor.hpp>
#include <boost/preprocessor/arithmetic/dec.hpp>
#include <boost/preprocessor/control/expr_iif.hpp>
#include <boost/preprocessor/control/iif.hpp>
#include <boost/preprocessor/logical/bool.hpp>
#include <boost/preprocessor/punctuation/is_begin_parens.hpp>
#include <boost/preprocessor/repetition/for.hpp>
#include <boost/preprocessor/seq/elem.hpp>
#include <boost/preprocessor/seq/enum.hpp>
#include <boost/preprocessor/seq/fold_left.hpp>
#include <boost/preprocessor/seq/size.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/variadic/elem.hpp>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "yb/util/threadpool.h"
#include "yb/common/entity_ids_types.h"
#include "yb/common/opid.h"
#include "yb/tablet/tablet_fwd.h"
#include "yb/util/enums.h"
#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb {
namespace consensus {
class RaftConsensus;
}  // namespace consensus

namespace tablet {
class TabletBootstrapStateManager;

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
      TabletWeakPtr tablet,
      std::shared_ptr<consensus::RaftConsensus> raft_consensus,
      std::shared_ptr<TabletBootstrapStateManager> bootstrap_state_manager,
      std::unique_ptr<ThreadPoolToken> flush_bootstrap_state_pool_token)
      : tablet_id_(tablet_id),
        tablet_(std::move(tablet)),
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
  TabletWeakPtr tablet_;
  std::shared_ptr<consensus::RaftConsensus> raft_consensus_;
  std::shared_ptr<TabletBootstrapStateManager> bootstrap_state_manager_;
  std::unique_ptr<ThreadPoolToken> flush_bootstrap_state_pool_token_;
};

} // namespace tablet
} // namespace yb
