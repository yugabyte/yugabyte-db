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
#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pggate/pg_txn_manager.h"

#include "yb/client/session.h"
#include "yb/client/transaction.h"

#include "yb/common/common.pb.h"
#include "yb/common/transaction_priority.h"

#include "yb/tserver/tserver_shared_mem.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/random_util.h"
#include "yb/util/status.h"

DEFINE_bool(use_node_hostname_for_local_tserver, false,
    "Connect to local t-server by using host name instead of local IP");

// A macro for logging the function name and the state of the current transaction.
// This macro is not enclosed in do { ... } while (true) because we want to be able to write
// additional information into the same log message.
#define VLOG_TXN_STATE(vlog_level) \
    VLOG(vlog_level) << __func__ << ": " << TxnStateDebugStr() \
                     << "; query: { " << ::yb::pggate::GetDebugQueryString(pg_callbacks_) << " }; "

DECLARE_bool(ysql_forward_rpcs_to_local_tserver);

namespace {

constexpr uint64_t txn_priority_highpri_upper_bound = yb::kHighPriTxnUpperBound;
constexpr uint64_t txn_priority_highpri_lower_bound = yb::kHighPriTxnLowerBound;

// Local copies that can be modified.
uint64_t txn_priority_regular_upper_bound = yb::kRegularTxnUpperBound;
uint64_t txn_priority_regular_lower_bound = yb::kRegularTxnLowerBound;

// Converts double value in range 0..1 to uint64_t value in range
// 0..(txn_priority_highpri_lower_bound - 1)
uint64_t ConvertBound(double value) {
  if (value <= 0.0) {
    return 0;
  }
  if (value >= 1.0) {
    return txn_priority_highpri_lower_bound - 1;
  }
  // Have to cast to double to avoid a warning on implicit cast that changes the value.
  return value * (static_cast<double>(txn_priority_highpri_lower_bound) - 1);
}

} // namespace

extern "C" {

void YBCAssignTransactionPriorityLowerBound(double newval, void* extra) {
  txn_priority_regular_lower_bound = ConvertBound(newval);
  // YSQL layer checks (guc.c) should ensure this.
  DCHECK_LE(txn_priority_regular_lower_bound, txn_priority_regular_upper_bound);
}

void YBCAssignTransactionPriorityUpperBound(double newval, void* extra) {
  txn_priority_regular_upper_bound = ConvertBound(newval);
  // YSQL layer checks (guc.c) should ensure this.
  DCHECK_LE(txn_priority_regular_lower_bound, txn_priority_regular_upper_bound);
}

}

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

#if defined(__APPLE__) && !defined(NDEBUG)
// We are experiencing more slowness in tests on macOS in debug mode.
const int kDefaultPgYbSessionTimeoutMs = 120 * 1000;
#else
const int kDefaultPgYbSessionTimeoutMs = 60 * 1000;
#endif

DEFINE_int32(pg_yb_session_timeout_ms, kDefaultPgYbSessionTimeoutMs,
             "Timeout for operations between PostgreSQL server and YugaByte DocDB services");

std::shared_ptr<yb::client::YBSession> BuildSession(
    yb::client::YBClient* client,
    const scoped_refptr<ClockBase>& clock) {
  auto session = std::make_shared<YBSession>(client, clock);
  session->SetForceConsistentRead(client::ForceConsistentRead::kTrue);
  session->SetTimeout(MonoDelta::FromMilliseconds(FLAGS_pg_yb_session_timeout_ms));
  return session;
}

PgTxnManager::PgTxnManager(
    AsyncClientInitialiser* async_client_init,
    scoped_refptr<ClockBase> clock,
    const tserver::TServerSharedObject* tserver_shared_object,
    PgCallbacks pg_callbacks)
    : async_client_init_(async_client_init),
      clock_(std::move(clock)),
      tserver_shared_object_(tserver_shared_object),
      pg_callbacks_(pg_callbacks) {
}

PgTxnManager::~PgTxnManager() {
  // Abort the transaction before the transaction manager gets destroyed.
  if (txn_) {
    txn_->Abort();
  }
  ResetTxnAndSession();
}

Status PgTxnManager::BeginTransaction() {
  VLOG_TXN_STATE(2);
  if (txn_in_progress_) {
    return STATUS(IllegalState, "Transaction is already in progress");
  }
  return RecreateTransaction(SavePriority::kFalse /* save_priority */);
}

