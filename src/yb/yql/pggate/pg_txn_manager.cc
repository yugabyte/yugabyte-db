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

#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/pg_client.messages.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/atomic.h"
#include "yb/util/debug-util.h"
#include "yb/util/flags.h"
#include "yb/util/flags/flag_tags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"

#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pggate/util/ybc_util.h"
#include "yb/yql/pggate/ybc_pggate.h"

DEFINE_UNKNOWN_bool(use_node_hostname_for_local_tserver, false,
    "Connect to local t-server by using host name instead of local IP");

DEFINE_NON_RUNTIME_bool(ysql_rc_force_pick_read_time_on_pg_client, false,
                        "When resetting read time for a statement in Read Commited isolation level,"
                        " pick read time on the PgClientService instead of allowing the tserver to"
                        " pick one.");
TAG_FLAG(ysql_rc_force_pick_read_time_on_pg_client, advanced);

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

} // namespace

namespace yb::pggate {
namespace {

tserver::ReadTimeManipulation GetActualReadTimeManipulator(
    IsolationLevel isolation_level, tserver::ReadTimeManipulation manipulator,
    EnsureReadTimeIsSet ensure_read_time) {
  if (isolation_level == IsolationLevel::SERIALIZABLE_ISOLATION) {
    return manipulator;
  }
  switch (manipulator) {
    case tserver::ReadTimeManipulation::NONE:
      return ensure_read_time ? tserver::ReadTimeManipulation::ENSURE_READ_TIME_IS_SET
                              : manipulator;

    case tserver::ReadTimeManipulation::RESTART:
      DCHECK(!ensure_read_time);
      return manipulator;

    case tserver::ReadTimeManipulation::ENSURE_READ_TIME_IS_SET:
      return manipulator;

    case tserver::ReadTimeManipulation::ReadTimeManipulation_INT_MIN_SENTINEL_DO_NOT_USE_:
    case tserver::ReadTimeManipulation::ReadTimeManipulation_INT_MAX_SENTINEL_DO_NOT_USE_:
      break;
  }
  FATAL_INVALID_ENUM_VALUE(tserver::ReadTimeManipulation, manipulator);
}

} // namespace

DEPRECATE_FLAG(int32, pg_yb_session_timeout_ms, "02_2024");

PgTxnManager::PgTxnManager(
    PgClient* client,
    scoped_refptr<ClockBase> clock,
    PgCallbacks pg_callbacks)
    : client_(client),
      clock_(std::move(clock)),
      pg_callbacks_(pg_callbacks) {
}

PgTxnManager::~PgTxnManager() {
  // Abort the transaction before the transaction manager gets destroyed.
  WARN_NOT_OK(ExitSeparateDdlTxnModeWithAbort(), "Failed to abort DDL transaction in dtor");
  WARN_NOT_OK(AbortPlainTransaction(), "Failed to abort DML transaction in dtor");
}

Status PgTxnManager::BeginTransaction(int64_t start_time) {
  VLOG_TXN_STATE(2);
  if (YBCIsInitDbModeEnvVarSet()) {
    return Status::OK();
  }
  if (IsTxnInProgress()) {
    return STATUS(IllegalState, "Transaction is already in progress");
  }
  pg_txn_start_us_ = start_time;
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
  VLOG(2) << __func__ << " set to " << read_only_;
  return UpdateReadTimeForFollowerReadsIfRequired();
}

Status PgTxnManager::SetEnableTracing(bool tracing) {
  enable_tracing_ = tracing;
  return Status::OK();
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
    return yb::kHighPriTxnUpperBound;
  }

  if (txn_priority_requirement == kHigherPriorityRange) {
    return RandomUniformInt(txn_priority_highpri_lower_bound,
                            txn_priority_highpri_upper_bound);
  }

  return RandomUniformInt(txn_priority_regular_lower_bound,
                          txn_priority_regular_upper_bound);
}

Status PgTxnManager::CalculateIsolation(
    bool read_only_op, TxnPriorityRequirement txn_priority_requirement) {
  if (IsDdlMode()) {
    VLOG_TXN_STATE(2);
    return Status::OK();
  }

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

    VLOG_TXN_STATE(2) << "effective isolation level: " << IsolationLevel_Name(docdb_isolation)
                      << " priority_: " << priority_
                      << "; transaction started successfully.";
  }

  return Status::OK();
}

