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
#include "yb/yql/pggate/pggate.h"
#include "yb/util/status.h"
#include "yb/client/transaction.h"
#include "yb/common/common.pb.h"

namespace yb {
namespace pggate {

using client::YBTransaction;
using client::YBClientPtr;
using client::AsyncClientInitialiser;
using client::TransactionManager;
using client::YBTransactionPtr;
using client::YBSession;
using client::YBSessionPtr;
using client::LocalTabletFilter;

PgTxnManager::PgTxnManager(
    AsyncClientInitialiser* async_client_init,
    scoped_refptr<ClockBase> clock)
    : async_client_init_(async_client_init),
      clock_(std::move(clock)) {
}

PgTxnManager::~PgTxnManager() {
  // Abort the transaction before the transaction manager gets destroyed.
  if (txn_) {
    txn_->Abort();
  }
  ResetTxnAndSession();
}

Status PgTxnManager::BeginTransaction() {
  VLOG(2) << "BeginTransaction: txn_in_progress_=" << txn_in_progress_;
  if (txn_in_progress_) {
    return STATUS(IllegalState, "Transaction is already in progress");
  }
  ResetTxnAndSession();
  txn_in_progress_ = true;
  StartNewSession();
  return Status::OK();
}

void PgTxnManager::StartNewSession() {
  session_ = std::make_shared<YBSession>(async_client_init_->client(), clock_);
  session_->SetReadPoint(client::Restart::kFalse);
  session_->SetForceConsistentRead(true);
}

Status PgTxnManager::BeginWriteTransactionIfNecessary() {
  VLOG(2) << "BeginWriteTransactionIfNecessary: txn_in_progress_="
          << txn_in_progress_;
  if (txn_) {
    return Status::OK();
  }
  txn_ = std::make_shared<YBTransaction>(GetOrCreateTransactionManager());
  RETURN_NOT_OK(txn_->Init(IsolationLevel::SNAPSHOT_ISOLATION));
  if (!session_) {
    StartNewSession();
  }
  session_->SetTransaction(txn_);
  return Status::OK();
}

Status PgTxnManager::CommitTransaction() {
  if (!txn_in_progress_) {
    return Status::OK();
  }
  if (!txn_) {
    // This was a read-only transaction, nothing to commit.
    ResetTxnAndSession();
    return Status::OK();
  }
  Status status = txn_->CommitFuture().get();
  ResetTxnAndSession();
  return status;
}

Status PgTxnManager::AbortTransaction() {
  if (!txn_in_progress_) {
    return Status::OK();
  }
  if (!txn_) {
    // This was a read-only transaction, nothing to commit.
    ResetTxnAndSession();
    return Status::OK();
  }
  // TODO: how do we report errors if the transaction has already committed?
  txn_->Abort();
  ResetTxnAndSession();
  return Status::OK();
}

// TODO: dedup with similar logic in CQLServiceImpl.
// TODO: do we need lazy initialization of the txn manager?
TransactionManager* PgTxnManager::GetOrCreateTransactionManager() {
  auto result = transaction_manager_.load(std::memory_order_acquire);
  if (result) {
    return result;
  }
  std::lock_guard<decltype(transaction_manager_mutex_)> lock(transaction_manager_mutex_);
  if (transaction_manager_holder_) {
    return transaction_manager_holder_.get();
  }

  transaction_manager_holder_ = std::make_unique<client::TransactionManager>(
      async_client_init_->client(), clock_, LocalTabletFilter());

  transaction_manager_.store(transaction_manager_holder_.get(), std::memory_order_release);
  return transaction_manager_holder_.get();
}

Result<client::YBSession*> PgTxnManager::GetTransactionalSession() {
  if (!txn_in_progress_) {
    RETURN_NOT_OK(BeginTransaction());
  }
  return session_.get();
}

void PgTxnManager::ResetTxnAndSession() {
  txn_in_progress_ = false;
  session_ = nullptr;
  txn_ = nullptr;
}

}  // namespace pggate
}  // namespace yb
