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

#include "yb/client/session.h"
#include "yb/client/transaction.h"

#include "yb/common/common.pb.h"

#include "yb/tserver/tserver_shared_mem.h"
#include "yb/tserver/tserver_service.proxy.h"

using namespace std::literals;
using namespace std::placeholders;

namespace yb {
namespace pggate {

using client::YBTransaction;
using client::AsyncClientInitialiser;
using client::TransactionManager;
using client::YBTransactionPtr;
using client::YBSession;
using client::YBSessionPtr;
using client::LocalTabletFilter;

PgTxnManager::PgTxnManager(
    AsyncClientInitialiser* async_client_init,
    scoped_refptr<ClockBase> clock,
    const tserver::TServerSharedObject* tserver_shared_object)
    : async_client_init_(async_client_init),
      clock_(std::move(clock)),
      tserver_shared_object_(tserver_shared_object) {
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

Status PgTxnManager::SetIsolationLevel(int level) {
  isolation_level_ = static_cast<PgIsolationLevel>(level);
  return Status::OK();
}

Status PgTxnManager::SetReadOnly(bool read_only) {
  read_only_ = read_only;
  return Status::OK();
}

Status PgTxnManager::SetDeferrable(bool deferrable) {
  deferrable_ = deferrable;
  return Status::OK();
}

void PgTxnManager::StartNewSession() {
  session_ = std::make_shared<YBSession>(async_client_init_->client(), clock_);
  session_->SetReadPoint(client::Restart::kFalse);
  session_->SetForceConsistentRead(client::ForceConsistentRead::kTrue);
}

Status PgTxnManager::BeginWriteTransactionIfNecessary(bool read_only_op) {
  VLOG(2) << "BeginWriteTransactionIfNecessary: txn_in_progress_="
          << txn_in_progress_;

  // Using Postgres isolation_level_, read_only_, and deferrable_, determine the internal isolation
  // level and defer effect.
  IsolationLevel isolation = (isolation_level_ == PgIsolationLevel::SERIALIZABLE) && !read_only_
      ? IsolationLevel::SERIALIZABLE_ISOLATION : IsolationLevel::SNAPSHOT_ISOLATION;
  bool defer = read_only_ && deferrable_;

  if (txn_) {
    // Sanity check: query layer should ensure that this does not happen.
    if (txn_->isolation() != isolation) {
      return STATUS(IllegalState, "Changing txn isolation level in the middle of a transaction");
    }
  } else if (read_only_op && isolation == IsolationLevel::SNAPSHOT_ISOLATION) {
    if (defer) {
      // This call is idempotent, meaning it has no affect after the first call.
      session_->DeferReadPoint();
    }
  } else {
    if (tserver_shared_object_) {
      if (!tablet_server_proxy_) {
        LOG(INFO) << "Using TServer endpoint: " << (**tserver_shared_object_).endpoint();
        tablet_server_proxy_ = std::make_unique<tserver::TabletServerServiceProxy>(
          &async_client_init_->client()->proxy_cache(),
          HostPort((**tserver_shared_object_).endpoint()));
      }
      tserver::TakeTransactionRequestPB req;
      tserver::TakeTransactionResponsePB resp;
      rpc::RpcController controller;
      // TODO(dtxn) propagate timeout from higher level
      controller.set_timeout(10s);
      RETURN_NOT_OK(tablet_server_proxy_->TakeTransaction(req, &resp, &controller));
      txn_ = YBTransaction::Take(
          GetOrCreateTransactionManager(),
          VERIFY_RESULT(TransactionMetadata::FromPB(resp.metadata())));
    } else {
      txn_ = std::make_shared<YBTransaction>(GetOrCreateTransactionManager());
    }
    if (isolation == IsolationLevel::SNAPSHOT_ISOLATION) {
      txn_->InitWithReadPoint(isolation, std::move(*session_->read_point()));
    } else {
      RETURN_NOT_OK(txn_->Init(isolation));
    }
    session_->SetTransaction(txn_);
  }
  return Status::OK();
}

Status PgTxnManager::RestartTransaction() {
  if (!txn_in_progress_ || !txn_) {
    CHECK_NOTNULL(session_);
    if (!session_->IsRestartRequired()) {
      return STATUS(IllegalState, "Attempted to restart when session does not require restart");
    }
    session_->SetReadPoint(client::Restart::kTrue);
    return Status::OK();
  }
  if (!txn_->IsRestartRequired()) {
    return STATUS(IllegalState, "Attempted to restart when transaction does not require restart");
  }
  txn_ = VERIFY_RESULT(txn_->CreateRestartedTransaction());
  session_->SetTransaction(txn_);

  DCHECK(can_restart_.load(std::memory_order_acquire));

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
  can_restart_.store(true, std::memory_order_release);
}

}  // namespace pggate
}  // namespace yb