Status PgTxnManager::RestartTransaction() {
  need_restart_ = true;
  return Status::OK();
}

// This is called at the start of each statement in READ COMMITTED isolation level. Note that this
// might also be called at the start of a new retry of the statement done via
// yb_attempt_to_restart_on_error() (e.g., in case of a retry on kConflict error).
Status PgTxnManager::ResetTransactionReadPoint() {
  RSTATUS_DCHECK(
      !IsDdlMode(), IllegalState, "READ COMMITTED semantics don't apply to DDL transactions");
  ++read_time_serial_no_;
  const auto& pick_read_time_alias = FLAGS_ysql_rc_force_pick_read_time_on_pg_client;
  read_time_manipulation_ =
      PREDICT_FALSE(pick_read_time_alias) ? tserver::ReadTimeManipulation::ENSURE_READ_TIME_IS_SET
                                          : tserver::ReadTimeManipulation::NONE;
  read_time_for_follower_reads_ = HybridTime();
  RETURN_NOT_OK(UpdateReadTimeForFollowerReadsIfRequired());
  return Status::OK();
}

/* This is called when a read committed transaction wants to restart its read point */
Status PgTxnManager::RestartReadPoint() {
  read_time_manipulation_ = tserver::ReadTimeManipulation::RESTART;
  return Status::OK();
}

bool PgTxnManager::IsRestartReadPointRequested() {
  return read_time_manipulation_ == tserver::ReadTimeManipulation::RESTART;
}

void PgTxnManager::SetActiveSubTransactionId(SubTransactionId id) {
  active_sub_transaction_id_ = id;
}

Status PgTxnManager::CommitPlainTransaction() {
  return FinishPlainTransaction(Commit::kTrue);
}

Status PgTxnManager::AbortPlainTransaction() {
  return FinishPlainTransaction(Commit::kFalse);
}

