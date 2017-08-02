//
// Copyright (c) YugaByte, Inc.
//

#include "yb/tablet/transactions/update_txn_transaction.h"

#include "yb/tablet/tablet_peer.h"

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
  auto* request = state()->request();
  if (request->status() == tserver::COMMITTED) {
    auto* tablet = state()->tablet_peer()->tablet();
    return tablet->ApplyIntents(request->transaction_id(),
                                state()->op_id(),
                                state()->hybrid_time());
  }
  return Status::OK();
}

string UpdateTxnTransaction::ToString() const {
  return Format("UpdateTxnTransaction [state=$0]", state()->ToString());
}

} // namespace tablet
} // namespace yb
