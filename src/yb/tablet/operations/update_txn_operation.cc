//
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
//

#include "yb/tablet/operations/update_txn_operation.h"

#include "yb/consensus/consensus.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transaction_coordinator.h"

using namespace std::literals;

namespace yb {
namespace tablet {

void UpdateTxnOperationState::UpdateRequestFromConsensusRound() {
  VLOG_WITH_PREFIX(2) << "UpdateRequestFromConsensusRound";
  request_.store(consensus_round()->replicate_msg()->mutable_transaction_state(),
                 std::memory_order_release);
}

std::string UpdateTxnOperationState::ToString() const {
  auto req = request();
  return Format("UpdateTxnOperationState [$0]", !req ? "(none)"s : req->ShortDebugString());
}

consensus::ReplicateMsgPtr UpdateTxnOperation::NewReplicateMsg() {
  auto result = std::make_shared<consensus::ReplicateMsg>();
  result->set_op_type(consensus::UPDATE_TRANSACTION_OP);
  *result->mutable_transaction_state() = *state()->request();
  return result;
}

Status UpdateTxnOperation::Prepare() {
  VLOG_WITH_PREFIX(2) << "Prepare";
  return Status::OK();
}

void UpdateTxnOperation::DoStart() {
  VLOG_WITH_PREFIX(2) << "DoStart";
  state()->TrySetHybridTimeFromClock();
}

TransactionCoordinator& UpdateTxnOperation::transaction_coordinator() const {
  return *state()->tablet()->transaction_coordinator();
}

Status UpdateTxnOperation::Apply(int64_t leader_term) {
  VLOG_WITH_PREFIX(2) << "Apply";
  auto* state = this->state();
  // APPLYING is handled separately, because it is received for transactions not managed by
  // this tablet as a transaction status tablet, but tablets that are involved in the data
  // path (receive write intents) for this transaction.
  if (state->request()->status() == TransactionStatus::APPLYING) {
    TransactionParticipant::ReplicatedData data = {
        leader_term,
        *state->request(),
        state->op_id(),
        state->hybrid_time(),
        false /* already_applied */
    };
    return state->tablet()->transaction_participant()->ProcessReplicated(data);
  } else {
    TransactionCoordinator::ReplicatedData data = {
        leader_term,
        *state->request(),
        state->op_id(),
        state->hybrid_time()
    };
    return transaction_coordinator().ProcessReplicated(data);
  }
}

string UpdateTxnOperation::ToString() const {
  return Format("UpdateTxnOperation { state: $0 }", *state());
}

void UpdateTxnOperation::Finish(OperationResult result) {
  if (result == OperationResult::ABORTED &&
      state()->tablet()->transaction_coordinator()) {
    LOG_WITH_PREFIX(INFO) << "Aborted";
    TransactionCoordinator::AbortedData data = {
      *state()->request(),
      state()->op_id(),
    };
    transaction_coordinator().ProcessAborted(data);
  }
}

} // namespace tablet
} // namespace yb
