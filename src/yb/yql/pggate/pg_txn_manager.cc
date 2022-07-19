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

#include "yb/yql/pggate/pg_txn_manager.h"

#include "yb/common/common.pb.h"
#include "yb/common/transaction_priority.h"
#include "yb/common/ybc_util.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/pg_client.messages.h"
#include "yb/tserver/tserver_service.proxy.h"
#include "yb/tserver/tserver_shared_mem.h"

#include "yb/util/debug-util.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/shared_mem.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"

#include "yb/yql/pggate/pg_client.h"
#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pggate/ybc_pggate.h"

DEFINE_bool(use_node_hostname_for_local_tserver, false,
    "Connect to local t-server by using host name instead of local IP");

// A macro for logging the function name and the state of the current transaction.
// This macro is not enclosed in do { ... } while (true) because we want to be able to write
// additional information into the same log message.
#define VLOG_TXN_STATE(vlog_level) \
    VLOG(vlog_level) << __func__ << ": " << TxnStateDebugStr() \
                     << "; query: { " << ::yb::pggate::GetDebugQueryString(pg_callbacks_) << " }; "

DECLARE_uint64(max_clock_skew_usec);

namespace {

// Local copies that can be modified.
uint64_t txn_priority_highpri_upper_bound = yb::kHighPriTxnUpperBound;
uint64_t txn_priority_highpri_lower_bound = yb::kHighPriTxnLowerBound;

uint64_t txn_priority_regular_upper_bound = yb::kRegularTxnUpperBound;
uint64_t txn_priority_regular_lower_bound = yb::kRegularTxnLowerBound;

// Converts double value in range 0..1 to uint64_t value in range [minValue, maxValue]
uint64_t ConvertBound(long double value, uint64_t minValue, uint64_t maxValue) {
  if (value <= 0.0) {
    return minValue;
  }

  if (value >= 1.0) {
    return maxValue;
  }

  return minValue + value * (maxValue - minValue);
}

uint64_t ConvertRegularPriorityTxnBound(double value) {
  return ConvertBound(value, yb::kRegularTxnLowerBound, yb::kRegularTxnUpperBound);
}

uint64_t ConvertHighPriorityTxnBound(double value) {
  return ConvertBound(value, yb::kHighPriTxnLowerBound, yb::kHighPriTxnUpperBound);
}

// Convert uint64_t value in range [minValue, maxValue] to double value in range 0..1
double ToTxnPriority(uint64_t value, uint64_t minValue, uint64_t maxValue) {
  if (value <= minValue) {
    return 0.0;
  }

  if (value >= maxValue) {
    return 1.0;
  }

  return static_cast<double>(value - minValue) / (maxValue - minValue);
}

} // namespace

extern "C" {

void YBCAssignTransactionPriorityLowerBound(double newval, void* extra) {
  txn_priority_regular_lower_bound = ConvertRegularPriorityTxnBound(newval);
  txn_priority_highpri_lower_bound = ConvertHighPriorityTxnBound(newval);
  // YSQL layer checks (guc.c) should ensure this.
  DCHECK_LE(txn_priority_regular_lower_bound, txn_priority_regular_upper_bound);
  DCHECK_LE(txn_priority_highpri_lower_bound, txn_priority_highpri_upper_bound);
  DCHECK_LE(txn_priority_regular_lower_bound, txn_priority_highpri_lower_bound);
}

void YBCAssignTransactionPriorityUpperBound(double newval, void* extra) {
  txn_priority_regular_upper_bound = ConvertRegularPriorityTxnBound(newval);
  txn_priority_highpri_upper_bound = ConvertHighPriorityTxnBound(newval);
  // YSQL layer checks (guc.c) should ensure this.
  DCHECK_LE(txn_priority_regular_lower_bound, txn_priority_regular_upper_bound);
  DCHECK_LE(txn_priority_highpri_lower_bound, txn_priority_highpri_upper_bound);
  DCHECK_LE(txn_priority_regular_upper_bound, txn_priority_highpri_lower_bound);
}

int* YBCStatementTimeoutPtr = nullptr;

}

