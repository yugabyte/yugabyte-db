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

#include "yb/tablet/transactions/update_txn_transaction.h"

#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transaction_coordinator.h"

using namespace std::literals;

namespace yb {
namespace tablet {

void UpdateTxnTransactionState::UpdateRequestFromConsensusRound() {
  request_ = consensus_round()->replicate_msg()->mutable_transaction_state();
}

std::string UpdateTxnTransactionState::ToString() const {
  return Format("UpdateTxnTransactionState [$0]",
                request_ ? "(none)"s : request_->ShortDebugString());
}

consensus::ReplicateMsgPtr UpdateTxnTransaction::NewReplicateMsg() {
  auto result = std::make_shared<consensus::ReplicateMsg>();
  result->set_op_type(consensus::UPDATE_TRANSACTION_OP);
  *result->mutable_transaction_state() = *state()->request();
  return result;
}

Status UpdateTxnTransaction::Prepare() {
  return Status::OK();
}

void UpdateTxnTransaction::Start() {
  if (!state()->has_hybrid_time()) {
    state()->set_hybrid_time(state()->tablet_peer()->clock().Now());
  }
}

TransactionCoordinator& UpdateTxnTransaction::transaction_coordinator() const {
  return *state()->tablet_peer()->tablet()->transaction_coordinator();
}

ProcessingMode UpdateTxnTransaction::mode() const {
  return type() == consensus::LEADER ? ProcessingMode::LEADER : ProcessingMode::NON_LEADER;
}

Status UpdateTxnTransaction::Apply(gscoped_ptr<consensus::CommitMsg>* commit_msg) {
  auto* state = this->state();
  TransactionCoordinator::ReplicatedData data = {
      mode(),
      state->tablet_peer()->tablet(),
      *state->request(),
      state->op_id(),
      state->hybrid_time()
  };
  return transaction_coordinator().ProcessReplicated(data);
}

string UpdateTxnTransaction::ToString() const {
  return Format("UpdateTxnTransaction [state=$0]", state()->ToString());
}

} // namespace tablet
} // namespace yb