Status PgTxnManager::RecreateTransaction() {
  VLOG_TXN_STATE(2);
  if (!txn_) {
    return Status::OK();
  }
  return RecreateTransaction(SavePriority::kTrue /* save_priority */);
}

Status PgTxnManager::RecreateTransaction(const SavePriority save_priority) {
  use_saved_priority_ = save_priority;
  if (save_priority) {
    saved_priority_ = txn_->GetPriority();
  }

  ResetTxnAndSession();
  txn_in_progress_ = true;
  StartNewSession();
  return Status::OK();
}

Status PgTxnManager::SetIsolationLevel(int level) {
  pg_isolation_level_ = static_cast<PgIsolationLevel>(level);
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
  session_ = BuildSession(async_client_init_->client(), clock_);
  session_->SetReadPoint(client::Restart::kFalse);
}

uint64_t PgTxnManager::GetPriority(const NeedsPessimisticLocking needs_pessimistic_locking) {
  if (use_saved_priority_) {
    return saved_priority_;
  }

  // Use high priority for transactions that need pessimistic locking.
  if (needs_pessimistic_locking) {
    return RandomUniformInt(txn_priority_highpri_lower_bound,
                            txn_priority_highpri_upper_bound);
  }
  return RandomUniformInt(txn_priority_regular_lower_bound,
                          txn_priority_regular_upper_bound);
}