namespace yb {
namespace pggate {

#if defined(__APPLE__) && !defined(NDEBUG)
// We are experiencing more slowness in tests on macOS in debug mode.
const int kDefaultPgYbSessionTimeoutMs = 120 * 1000;
#else
const int kDefaultPgYbSessionTimeoutMs = 60 * 1000;
#endif

DEFINE_int32(pg_yb_session_timeout_ms, kDefaultPgYbSessionTimeoutMs,
             "Timeout for operations between PostgreSQL server and YugaByte DocDB services");

PgTxnManager::PgTxnManager(
    PgClient* client,
    scoped_refptr<ClockBase> clock,
    const tserver::TServerSharedObject* tserver_shared_object,
    PgCallbacks pg_callbacks)
    : client_(client),
      clock_(std::move(clock)),
      tserver_shared_object_(tserver_shared_object),
      pg_callbacks_(pg_callbacks) {
}

PgTxnManager::~PgTxnManager() {
  // Abort the transaction before the transaction manager gets destroyed.
  WARN_NOT_OK(AbortTransaction(), "Failed to abort transaction in dtor");
}

Status PgTxnManager::BeginTransaction() {
  VLOG_TXN_STATE(2);
  if (YBCIsInitDbModeEnvVarSet()) {
    return Status::OK();
  }
  if (IsTxnInProgress()) {
    return STATUS(IllegalState, "Transaction is already in progress");
  }
  return RecreateTransaction(SavePriority::kFalse);
}

Status PgTxnManager::RecreateTransaction() {
  VLOG_TXN_STATE(2);
  return RecreateTransaction(SavePriority::kTrue);
}

Status PgTxnManager::RecreateTransaction(SavePriority save_priority) {
  use_saved_priority_ = save_priority;
  ResetTxnAndSession();
  txn_in_progress_ = true;
  return Status::OK();
}

Status PgTxnManager::SetPgIsolationLevel(int level) {
  pg_isolation_level_ = static_cast<PgIsolationLevel>(level);
  return Status::OK();
}

PgIsolationLevel PgTxnManager::GetPgIsolationLevel() {
  return pg_isolation_level_;
}

Status PgTxnManager::SetReadOnly(bool read_only) {
  read_only_ = read_only;
  VLOG(2) << __func__ << " set to " << read_only_ << " from " << GetStackTrace();
  return UpdateReadTimeForFollowerReadsIfRequired();
}

Status PgTxnManager::EnableFollowerReads(bool enable_follower_reads, int32_t session_staleness) {
  VLOG_TXN_STATE(2) << (enable_follower_reads ? "Enabling follower reads "
                                              : "Disabling follower reads ")
                    << " with staleness " << session_staleness << " ms";
  enable_follower_reads_ = enable_follower_reads;
  follower_read_staleness_ms_ = session_staleness;
  return UpdateReadTimeForFollowerReadsIfRequired();
}

Status PgTxnManager::UpdateReadTimeForFollowerReadsIfRequired() {
  if (enable_follower_reads_ && read_only_ && !read_time_for_follower_reads_) {
    constexpr uint64_t kMargin = 2;
    RSTATUS_DCHECK(
        follower_read_staleness_ms_ * 1000 > kMargin * GetAtomicFlag(&FLAGS_max_clock_skew_usec),
        InvalidArgument,
        Format("Setting follower read staleness less than the $0 x max_clock_skew.", kMargin));
    // Add a delta to the start point to lower the read point.
    read_time_for_follower_reads_ = clock_->Now().AddMilliseconds(-follower_read_staleness_ms_);
    VLOG_TXN_STATE(2) << "Updating read-time with staleness "
                      << follower_read_staleness_ms_ << " to "
                      << read_time_for_follower_reads_;
  } else {
    VLOG(2) << " Not updating read-time " << yb::ToString(pg_isolation_level_)
            << read_time_for_follower_reads_
            << (enable_follower_reads_ ? " Follower reads allowed." : " Follower reads DISallowed.")
            << (read_only_ ? " Is read-only" : " Is NOT read-only");
  }
  return Status::OK();
}

Status PgTxnManager::SetDeferrable(bool deferrable) {
  deferrable_ = deferrable;
  return Status::OK();
}

uint64_t PgTxnManager::NewPriority(TxnPriorityRequirement txn_priority_requirement) {
  if (txn_priority_requirement == kHighestPriority) {
    return txn_priority_highpri_upper_bound;
  }

  if (txn_priority_requirement == kHigherPriorityRange) {
    return RandomUniformInt(txn_priority_highpri_lower_bound,
                            txn_priority_highpri_upper_bound);
  }

  return RandomUniformInt(txn_priority_regular_lower_bound,
                          txn_priority_regular_upper_bound);
}

Status PgTxnManager::CalculateIsolation(bool read_only_op,
                                        TxnPriorityRequirement txn_priority_requirement,
                                        uint64_t* in_txn_limit) {
  if (ddl_mode_) {
    VLOG_TXN_STATE(2);
    return Status::OK();
  }

  auto se = ScopeExit([this, in_txn_limit] {
    if (in_txn_limit) {
      if (!*in_txn_limit) {
        *in_txn_limit = clock_->Now().ToUint64();
      }
      in_txn_limit_ = HybridTime(*in_txn_limit);
    }
  });

  VLOG_TXN_STATE(2);
  if (!txn_in_progress_) {
    return RecreateTransaction(SavePriority::kFalse);
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
          : (pg_isolation_level_ == PgIsolationLevel::READ_COMMITTED
              ? IsolationLevel::READ_COMMITTED
              : IsolationLevel::SNAPSHOT_ISOLATION);
  const bool defer = read_only_ && deferrable_;

  VLOG_TXN_STATE(2) << "DocDB isolation level: " << IsolationLevel_Name(docdb_isolation);

  if (isolation_level_ != IsolationLevel::NON_TRANSACTIONAL) {
    // Sanity check: query layer should ensure that this does not happen.
    if (isolation_level_ != docdb_isolation) {
      return STATUS_FORMAT(
          IllegalState,
          "Attempt to change effective isolation from $0 to $1 in the middle of a transaction. "
          "Postgres-level isolation: $2; read_only: $3.",
          isolation_level_, IsolationLevel_Name(docdb_isolation), pg_isolation_level_,
          read_only_);
    }
  } else if (read_only_op &&
             (docdb_isolation == IsolationLevel::SNAPSHOT_ISOLATION ||
              docdb_isolation == IsolationLevel::READ_COMMITTED)) {
    if (defer) {
      need_defer_read_point_ = true;
    }
  } else {
    if (!use_saved_priority_) {
      priority_ = NewPriority(txn_priority_requirement);
    }
    isolation_level_ = docdb_isolation;

    VLOG_TXN_STATE(2) << "effective isolation level: "
                      << IsolationLevel_Name(docdb_isolation)
                      << "; transaction started successfully.";
  }

  return Status::OK();
}

Status PgTxnManager::RestartTransaction() {
  need_restart_ = true;
  return Status::OK();
}

/* This is called at the start of each statement in READ COMMITTED isolation level */
Status PgTxnManager::ResetTransactionReadPoint() {
  RSTATUS_DCHECK(!ddl_mode_, IllegalState,
                 "READ COMMITTED semantics don't apply to DDL transactions");
  read_time_manipulation_ = tserver::ReadTimeManipulation::RESET;
  read_time_for_follower_reads_ = HybridTime();
  RETURN_NOT_OK(UpdateReadTimeForFollowerReadsIfRequired());
  return Status::OK();
}

/* This is called when a read committed transaction wants to restart its read point */
Status PgTxnManager::RestartReadPoint() {
  read_time_manipulation_ = tserver::ReadTimeManipulation::RESTART;
  return Status::OK();
}

Status PgTxnManager::CommitTransaction() {
  return FinishTransaction(Commit::kTrue);
}

Status PgTxnManager::FinishTransaction(Commit commit) {
  // If a DDL operation during a DDL txn fails the txn will be aborted before we get here.
  // However if there are failures afterwards (i.e. during COMMIT or catalog version increment),
  // then we might get here with a ddl_txn_. Clean it up in that case.
  if (ddl_mode_ && !commit) {
    RETURN_NOT_OK(ExitSeparateDdlTxnMode(commit));
  }

  if (!txn_in_progress_) {
    VLOG_TXN_STATE(2) << "No transaction in progress, nothing to commit.";
    return Status::OK();
  }

  if (isolation_level_ == IsolationLevel::NON_TRANSACTIONAL) {
    VLOG_TXN_STATE(2) << "This was a read-only transaction, nothing to commit.";
    ResetTxnAndSession();
    return Status::OK();
  }

  VLOG_TXN_STATE(2) << (commit ? "Committing" : "Aborting") << " transaction.";
  Status status = client_->FinishTransaction(commit, DdlMode::kFalse);
  VLOG_TXN_STATE(2) << "Transaction " << (commit ? "commit" : "abort") << " status: " << status;
  ResetTxnAndSession();
  return status;
}

Status PgTxnManager::AbortTransaction() {
  return FinishTransaction(Commit::kFalse);
}

void PgTxnManager::ResetTxnAndSession() {
  txn_in_progress_ = false;
  isolation_level_ = IsolationLevel::NON_TRANSACTIONAL;
  priority_ = 0;
  in_txn_limit_ = HybridTime();
  ++txn_serial_no_;

  enable_follower_reads_ = false;
  read_only_ = false;
  read_time_for_follower_reads_ = HybridTime();
  read_time_manipulation_ = tserver::ReadTimeManipulation::NONE;
}

Status PgTxnManager::EnterSeparateDdlTxnMode() {
  RSTATUS_DCHECK(!ddl_mode_, IllegalState,
                 "EnterSeparateDdlTxnMode called when already in a DDL transaction");
  VLOG_TXN_STATE(2);
  ddl_mode_ = true;
  VLOG_TXN_STATE(2);
  return Status::OK();
}

Status PgTxnManager::ExitSeparateDdlTxnMode(Commit commit) {
  VLOG_TXN_STATE(2);
  if (!ddl_mode_) {
    RSTATUS_DCHECK(!commit, IllegalState, "Commit ddl txn called when not in a DDL transaction");
    return Status::OK();
  }
  RETURN_NOT_OK(client_->FinishTransaction(commit, DdlMode::kTrue));
  ddl_mode_ = false;
  return Status::OK();
}

std::string PgTxnManager::TxnStateDebugStr() const {
  return YB_CLASS_TO_STRING(
      ddl_mode,
      read_only,
      deferrable,
      txn_in_progress,
      pg_isolation_level,
      isolation_level);
}

uint64_t PgTxnManager::SetupPerformOptions(tserver::PgPerformOptionsPB* options) {
  if (!ddl_mode_ && !txn_in_progress_) {
    ++txn_serial_no_;
  }
  options->set_isolation(isolation_level_);
  options->set_ddl_mode(ddl_mode_);
  options->set_txn_serial_no(txn_serial_no_);
  if (txn_in_progress_ && in_txn_limit_) {
    options->set_in_txn_limit_ht(in_txn_limit_.ToUint64());
  }
  if (use_saved_priority_) {
    options->set_use_existing_priority(true);
  } else {
    options->set_priority(priority_);
  }
  if (need_restart_) {
    options->set_restart_transaction(true);
    need_restart_ = false;
  }
  if (need_defer_read_point_) {
    options->set_defer_read_point(true);
    need_defer_read_point_ = false;
  }
  if (!ddl_mode_) {
    // The state in read_time_manipulation_ is only for kPlain transactions. And if YSQL switches to
    // kDdl mode for sometime, we should keep read_time_manipulation_ as is so that once YSQL
    // switches back to kDdl mode, the read_time_manipulation_ is not lost.
    options->set_read_time_manipulation(read_time_manipulation_);
    read_time_manipulation_ = tserver::ReadTimeManipulation::NONE;
  }
  if (read_time_for_follower_reads_) {
    ReadHybridTime::SingleTime(read_time_for_follower_reads_).ToPB(options->mutable_read_time());
  }
  return txn_serial_no_;
}

double PgTxnManager::GetTransactionPriority() const {
  if (priority_ <= yb::kRegularTxnUpperBound) {
    return ToTxnPriority(priority_,
                         yb::kRegularTxnLowerBound,
                         yb::kRegularTxnUpperBound);
  }

  return ToTxnPriority(priority_,
                       yb::kHighPriTxnLowerBound,
                       yb::kHighPriTxnUpperBound);
}

TxnPriorityRequirement PgTxnManager::GetTransactionPriorityType() const {
  if (priority_ <= yb::kRegularTxnUpperBound) {
    return kLowerPriorityRange;
  }
  if (priority_ < yb::kHighPriTxnUpperBound) {
    return kHigherPriorityRange;
  }
  return kHighestPriority;
}

}  // namespace pggate
}  // namespace yb
