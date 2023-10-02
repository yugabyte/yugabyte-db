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

#include "yb/tablet/operations/operation.h"

#include "yb/consensus/consensus.messages.h"
#include "yb/consensus/consensus_round.h"

#include "yb/tablet/tablet.h"

#include "yb/tserver/tserver_error.h"

#include "yb/util/async_util.h"
#include "yb/util/logging.h"
#include "yb/util/size_literals.h"
#include "yb/util/trace.h"

namespace yb {
namespace tablet {

using tserver::TabletServerError;

Operation::Operation(OperationType operation_type, TabletPtr tablet)
    : operation_type_(operation_type) {
  if (tablet) {
    SetTablet(tablet);
  }
}

Operation::~Operation() {}

std::string Operation::LogPrefix() const {
  if (!log_prefix_initialized_.load(std::memory_order_acquire)) {
    std::lock_guard lock(log_prefix_mutex_);
    auto tablet = tablet_safe();
    log_prefix_ = Format(
        "T $0 $1: ",
        tablet.ok() ? (*tablet)->tablet_id()
                    : Format("(error getting tablet: $0)", tablet.status()),
        this);
    log_prefix_initialized_.store(true, std::memory_order_release);
  }
  return GetLogPrefixUnsafe();
}

std::string Operation::ToString() const {
  return Format("{ type: $0 consensus_round: $1 ht: $2 }",
                operation_type(), consensus_round(), hybrid_time_even_if_unset());
}

Status Operation::Replicated(int64_t leader_term, WasPending was_pending) {
  Status complete_status = Status::OK();
  RETURN_NOT_OK(DoReplicated(leader_term, &complete_status));
  Replicated(was_pending);
  Release();
  CompleteWithStatus(complete_status);
  return Status::OK();
}

void Operation::Aborted(const Status& status, bool was_pending) {
  VLOG_WITH_PREFIX_AND_FUNC(4) << status;
  Aborted(was_pending);
  Release();
  CompleteWithStatus(DoAborted(status));
}

void Operation::CompleteWithStatus(const Status& status) const {
  bool expected = false;
  if (!complete_.compare_exchange_strong(expected, true)) {
    LOG_WITH_PREFIX(DFATAL) << __func__ << " called twice, new status: " << status;
    return;
  }
  if (completion_clbk_) {
    completion_clbk_(status);
  }
}

void Operation::set_consensus_round(const consensus::ConsensusRoundPtr& consensus_round) {
  {
    std::lock_guard l(mutex_);
    // We are not using set_op_id here so we can acquire the mutex only once.
    consensus_round_ = consensus_round;
    consensus_round_atomic_.store(consensus_round.get(), std::memory_order_release);
    op_id_.store(consensus_round_->id(), std::memory_order_release);
  }
  UpdateRequestFromConsensusRound();
}

void Operation::set_hybrid_time(const HybridTime& hybrid_time) {
  auto existing_hybrid_time = hybrid_time_.exchange(hybrid_time, std::memory_order_acq_rel);
  // Make sure we set hybrid time only once.
  DCHECK_EQ(existing_hybrid_time, HybridTime::kInvalid);
}

HybridTime Operation::WriteHybridTime() const {
  return hybrid_time();
}

Status Operation::AddedToLeader(const OpId& op_id, const OpId& committed_op_id) {
  HybridTime hybrid_time;
  auto tablet = VERIFY_RESULT(tablet_safe());
  if (use_mvcc()) {
    hybrid_time = tablet->mvcc_manager()->AddLeaderPending(op_id);
  } else {
    hybrid_time = tablet->clock()->Now();
  }

  {
    std::lock_guard l(mutex_);
    op_id_ = op_id;
    auto* replicate_msg = consensus_round_->replicate_msg().get();
    op_id.ToPB(replicate_msg->mutable_id());
    committed_op_id.ToPB(replicate_msg->mutable_committed_op_id());
    replicate_msg->set_hybrid_time(hybrid_time.ToUint64());
    replicate_msg->set_monotonic_counter(*tablet->monotonic_counter());
    // "Publish" the hybrid time to the atomic immediately before the lock is released.
    hybrid_time_.store(hybrid_time, std::memory_order_release);
  }

  AddedAsPending(tablet);
  return Status::OK();
}

Status Operation::AddedToFollower() {
  auto tablet = VERIFY_RESULT(tablet_safe());
  if (use_mvcc()) {
    tablet->mvcc_manager()->AddFollowerPending(hybrid_time(), op_id());
  }

  AddedAsPending(tablet);
  return Status::OK();
}

void Operation::Aborted(bool was_pending) {
  TabletPtr tablet;
  if (use_mvcc()) {
    auto hybrid_time = hybrid_time_even_if_unset();
    if (hybrid_time.is_valid()) {
      if (!GetTabletOrLogError(&tablet, __FUNCTION__)) {
        return;
      }
      tablet->mvcc_manager()->Aborted(hybrid_time, op_id());
    }
  }

  if (was_pending) {
    if (!GetTabletOrLogError(&tablet, __FUNCTION__)) {
      return;
    }
    RemovedFromPending(tablet);
  }
}

void Operation::Replicated(WasPending was_pending) {
  TabletPtr tablet;
  if (use_mvcc()) {
    if (!GetTabletOrLogError(&tablet, __FUNCTION__)) {
      return;
    }
    tablet->mvcc_manager()->Replicated(hybrid_time(), op_id());
  }
  if (was_pending) {
    if (!GetTabletOrLogError(&tablet, __FUNCTION__)) {
      return;
    }
    RemovedFromPending(tablet);
  }
}

Result<TabletPtr> Operation::tablet_safe() const {
  auto tablet = tablet_.lock();
  if (!tablet) {
    return STATUS_FORMAT(
        IllegalState,
        "Tablet object referenced by operation $0 has already been deallocated",
        this);
  }
  return tablet;
}

void Operation::Release() {
}

bool Operation::GetTabletOrLogError(TabletPtr* tablet, const char* state_str) {
  if (*tablet) {
    return true;
  }
  auto tablet_result = tablet_safe();
  if (!tablet_result.ok()) {
    // TODO(operation_errors): is this sufficient?
    LOG(DFATAL) << "Failed to mark operation " << ToString() << " as " << state_str << ": "
                << tablet_result.status();
    return false;
  }
  *tablet = std::move(*tablet_result);
  return true;
}

void ExclusiveSchemaOperationBase::ReleasePermitToken() {
  permit_token_.Reset();
  TRACE("Released permit token");
}

OperationCompletionCallback MakeWeakSynchronizerOperationCompletionCallback(
    std::weak_ptr<Synchronizer> synchronizer) {
  return [synchronizer = std::move(synchronizer)](const Status& status) {
    auto shared_synchronizer = synchronizer.lock();
    if (shared_synchronizer) {
      shared_synchronizer->StatusCB(status);
    }
  };
}

consensus::LWReplicateMsg* CreateReplicateMsg(ThreadSafeArena* arena, OperationType op_type) {
  auto result = arena->NewObject<consensus::LWReplicateMsg>(arena);
  result->set_op_type(static_cast<consensus::OperationType>(op_type));
  return result;
}

}  // namespace tablet
}  // namespace yb
