//
// Copyright (c) YugaByte, Inc.
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
    state()->set_hybrid_time(state()->tablet_peer()->clock()->Now());
  }
}

Status UpdateTxnTransaction::Apply(gscoped_ptr<consensus::CommitMsg>* commit_msg) {
  auto* tablet = state()->tablet_peer()->tablet();
  auto mode = type() == consensus::LEADER ? ProcessMode::LEADER : ProcessMode::NON_LEADER;
  return tablet->transaction_coordinator().Process(mode,
                                                   *state()->request(),
                                                   state()->op_id(),
                                                   state()->hybrid_time());
}

string UpdateTxnTransaction::ToString() const {
  return Format("UpdateTxnTransaction [state=$0]", state()->ToString());
}

} // namespace tablet
} // namespace yb
