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

#include "yb/consensus/consensus_round.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/tserver_error.h"

#include "yb/util/size_literals.h"
#include "yb/util/trace.h"

namespace yb {
namespace tablet {

using consensus::DriverType;
using tserver::TabletServerError;
using tserver::TabletServerErrorPB;

Operation::Operation(std::unique_ptr<OperationState> state,
                     OperationType operation_type)
    : state_(std::move(state)),
      operation_type_(operation_type) {
}

std::string Operation::LogPrefix() const {
  return Format("T $0 $1: ", state()->tablet()->tablet_id(), this);
}

Status Operation::Replicated(int64_t leader_term) {
  Status complete_status = Status::OK();
  RETURN_NOT_OK(DoReplicated(leader_term, &complete_status));
  auto state = this->state();
  state->Replicated();
  state->Release();
  state->CompleteWithStatus(complete_status);
  return Status::OK();
}

void Operation::Aborted(const Status& status) {
  VLOG_WITH_PREFIX_AND_FUNC(4) << status;
  auto state = this->state();
  state->Aborted();
  state->CompleteWithStatus(DoAborted(status));
}

void OperationState::CompleteWithStatus(const Status& status) const {
  bool expected = false;
  if (!complete_.compare_exchange_strong(expected, true)) {
    LOG_WITH_PREFIX(DFATAL) << __func__ << " called twice, new status: " << status;
    return;
  }
  if (completion_clbk_) {
    completion_clbk_(status);
  }
}

OperationState::OperationState(Tablet* tablet)
    : tablet_(tablet) {
}

void OperationState::set_consensus_round(const consensus::ConsensusRoundPtr& consensus_round) {
  consensus_round_ = consensus_round;
  op_id_ = consensus_round_->id();
  UpdateRequestFromConsensusRound();
}

OperationState::~OperationState() {
}

void OperationState::set_hybrid_time(const HybridTime& hybrid_time) {
  // make sure we set the hybrid_time only once
  std::lock_guard<simple_spinlock> l(mutex_);
  DCHECK(!hybrid_time_.is_valid());
  hybrid_time_ = hybrid_time;
}

std::string OperationState::LogPrefix() const {
  return Format("$0: ", this);
}

HybridTime OperationState::WriteHybridTime() const {
  return hybrid_time();
}

std::string OperationState::ConsensusRoundAsString() const {
  std::lock_guard<simple_spinlock> l(mutex_);
  return AsString(consensus_round());
}

void OperationState::AddedToLeader(const OpId& op_id, const OpId& committed_op_id) {
  HybridTime hybrid_time;
  if (use_mvcc()) {
    hybrid_time = tablet_->mvcc_manager()->AddLeaderPending(op_id);
  } else {
    hybrid_time = tablet_->clock()->Now();
  }

  {
    std::lock_guard<simple_spinlock> l(mutex_);
    hybrid_time_ = hybrid_time;
    op_id_ = op_id;
    auto* replicate_msg = consensus_round_->replicate_msg().get();
    op_id.ToPB(replicate_msg->mutable_id());
    committed_op_id.ToPB(replicate_msg->mutable_committed_op_id());
    replicate_msg->set_hybrid_time(hybrid_time_.ToUint64());
    replicate_msg->set_monotonic_counter(*tablet()->monotonic_counter());
  }

  AddedAsPending();
}

void OperationState::AddedToFollower() {
  if (use_mvcc()) {
    tablet()->mvcc_manager()->AddFollowerPending(hybrid_time(), op_id());
  }

  AddedAsPending();
}

void OperationState::Aborted() {
  if (use_mvcc()) {
    auto hybrid_time = hybrid_time_even_if_unset();
    if (hybrid_time.is_valid()) {
      tablet()->mvcc_manager()->Aborted(hybrid_time, op_id());
    }
  }

  RemovedFromPending();
}

void OperationState::Replicated() {
  if (use_mvcc()) {
    tablet()->mvcc_manager()->Replicated(hybrid_time(), op_id());
  }

  RemovedFromPending();
}

void OperationState::Release() {
}

void ExclusiveSchemaOperationStateBase::ReleasePermitToken() {
  permit_token_.Reset();
  TRACE("Released permit token");
}

}  // namespace tablet
}  // namespace yb
