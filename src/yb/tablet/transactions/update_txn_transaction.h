//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_TABLET_TRANSACTIONS_UPDATE_TXN_TRANSACTION_H
#define YB_TABLET_TRANSACTIONS_UPDATE_TXN_TRANSACTION_H

#include "yb/tablet/transaction_coordinator.h"

#include "yb/tablet/transactions/transaction.h"

namespace yb {
namespace tablet {

class TransactionCoordinator;

class UpdateTxnTransactionState : public TransactionState {
 public:
  UpdateTxnTransactionState(TabletPeer* tablet_peer, const tserver::TransactionStatePB* request)
      : TransactionState(tablet_peer), request_(request) {}

  explicit UpdateTxnTransactionState(TabletPeer* tablet_peer)
      : UpdateTxnTransactionState(tablet_peer, nullptr) {}

  const tserver::TransactionStatePB* request() const override { return request_; }

  void TakeRequest(tserver::TransactionStatePB* request) {
    request_holder_.reset(new tserver::TransactionStatePB);
    request_ = request_holder_.get();
    request_holder_->Swap(request);
  }

  std::string ToString() const override;

 private:
  void UpdateRequestFromConsensusRound() override;

  std::unique_ptr<tserver::TransactionStatePB> request_holder_;
  const tserver::TransactionStatePB* request_;
};

class UpdateTxnTransaction : public Transaction {
 public:
  UpdateTxnTransaction(std::unique_ptr<UpdateTxnTransactionState> state, consensus::DriverType type)
      : Transaction(std::move(state), type, Transaction::UPDATE_TRANSACTION_TXN) {}

  UpdateTxnTransactionState* state() override {
    return down_cast<UpdateTxnTransactionState*>(Transaction::state());
  }

  const UpdateTxnTransactionState* state() const override {
    return down_cast<const UpdateTxnTransactionState*>(Transaction::state());
  }

 private:
  TransactionCoordinator& transaction_coordinator() const;
  ProcessingMode mode() const;

  consensus::ReplicateMsgPtr NewReplicateMsg() override;
  CHECKED_STATUS Prepare() override;
  void Start() override;
  CHECKED_STATUS Apply(gscoped_ptr<consensus::CommitMsg>* commit_msg) override;
  std::string ToString() const override;
};

} // namespace tablet
} // namespace yb

#endif // YB_TABLET_TRANSACTIONS_UPDATE_TXN_TRANSACTION_H