Status PgTxnManager::FinishPlainTransaction(Commit commit) {
  if (PREDICT_FALSE(IsDdlMode())) {
    // GH #22353 - A DML txn must be aborted or committed only when there is no active DDL txn
    // (ie. after any active DDL txn has itself committed or aborted). Silently ignoring this
    // scenario may lead to errors in the future. Convert this to an SCHECK once the GH issue is
    // resolved.
    LOG(WARNING) << "Received DML txn commit with DDL txn state active";
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

  // Aborting a transaction is on a best-effort basis. In the event that the abort RPC to the
  // tserver fails, we simply reset the transaction state here and return any error back to the
  // caller. In the event that the tserver recovers, it will eventually expire the transaction due
  // to inactivity.
  VLOG_TXN_STATE(2) << (commit ? "Committing" : "Aborting") << " transaction.";
  Status status = client_->FinishTransaction(commit);
  VLOG_TXN_STATE(2) << "Transaction " << (commit ? "commit" : "abort") << " status: " << status;
  ResetTxnAndSession();
  return status;
}

void PgTxnManager::ResetTxnAndSession() {
  txn_in_progress_ = false;
  isolation_level_ = IsolationLevel::NON_TRANSACTIONAL;
  priority_ = 0;
  IncTxnSerialNo();

  enable_follower_reads_ = false;
  read_only_ = false;
  enable_tracing_ = false;
  read_time_for_follower_reads_ = HybridTime();
  read_time_manipulation_ = tserver::ReadTimeManipulation::NONE;
}

Status PgTxnManager::EnterSeparateDdlTxnMode() {
  RSTATUS_DCHECK(!IsDdlMode(), IllegalState,
                 "EnterSeparateDdlTxnMode called when already in a DDL transaction");
  VLOG_TXN_STATE(2);
  ddl_mode_.emplace();
  VLOG_TXN_STATE(2);
  return Status::OK();
}

Status PgTxnManager::ExitSeparateDdlTxnModeWithAbort() {
  return ExitSeparateDdlTxnMode({});
}

Status PgTxnManager::ExitSeparateDdlTxnModeWithCommit(uint32_t db_oid, bool is_silent_altering) {
  return ExitSeparateDdlTxnMode(
      DdlCommitInfo{.db_oid = db_oid, .is_silent_altering = is_silent_altering});
}

Status PgTxnManager::ExitSeparateDdlTxnMode(const std::optional<DdlCommitInfo>& commit_info) {
  VLOG_TXN_STATE(2);
  if (!IsDdlMode()) {
    RSTATUS_DCHECK(
        !commit_info, IllegalState, "Commit ddl txn called when not in a DDL transaction");
    return Status::OK();
  }
  decltype(ddl_mode_) ddl_mode;
  ddl_mode = ddl_mode_;
  if (commit_info && commit_info->is_silent_altering) {
    ddl_mode->silently_altered_db = commit_info->db_oid;
  }

  Commit commit = commit_info ? Commit::kTrue : Commit::kFalse;
  Status status = client_->FinishTransaction(commit, ddl_mode);
  WARN_NOT_OK(status, Format("Failed to $0 DDL transaction", commit ? "commit" : "abort"));
  if (PREDICT_TRUE(status.ok() || !commit)) {
    // In case of an abort, reset the DDL mode here as we may later re-enter this function and retry
    // the abort as part of transaction error recovery if the status is not ok.
    ddl_mode_.reset();
  }

  return status;
}

void PgTxnManager::SetDdlHasSyscatalogChanges() {
  if (IsDdlMode()) {
    ddl_mode_->has_docdb_schema_changes = true;
    return;
  }
  // There are only 2 cases where we may be performing DocDB schema changes outside of DDL mode:
  // 1. During initdb, when we do not use a transaction at all.
  // 2. When yb_non_ddl_txn_for_sys_tables_allowed is set. Here we would use a regular transaction.
  // has_docdb_schema_changes is mainly used for DDL atomicity, which is disabled for the PG
  // system catalog tables. Both cases above are primarily used for modifying the system catalog,
  // so there is no need to set this flag here.
  DCHECK(YBCIsInitDbModeEnvVarSet() ||
         (IsTxnInProgress() && yb_non_ddl_txn_for_sys_tables_allowed));
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

void PgTxnManager::SetupPerformOptions(
    tserver::PgPerformOptionsPB* options, EnsureReadTimeIsSet ensure_read_time) {
  if (!IsDdlMode() && !txn_in_progress_) {
    IncTxnSerialNo();
  }
  options->set_isolation(isolation_level_);
  options->set_ddl_mode(IsDdlMode());
  options->set_yb_non_ddl_txn_for_sys_tables_allowed(yb_non_ddl_txn_for_sys_tables_allowed);
  options->set_trace_requested(enable_tracing_);
  options->set_txn_serial_no(txn_serial_no_);
  options->set_read_time_serial_no(read_time_serial_no_);
  options->set_active_sub_transaction_id(active_sub_transaction_id_);

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
  if (!IsDdlMode()) {
    // The state in read_time_manipulation_ is only for kPlain transactions. And if YSQL switches to
    // kDdl mode for sometime, we should keep read_time_manipulation_ as is so that once YSQL
    // switches back to kDdl mode, the read_time_manipulation_ is not lost.
    options->set_read_time_manipulation(
        GetActualReadTimeManipulator(isolation_level_, read_time_manipulation_, ensure_read_time));
    read_time_manipulation_ = tserver::ReadTimeManipulation::NONE;
    // pg_txn_start_us is similarly only used for kPlain transactions.
    options->set_pg_txn_start_us(pg_txn_start_us_);
  }
  if (read_time_for_follower_reads_) {
    ReadHybridTime::SingleTime(read_time_for_follower_reads_).ToPB(options->mutable_read_time());
    options->set_read_from_followers(true);
  }
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

void PgTxnManager::IncTxnSerialNo() {
  ++txn_serial_no_;
  active_sub_transaction_id_ = kMinSubTransactionId;
  ++read_time_serial_no_;
}

void PgTxnManager::RestoreSessionParallelData(const YBCPgSessionParallelData* session_data) {
  txn_serial_no_ = session_data->txn_serial_no;
  read_time_serial_no_ = session_data->read_time_serial_no;
  active_sub_transaction_id_ = session_data->active_sub_transaction_id;
  VLOG_TXN_STATE(2);
}

}  // namespace yb::pggate
