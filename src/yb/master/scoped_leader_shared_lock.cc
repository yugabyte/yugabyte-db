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

#include "yb/master/scoped_leader_shared_lock.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/metadata.pb.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/sys_catalog.h"

#include "yb/tablet/tablet_peer.h"

#include "yb/util/debug-util.h"
#include "yb/util/shared_lock.h"
#include "yb/util/status_format.h"
#include "yb/util/tsan_util.h"
#include "yb/util/flags.h"

using std::string;

using namespace std::literals;

constexpr int32_t kMasterLogLockWarningMsDefault =
    ::yb::RegularBuildVsSanitizers<int32_t>(1000, 3000);

DEFINE_UNKNOWN_int32(master_log_lock_warning_ms, kMasterLogLockWarningMsDefault,
             "Print warnings if the master leader shared lock is held for longer than this amount "
             "of time. Note that this is a shared lock, so these warnings simply indicate "
             "long-running master operations that could delay system catalog loading by a new "
             "master leader.");

constexpr int32_t kMasterLeaderLockStackTraceMsDefault =
    ::yb::RegularBuildVsSanitizers<int32_t>(3000, 9000);

DEFINE_UNKNOWN_int32(master_leader_lock_stack_trace_ms, kMasterLeaderLockStackTraceMsDefault,
             "Dump a stack trace if the master leader shared lock is held for longer than this "
             "of time. Also see master_log_lock_warning_ms.");

using yb::consensus::Consensus;
using yb::consensus::ConsensusStatePB;

using yb::consensus::CONSENSUS_CONFIG_COMMITTED;

namespace yb {
namespace master {

ScopedLeaderSharedLock::ScopedLeaderSharedLock(
    CatalogManager* catalog, const char* file_name, int line_number, const char* function_name)
    : catalog_(DCHECK_NOTNULL(catalog)),
      leader_shared_lock_(catalog->leader_lock_, std::try_to_lock),
      start_(std::chrono::steady_clock::now()),
      epoch_(LeaderEpoch(-1)),
      file_name_(file_name),
      line_number_(line_number),
      function_name_(function_name) {
  bool catalog_loaded;
  {
    // Check if the catalog manager is running.
    std::lock_guard l(catalog_->state_lock_);
    if (PREDICT_FALSE(catalog_->state_ != CatalogManager::kRunning)) {
      catalog_status_ = STATUS_SUBSTITUTE(ServiceUnavailable,
          "Catalog manager is not initialized. State: $0", catalog_->state_);
      return;
    }
    epoch_.leader_term = catalog_->leader_ready_term_;
    catalog_loaded = catalog_->is_catalog_loaded_;
    epoch_.pitr_count = catalog_->sys_catalog_->pitr_count();
  }

  string uuid = catalog_->master_->fs_manager()->uuid();
  if (PREDICT_FALSE(catalog_->master_->IsShellMode())) {
    // Consensus and other internal fields should not be checked when in shell mode as they may be
    // in transition.
    leader_status_ = STATUS_SUBSTITUTE(IllegalState,
        "Catalog manager of $0 is in shell mode, not the leader.", uuid);
    return;
  }

  // Check if the catalog manager is the leader.
  ConsensusStatePB cstate;
  auto consensus_result = catalog_->sys_catalog_->tablet_peer()->GetConsensus();
  if (consensus_result) {
    cstate = consensus_result.get()->ConsensusState(CONSENSUS_CONFIG_COMMITTED);
  }
  if (PREDICT_FALSE(!cstate.has_leader_uuid() || cstate.leader_uuid() != uuid)) {
    leader_status_ = STATUS_FORMAT(IllegalState,
                                   "Not the leader. Local UUID: $0, Consensus state: $1",
                                   uuid, cstate);
    return;
  }
  // TODO: deduplicate the leadership check above and below (one is committed, one is active).
  const Status s = consensus_result.get()->CheckIsActiveLeaderAndHasLease();
  if (!s.ok()) {
    leader_status_ = s;
    return;
  }
  if (PREDICT_FALSE(epoch_.leader_term != cstate.current_term())) {
    // Normally we use LeaderNotReadyToServe to indicate that the leader has not replicated its
    // NO_OP entry or the previous leader's lease has not expired yet, and the handling logic is to
    // to retry on the same server.
    leader_status_ = STATUS_SUBSTITUTE(
        LeaderNotReadyToServe,
        "Leader not yet ready to serve requests: "
        "leader_ready_term_ = $0; cstate.current_term = $1",
        epoch_.leader_term, cstate.current_term());
    return;
  }
  if (PREDICT_FALSE(!leader_shared_lock_.owns_lock())) {
    leader_status_ = STATUS_SUBSTITUTE(
        ServiceUnavailable,
        "Couldn't get leader_lock_ in shared mode. Leader still loading catalog tables."
        "leader_ready_term_ = $0; cstate.current_term = $1",
        epoch_.leader_term, cstate.current_term());
    return;
  }
  if (!catalog_loaded) {
    leader_status_ = STATUS_SUBSTITUTE(ServiceUnavailable, "Catalog manager is not loaded");
    return;
  }
}

ScopedLeaderSharedLock::~ScopedLeaderSharedLock() {
  Unlock();
}

void ScopedLeaderSharedLock::Unlock() {
  if (leader_shared_lock_.owns_lock()) {
    {
      decltype(leader_shared_lock_) lock;
      lock.swap(leader_shared_lock_);
    }
    if (IsSanitizer()) {
      return;
    }

    auto finish = std::chrono::steady_clock::now();
    bool need_stack_trace = finish > start_ + 1ms * FLAGS_master_leader_lock_stack_trace_ms;
    bool need_warning =
        need_stack_trace || (finish > start_ + 1ms * FLAGS_master_log_lock_warning_ms);
    if (need_warning) {
      LOG(WARNING)
          << "RPC took a long time (" << file_name_ << ":" << line_number_ << ", "
          << function_name_ << "): " << AsString(finish - start_)
          << (need_stack_trace ? "\n" + GetStackTrace() : "");
    }
  }
}

int64_t ScopedLeaderSharedLock::GetLeaderReadyTerm() const { return epoch_.leader_term; }

}  // namespace master
}  // namespace yb