Status PgTxnManager::BeginWriteTransactionIfNecessary(bool read_only_op,
                                                      bool needs_pessimistic_locking) {
  if (ddl_txn_) {
    VLOG_TXN_STATE(2);
    return Status::OK();
  }

  // Using pg_isolation_level_, read_only_, and deferrable_, determine the effective isolation level
  // to use at the DocDB layer, and the "deferrable" flag.
  //
  // Effective isolation means that sometimes SERIALIZABLE reads are internally executed as snapshot
  // isolation reads. This way we don't have to write read intents and we get higher peformance.
  // The resulting execution is still serializable: the order of transactions is the order of
  // timestamps, i.e. read timestamps (for read-only transactions executed at snapshot isolation)
  // and commit timestamps of serializable transactions.
  //
  // The "deferrable" flag that in SERIALIZABLE DEFERRABLE READ ONLY mode we will choose the read
  // timestamp as global_limit to avoid the possibility of read restarts. This results in waiting
  // out the maximum clock skew and is appropriate for non-latency-sensitive operations.

  const IsolationLevel docdb_isolation =
      (pg_isolation_level_ == PgIsolationLevel::SERIALIZABLE) && !read_only_
          ? IsolationLevel::SERIALIZABLE_ISOLATION
          : IsolationLevel::SNAPSHOT_ISOLATION;
  const bool defer = read_only_ && deferrable_;

  VLOG_TXN_STATE(2) << "DocDB isolation level: " << IsolationLevel_Name(docdb_isolation);

  if (txn_) {
    // Sanity check: query layer should ensure that this does not happen.
    if (txn_->isolation() != docdb_isolation) {
      return STATUS_FORMAT(
          IllegalState,
          "Attempt to change effective isolation from $0 to $1 in the middle of a transaction. "
          "Postgres-level isolation: $2; read_only: $3.",
          txn_->isolation(), IsolationLevel_Name(docdb_isolation), pg_isolation_level_,
          read_only_);
    }
  } else if (read_only_op && docdb_isolation == IsolationLevel::SNAPSHOT_ISOLATION) {
    if (defer) {
      // This call is idempotent, meaning it has no effect after the first call.
      session_->DeferReadPoint();
    }
  } else {
    if (tserver_shared_object_) {
      if (!tablet_server_proxy_) {
        boost::optional<MonoDelta> resolve_cache_timeout;
        const auto& tserver_shared_data_ = **tserver_shared_object_;
        HostPort host_port(tserver_shared_data_.endpoint());
        if (FLAGS_use_node_hostname_for_local_tserver) {
          host_port = HostPort(tserver_shared_data_.host().ToBuffer(),
                               tserver_shared_data_.endpoint().port());
          resolve_cache_timeout = MonoDelta::kMax;
        }
        LOG(INFO) << "Using TServer host_port: " << host_port;
        tablet_server_proxy_ = std::make_unique<tserver::TabletServerServiceProxy>(
            &async_client_init_->client()->proxy_cache(), host_port,
            nullptr /* protocol */, resolve_cache_timeout);
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

    txn_->SetPriority(GetPriority(NeedsPessimisticLocking(needs_pessimistic_locking)));

    if (docdb_isolation == IsolationLevel::SNAPSHOT_ISOLATION) {
      txn_->InitWithReadPoint(docdb_isolation, std::move(*session_->read_point()));
    } else {
      DCHECK_EQ(docdb_isolation, IsolationLevel::SERIALIZABLE_ISOLATION);
      RETURN_NOT_OK(txn_->Init(docdb_isolation));
    }
    session_->SetTransaction(txn_);

    VLOG_TXN_STATE(2) << "effective isolation level: "
                      << IsolationLevel_Name(docdb_isolation)
                      << "; transaction started successfully.";
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
    VLOG_TXN_STATE(2) << "No transaction in progress, nothing to commit.";
    return Status::OK();
  }

  if (!txn_) {
    VLOG_TXN_STATE(2) << "This was a read-only transaction, nothing to commit.";
    ResetTxnAndSession();
    return Status::OK();
  }
  VLOG_TXN_STATE(2) << "committing transaction.";
  Status status = txn_->CommitFuture().get();
  VLOG_TXN_STATE(2) << "transaction commit status: " << status;
  ResetTxnAndSession();
  return status;
}

Status PgTxnManager::AbortTransaction() {
  // If a DDL operation during a DDL txn fails the txn will be aborted before we get here.
  // However if there are failures afterwards (i.e. during COMMIT or catalog version increment),
  // then we might get here with a ddl_txn_. Clean it up in that case.
  if (ddl_txn_) {
    RETURN_NOT_OK(ExitSeparateDdlTxnMode(false));
  }

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
  if (ddl_session_) {
    VLOG_TXN_STATE(2) << "Using the DDL session: " << ddl_session_.get();
    return ddl_session_.get();
  }
  if (!txn_in_progress_) {
    RETURN_NOT_OK(BeginTransaction());
  }
  VLOG_TXN_STATE(2) << "Using the non-DDL transactional session: " << session_.get();
  return session_.get();
}

std::shared_future<Result<TransactionMetadata>> PgTxnManager::GetDdlTxnMetadata() const {
  return ddl_txn_->GetMetadata();
}

void PgTxnManager::ResetTxnAndSession() {
  txn_in_progress_ = false;
  session_ = nullptr;
  txn_ = nullptr;
  can_restart_.store(true, std::memory_order_release);
}

Status PgTxnManager::EnterSeparateDdlTxnMode() {
  RSTATUS_DCHECK(!ddl_txn_,
          IllegalState, "EnterSeparateDdlTxnMode called when already in a DDL transaction");
  VLOG_TXN_STATE(2);
  ddl_session_ = BuildSession(async_client_init_->client(), clock_);
  ddl_txn_ = std::make_shared<YBTransaction>(GetOrCreateTransactionManager());
  ddl_session_->SetTransaction(ddl_txn_);
  RETURN_NOT_OK(ddl_txn_->Init(
      FLAGS_ysql_serializable_isolation_for_ddl_txn ? IsolationLevel::SERIALIZABLE_ISOLATION
                                                    : IsolationLevel::SNAPSHOT_ISOLATION));
  VLOG_TXN_STATE(2);
  return Status::OK();
}

Status PgTxnManager::ExitSeparateDdlTxnMode(bool is_success) {
  VLOG_TXN_STATE(2) << "is_success=" << is_success;
  RSTATUS_DCHECK(
      ddl_txn_ != nullptr,
      IllegalState, "ExitSeparateDdlTxnMode called when not in a DDL transaction");
  if (is_success) {
    RETURN_NOT_OK(ddl_txn_->CommitFuture().get());
  } else {
    ddl_txn_->Abort();
  }
  ddl_txn_.reset();
  ddl_session_.reset();
  return Status::OK();
}

std::string PgTxnManager::TxnStateDebugStr() const {
  return YB_CLASS_TO_STRING(
      txn,
      ddl_txn,
      read_only,
      deferrable,
      txn_in_progress,
      pg_isolation_level);
}

}  // namespace pggate
}  // namespace yb
