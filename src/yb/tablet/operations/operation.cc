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
#include "yb/consensus/consensus.pb.h"

#include "yb/tablet/tablet.h"

#include "yb/tserver/tserver_error.h"

#include "yb/util/async_util.h"
#include "yb/util/logging.h"
#include "yb/util/size_literals.h"
#include "yb/util/trace.h"

namespace yb {
namespace tablet {

using tserver::TabletServerError;
using tserver::TabletServerErrorPB;

Operation::Operation(OperationType operation_type, Tablet* tablet)
    : operation_type_(operation_type), tablet_(tablet) {
}

Operation::~Operation() {}

std::string Operation::LogPrefix() const {
  return Format("T $0 $1: ", tablet()->tablet_id(), this);
}

std::string Operation::ToString() const {
  return Format("{ type: $0 consensus_round: $1 }", operation_type(), consensus_round());
}


Status Operation::Replicated(int64_t leader_term) {
  Status complete_status = Status::OK();
  RETURN_NOT_OK(DoReplicated(leader_term, &complete_status));
  Replicated();
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
  consensus_round_ = consensus_round;
  set_op_id(consensus_round_->id());
  UpdateRequestFromConsensusRound();
}

void Operation::set_hybrid_time(const HybridTime& hybrid_time) {
  // make sure we set the hybrid_time only once
  std::lock_guard<simple_spinlock> l(mutex_);
  DCHECK(!hybrid_time_.is_valid());
  hybrid_time_ = hybrid_time;
}

HybridTime Operation::WriteHybridTime() const {
  return hybrid_time();
}

void Operation::AddedToLeader(const OpId& op_id, const OpId& committed_op_id) {
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

void Operation::AddedToFollower() {
  if (use_mvcc()) {
    tablet()->mvcc_manager()->AddFollowerPending(hybrid_time(), op_id());
  }

  AddedAsPending();
}

void Operation::Aborted(bool was_pending) {
  if (use_mvcc()) {
    auto hybrid_time = hybrid_time_even_if_unset();
    if (hybrid_time.is_valid()) {
      tablet()->mvcc_manager()->Aborted(hybrid_time, op_id());
    }
  }

  if (was_pending) {
    RemovedFromPending();
  }
}

void Operation::Replicated() {
  if (use_mvcc()) {
    tablet()->mvcc_manager()->Replicated(hybrid_time(), op_id());
  }

  RemovedFromPending();
}

void Operation::Release() {
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

consensus::ReplicateMsgPtr CreateReplicateMsg(OperationType op_type) {
  auto result = std::make_shared<consensus::ReplicateMsg>();
  result->set_op_type(static_cast<consensus::OperationType>(op_type));
  return result;
}

}  // namespace tablet
}  // namespace yb
