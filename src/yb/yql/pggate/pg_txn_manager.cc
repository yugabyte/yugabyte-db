// Copyright (c) YugabyteDB, Inc.
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
#include "yb/common/row_mark.h"
#include "yb/common/transaction_priority.h"

#include "yb/docdb/object_lock_shared_state.h"

#include "yb/gutil/casts.h"

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
#include "yb/yql/pggate/pggate.h"
#include "yb/yql/pggate/util/ybc_util.h"
#include "yb/yql/pggate/ybc_pggate.h"

DEFINE_UNKNOWN_bool(use_node_hostname_for_local_tserver, false,
    "Connect to local t-server by using host name instead of local IP");

DEFINE_NON_RUNTIME_bool(ysql_rc_force_pick_read_time_on_pg_client, false,
                        "When resetting read time for a statement in Read Commited isolation level,"
                        " pick read time on the PgClientService instead of allowing the tserver to"
                        " pick one.");
TAG_FLAG(ysql_rc_force_pick_read_time_on_pg_client, advanced);

DEFINE_RUNTIME_PG_FLAG(bool, yb_follower_reads_behavior_before_fixing_20482, false,
    "Controls whether ysql follower reads that is enabled inside a transaction block "
    "should take effect in the same transaction or not. Prior to fixing #20482 the "
    "behavior was that the change does not affect the current transaction but only "
    "affects subsequent transactions. The flag is intended to be used if there is "
    "a customer who relies on the old behavior.");
TAG_FLAG(ysql_yb_follower_reads_behavior_before_fixing_20482, advanced);

// A macro for logging the function name and the state of the current transaction.
// This macro is not enclosed in do { ... } while (true) because we want to be able to write
// additional information into the same log message.
#define VLOG_TXN_STATE(vlog_level) \
    VLOG(vlog_level) << __func__ << ": " << TxnStateDebugStr() \
                     << "; query: { " << ::yb::pggate::GetDebugQueryString(pg_callbacks_) << " }; "

DECLARE_bool(enable_object_locking_for_table_locks);

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
    std::optional<ReadTimeAction> read_time_action) {
  if (isolation_level == IsolationLevel::SERIALIZABLE_ISOLATION) {
    return manipulator;
  }
  const auto ensure_read_time =
      read_time_action && (*read_time_action) == ReadTimeAction::ENSURE_IS_SET;
  switch (manipulator) {
    case tserver::ReadTimeManipulation::NONE:
      return ensure_read_time
          ? tserver::ReadTimeManipulation::ENSURE_READ_TIME_IS_SET : manipulator;

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

PgTxnManager::SerialNo::SerialNo(std::atomic<uint64_t>& next_read_time_serial_no)
    : next_read_time_serial_no_(next_read_time_serial_no),
      txn_(0),
      read_time_(0),
      min_read_time_(0),
      max_read_time_(0) {
}

void PgTxnManager::SerialNo::Set(uint64_t txn_serial_no, uint64_t read_time_serial_no) {
  txn_ = txn_serial_no;
  read_time_ = min_read_time_ = max_read_time_ = read_time_serial_no;
}

uint64_t PgTxnManager::SerialNo::NextReadTimeSerialNo() {
  // fetch_add returns the previous value, so the first allocated serial number is 1. This matches
  // the historical behaviour where read time serial numbers started from 1.
  return next_read_time_serial_no_.fetch_add(1, std::memory_order_acq_rel) + 1;
}

void PgTxnManager::SerialNo::IncTxn(
    bool preserve_read_time_history, YbcReadPointHandle catalog_read_time_serial_no) {
  VLOG_WITH_FUNC(4)
      << "Inc txn from old txn_: " << txn_
      << ", min_read_time=" << min_read_time_
      << " , preserve_read_time_history=" << preserve_read_time_history;
  ++txn_;
  IncReadTime();
  if (!preserve_read_time_history) {
    min_read_time_ = read_time_;
    if (!YBCIsLegacyModeForCatalogOps() && catalog_read_time_serial_no &&
        catalog_read_time_serial_no < min_read_time_) {
      min_read_time_ = catalog_read_time_serial_no;
    }
    VLOG_WITH_FUNC(4) << "Updated min_read_time_ to: " << min_read_time_;
  }
}

void PgTxnManager::SerialNo::IncReadTime() {
  read_time_ = max_read_time_ = NextReadTimeSerialNo();
  VLOG(4) << "IncReadTime to " << max_read_time_;
}

void PgTxnManager::SerialNo::IncMaxReadTime() {
  max_read_time_ = NextReadTimeSerialNo();
  VLOG(4) << "IncMaxReadTime to " << max_read_time_;
}

Status PgTxnManager::SerialNo::RestoreReadTime(uint64_t read_time_serial_no) {
  RSTATUS_DCHECK(
      read_time_serial_no <= max_read_time_ && read_time_serial_no >= min_read_time_, IllegalState,
      "Bad read time serial no $0 while [$1, $2] is expected", read_time_serial_no, min_read_time_,
      max_read_time_);
  read_time_ = read_time_serial_no;
  return Status::OK();
}

#ifndef NDEBUG
void PgTxnManager::DEBUG_UpdateLastObjectLockingInfo() {
  debug_last_object_locking_txn_serial_ = serial_no_.txn();
}

void PgTxnManager::DEBUG_CheckOptionsForPerform(
    const tserver::PgPerformOptionsPB& options) const {
  // Functions like YBCheckSharedCatalogCacheVersion, being executed outside scope of a
  // transaction block seem to issue custom selects on pg_yb_catalog_version table using
  // YBCPgNewSelect, skipping object locks. Skip the assertion for such cases for now.
  if (!IsTableLockingEnabledForCurrentTxn() || options.ddl_mode() ||
      options.use_legacy_catalog_session() || options.yb_non_ddl_txn_for_sys_tables_allowed() ||
      YBCIsInitDbModeEnvVarSet() || !YBCIsLegacyModeForCatalogOps()) {
    return;
  }
  LOG_IF(DFATAL, debug_last_object_locking_txn_serial_ != serial_no_.txn())
      << "txn state: " << TxnStateDebugStr()
      << ", last object locking txn serial: " << debug_last_object_locking_txn_serial_
      << ", query: {" << ::yb::pggate::GetDebugQueryString(pg_callbacks_) << "}";
}
#endif

PgTxnManager::PgTxnManager(
    PgClient* client, YbcPgCallbacks pg_callbacks, bool enable_table_locking,
    std::atomic<uint64_t>& next_read_time_serial_no)
    : client_(client),
      serial_no_(next_read_time_serial_no),
      pg_callbacks_(pg_callbacks),
      enable_table_locking_(enable_table_locking) {}

bool PgTxnManager::ShouldEnableTableLocking() const {
  VLOG_WITH_FUNC(1) << "enable_table_locking_: " << enable_table_locking_
                    << " enable_object_locking_infra: " << enable_object_locking_infra;
  return enable_table_locking_ && enable_object_locking_infra;
}

bool PgTxnManager::IsTableLockingEnabledForCurrentTxn() const {
  return using_table_locks_;
}

PgTxnManager::~PgTxnManager() = default;

void PgTxnManager::Shutdown() {
  // Abort the transaction before the transaction manager gets destroyed.
  WARN_NOT_OK(ExitSeparateDdlTxnModeWithAbort(),
              "Failed to abort separate DDL transaction during shutdown");
  WARN_NOT_OK(AbortPlainTransaction(), "Failed to abort plain transaction during shutdown");
}

Status PgTxnManager::BeginTransaction(int64_t start_time) {
  VLOG_TXN_STATE(2);
  if (YBCIsInitDbModeEnvVarSet()) {
    return Status::OK();
  }
  VLOG(2) << "BeginTransaction";
  if (IsTxnInProgress()) {
    VLOG(3) << "Transaction is already in progress, skipping creation of a new transaction";
    return Status::OK();
  }

  pg_txn_start_us_ = start_time;
  // Table Locking auto flag can only go from off -> on. Not the other way around.
  using_table_locks_ = using_table_locks_ || ShouldEnableTableLocking();
  VLOG_WITH_FUNC(1) << "using_table_locks_: " << using_table_locks_;
  // NOTE: Do not reset in_txn_blk_ when restarting txns internally
  // (i.e., via PgTxnManager::RecreateTransaction).
  in_txn_blk_ = false;
  return RecreateTransaction(SavePriority::kFalse);
}

Status PgTxnManager::RecreateTransaction() {
  VLOG_TXN_STATE(2);
  return RecreateTransaction(SavePriority::kTrue);
}

Status PgTxnManager::RecreateTransaction(SavePriority save_priority) {
  VLOG(2) << "RecreateTransaction";
  use_saved_priority_ = save_priority;
  ResetTxnAndSession();
  txn_in_progress_ = true;
  return Status::OK();
}

Status PgTxnManager::SetPgIsolationLevel(int level) {
  pg_isolation_level_ = static_cast<PgIsolationLevel>(level);
  return Status::OK();
}

PgIsolationLevel PgTxnManager::GetPgIsolationLevel() const {
  return pg_isolation_level_;
}

void PgTxnManager::SetReadOnly(bool read_only) {
  read_only_ = read_only;
  VLOG(2) << __func__ << " set to " << read_only_;
}

Status PgTxnManager::SetEnableTracing(bool tracing) {
  enable_tracing_ = tracing;
  return Status::OK();
}

void PgTxnManager::UpdateFollowerReadsConfig(
    bool enable_follower_reads, int32_t session_staleness) {
  VLOG_TXN_STATE(2) << (enable_follower_reads ? "Enabling follower reads "
                                              : "Disabling follower reads ")
                    << " with staleness " << session_staleness << " ms";
  follower_read_staleness_ms_ =
      enable_follower_reads ? std::optional<uint64_t>(session_staleness) : std::nullopt;
}

bool PgTxnManager::UsesFollowerReads() const {
  return follower_read_staleness_ms_ && read_only_;
}

Status PgTxnManager::SetDeferrable(bool deferrable) {
  deferrable_ = deferrable;
  return Status::OK();
}

Status PgTxnManager::SetInTxnBlock(bool in_txn_blk) {
  in_txn_blk_ = in_txn_blk;
  VLOG_WITH_FUNC(2) << (in_txn_blk ? "In " : "Not in ") << "txn block.";
  return Status::OK();
}

Status PgTxnManager::SetReadOnlyStmt(bool read_only_stmt) {
  read_only_stmt_ = read_only_stmt;
  VLOG_WITH_FUNC(2)
    << "Executing a " << (read_only_stmt ? "read only " : "read/write ")
    << "stmt.";
  return Status::OK();
}

uint64_t PgTxnManager::NewPriority(YbcTxnPriorityRequirement txn_priority_requirement) {
  VLOG(1) << "txn_priority_requirement: " << txn_priority_requirement
          << " txn_priority_highpri_lower_bound: " << txn_priority_highpri_lower_bound
          << " txn_priority_highpri_upper_bound: " << txn_priority_highpri_upper_bound
          << " txn_priority_regular_lower_bound: " << txn_priority_regular_lower_bound
          << " txn_priority_regular_upper_bound: " << txn_priority_regular_upper_bound;
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
    bool read_only_op, YbcTxnPriorityRequirement txn_priority_requirement,
    IsLocalObjectLockOp is_local_object_lock_op) {
  if (yb_ddl_transaction_block_enabled ? IsDdlModeWithSeparateTransaction() : IsDdlMode()) {
    VLOG_TXN_STATE(2);
    priority_ = NewPriority(txn_priority_requirement);
    return Status::OK();
  }

  VLOG_TXN_STATE(2);
  if (!txn_in_progress_) {
    return RecreateTransaction(SavePriority::kFalse);
  }

  if (!read_only_op && !is_local_object_lock_op) {
    has_writes_ = true;
  }

  // Force use of a docdb distributed txn for YSQL read only transactions involving savepoints when
  // object locking feature is enabled (only for isolation != read committed case).
  //
  // TODO(table-locks): Need to explicitly handle READ_COMMITTED case since YSQL internally bumps up
  // subtxn id for every statement. Else, every RC read-only txn would burn a docdb txn.
  if (IsTableLockingEnabledForCurrentTxn() &&
      active_sub_transaction_id_ > kMinSubTransactionId &&
      pg_isolation_level_ != PgIsolationLevel::READ_COMMITTED) {
    read_only_op = false;
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
  // Users can use the deferrable mode via:
  // (1) DEFERRABLE READ ONLY setting in transaction blocks
  // (2) SET yb_read_after_commit_visibility = 'deferred';
  //
  // The feature doesn't take affect for non-read only serializable isolation txns
  // and fast-path transactions because they don't face read restart errors in the first place.
  //
  // (1) Serializable isolation txns don't face read restart errors because
  //    they use the latest timestamp for reading.
  // (2) Fast-path txns don't face read restart errors because
  //    they pick a read time after conflict resolution.
  // We already skip (2) because CalculateIsolation is not called for fast-path
  //    (i.e., NON_TRANSACTIONAL).
  need_defer_read_point_ =
      ((read_only_ && deferrable_)
        || yb_read_after_commit_visibility == YB_DEFERRED_READ_AFTER_COMMIT_VISIBILITY)
      && docdb_isolation != IsolationLevel::SERIALIZABLE_ISOLATION;

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
    return Status::OK();
  }
  auto skip_picking_isolation_level = read_only_op &&
      (docdb_isolation == IsolationLevel::SNAPSHOT_ISOLATION ||
       docdb_isolation == IsolationLevel::READ_COMMITTED);
  skip_picking_isolation_level |= is_local_object_lock_op;
  if (!skip_picking_isolation_level || (yb_ddl_transaction_block_enabled && IsDdlMode())) {
    if (IsDdlMode()) {
      DCHECK(yb_ddl_transaction_block_enabled)
          << "Unexpected DDL state found in plain transaction";
    }

    if (!use_saved_priority_ && !priority_.has_value()) {
      priority_ = NewPriority(txn_priority_requirement);
    }
    isolation_level_ = docdb_isolation;

    VLOG_TXN_STATE(2) << "effective isolation level: " << IsolationLevel_Name(docdb_isolation)
                      << " priority_: " << (priority_ ? std::to_string(*priority_) : "nullopt")
                      << "; transaction started successfully.";
  }
  // Else, preserve isolation_level_ as NON_TRANSACTIONAL.
  return Status::OK();
}

Status PgTxnManager::RestartTransaction() {
  need_restart_ = true;
  return Status::OK();
}

// Reset to a new read point. This corresponds to a new latest snapshot.
Status PgTxnManager::ResetTransactionReadPoint(bool is_catalog_snapshot) {
  VLOG_WITH_FUNC(4);
  if (is_catalog_snapshot) {
    // For a new catalog snapshot, create a new read time serial number but do not switch
    // to it yet. Switching to it will happen via PgSession::UpdateReadPointForCatalogOps()
    // when catalog operations are performed.
    //
    // NOTE: If YBCIsLegacyModeForCatalogOps() is true, we will not arrive here for
    // catalog snapshots.
    serial_no_.IncMaxReadTime();
  } else {
    // For transaction snapshots, create and switch to the new read time serial number.
    // Leaving it upto the caller would have worked too, but this is how it has been, so not
    // changing it now.
    serial_no_.IncReadTime();
  }

  if (pg_isolation_level_ != PgIsolationLevel::READ_COMMITTED) {
    return Status::OK();
  }
  read_time_manipulation_ = PREDICT_FALSE(FLAGS_ysql_rc_force_pick_read_time_on_pg_client)
      ? tserver::ReadTimeManipulation::ENSURE_READ_TIME_IS_SET
      : tserver::ReadTimeManipulation::NONE;
  return Status::OK();
}

Status PgTxnManager::EnsureReadPoint() {
  DCHECK(read_time_manipulation_ != tserver::ReadTimeManipulation::RESTART);
  read_time_manipulation_ = tserver::ReadTimeManipulation::ENSURE_READ_TIME_IS_SET;
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
  VLOG_WITH_FUNC(4) << "id: " << id << ", old id: " << active_sub_transaction_id_;
  active_sub_transaction_id_ = id;
}

Status PgTxnManager::CommitPlainTransaction(const std::optional<PgDdlCommitInfo>& ddl_commit_info) {
  return FinishPlainTransaction(Commit::kTrue, ddl_commit_info);
}

Status PgTxnManager::AbortPlainTransaction() {
  return FinishPlainTransaction(Commit::kFalse, std::nullopt /* ddl_commit_info */);
}

DdlMode PgTxnManager::GetDdlModeFromDdlState(
    const std::optional<DdlState> ddl_state,
    const std::optional<PgDdlCommitInfo>& ddl_commit_info) {
  return DdlMode{
      .has_docdb_schema_changes = ddl_state->has_docdb_schema_changes,
      .silently_altered_db = ddl_commit_info && ddl_commit_info->is_silent_altering
                                 ? std::optional(ddl_commit_info->db_oid)
                                 : std::nullopt,
      .use_regular_transaction_block = ddl_state->use_regular_transaction_block,
  };
}

Status PgTxnManager::FinishPlainTransaction(
    Commit commit, const std::optional<PgDdlCommitInfo>& ddl_commit_info) {
  if (PREDICT_FALSE(IsDdlMode() && IsDdlModeWithSeparateTransaction())) {
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

  const auto is_read_only = isolation_level_ == IsolationLevel::NON_TRANSACTIONAL;
  if (is_read_only && !IsTableLockingEnabledForCurrentTxn()) {
    VLOG_TXN_STATE(2) << "This was a read-only transaction, nothing to commit.";
    ResetTxnAndSession();
    return Status::OK();
  }

  // Aborting a transaction is on a best-effort basis. In the event that the abort RPC to the
  // tserver fails, we simply reset the transaction state here and return any error back to the
  // caller. In the event that the tserver recovers, it will eventually expire the transaction due
  // to inactivity.
  VLOG_TXN_STATE(2)
      << (commit ? "Committing" : "Aborting")
      << (is_read_only ? " read only" : "") << " transaction.";
  std::optional<DdlMode> ddl_mode = std::nullopt;
  // GH #22353 - We are only expected to reach inside the **if** condition, if the DDL, DML
  // transaction unification is enabled and we have a DDL statement within the transaction block.
  // Therefore, ideally here we should have a RSTATUS_DCHECK on
  // IsDdlModeWithRegularTransactionBlock() inside, instead of checking it in the if condition. We
  // do that only due to the linked bug GH #22353 where we can enter this function while executing
  // the ANALYZE command with DDL state set despite of using separate DDL transactions.
  if (ddl_state_ && IsDdlModeWithRegularTransactionBlock()) {
    ddl_mode.emplace(GetDdlModeFromDdlState(ddl_state_, ddl_commit_info));
  }
  VLOG_TXN_STATE(2) << "Sending FinishTransaction request with commit: " << commit << ", ddl_mode: "
                    << (ddl_mode ? ddl_mode->ToString() : "NULL");
  Status status = client_->FinishTransaction(commit, ddl_mode);
  VLOG_TXN_STATE(2) << "Transaction " << (commit ? "commit" : "abort") << " status: " << status;
  ResetTxnAndSession();
  return status;
}

void PgTxnManager::ResetTxnAndSession() {
  txn_in_progress_ = false;
  isolation_level_ = IsolationLevel::NON_TRANSACTIONAL;
  priority_ = std::nullopt;
  IncTxnSerialNo();

  follower_read_staleness_ms_.reset();
  read_only_ = false;
  has_writes_ = false;
  enable_tracing_ = false;
  crosstxn_snapshot_read_time_is_used_ = false;
  read_time_manipulation_ = tserver::ReadTimeManipulation::NONE;
  read_only_stmt_ = false;
  need_defer_read_point_ = false;
  clamp_uncertainty_window_ = false;

  // GH #22353 - Ideally the reset of the ddl_state_ should happen without the if condition, but
  // due to the linked bug GH #22353, we are resetting the DDL state only if DDL, DML transaction
  // unification is enabled and we have a DDL statement within the transaction block.
  // With transactional DDL disabled, we can enter this function while executing the ANALYZE command
  // with DDL state set. We don't want to clear out the DDL state in that case.
  // TODO(#26298): Add unit test with RC isolation level once supported since it can retry DDLs.
  if (IsDdlModeWithRegularTransactionBlock()) {
    ddl_state_.reset();
  }
}

Status PgTxnManager::SetDdlStateInPlainTransaction() {
  RSTATUS_DCHECK(!IsDdlModeWithSeparateTransaction(), IllegalState,
                 "SetDdlStateInPlainTransaction called when already in separate DDL Txn mode");

  VLOG_TXN_STATE(2);
  ddl_state_.emplace();
  ddl_state_->use_regular_transaction_block = true;
  VLOG_TXN_STATE(2);
  return Status::OK();
}

Status PgTxnManager::EnterSeparateDdlTxnMode() {
  RSTATUS_DCHECK(!IsDdlMode(), IllegalState,
                 "EnterSeparateDdlTxnMode called when already in DDL mode");
  VLOG_TXN_STATE(2);
  ddl_state_.emplace();
  ddl_state_->use_regular_transaction_block = false;
  VLOG_TXN_STATE(2);
  return Status::OK();
}

Status PgTxnManager::ExitSeparateDdlTxnModeWithAbort() {
  return ExitSeparateDdlTxnMode({});
}

Status PgTxnManager::ExitSeparateDdlTxnModeWithCommit(uint32_t db_oid, bool is_silent_altering) {
  return ExitSeparateDdlTxnMode(
    PgDdlCommitInfo{.db_oid = db_oid, .is_silent_altering = is_silent_altering});
}

Status PgTxnManager::ExitSeparateDdlTxnMode(const std::optional<PgDdlCommitInfo>& commit_info) {
  VLOG_TXN_STATE(2);
  if (!((yb_ddl_transaction_block_enabled && IsDdlModeWithSeparateTransaction()) ||
          (!yb_ddl_transaction_block_enabled && IsDdlMode()))) {
    RSTATUS_DCHECK(
        !commit_info, IllegalState,
        "Commit separate ddl txn called when not in a separate DDL transaction");
    return Status::OK();
  }

  const auto commit = commit_info ? Commit::kTrue : Commit::kFalse;
  const auto status = client_->FinishTransaction(
      commit,
      GetDdlModeFromDdlState(ddl_state_, commit_info));
  WARN_NOT_OK(status, Format("Failed to $0 DDL transaction", commit ? "commit" : "abort"));
  if (PREDICT_TRUE(status.ok() || !commit)) {
    // In case of an abort, reset the DDL mode here as we may later re-enter this function and retry
    // the abort as part of transaction error recovery if the status is not ok.
    ddl_state_.reset();
  }

  return status;
}

void PgTxnManager::DdlEnableForceCatalogModification() {
  if (PREDICT_FALSE(!ddl_state_)) {
    LOG(DFATAL) << "Unexpected call of " << __PRETTY_FUNCTION__ << " outside DDL";
    return;
  }

  ddl_state_->force_catalog_modification = true;
}

void PgTxnManager::SetDdlHasSyscatalogChanges() {
  if (PREDICT_FALSE(!ddl_state_)) {
    // There are only 2 cases where we may be performing DocDB schema changes outside of DDL mode:
    // 1. During initdb, when we do not use a transaction at all.
    // 2. When yb_non_ddl_txn_for_sys_tables_allowed is set. We would use a regular transaction.
    // has_docdb_schema_changes is mainly used for DDL atomicity, which is disabled for the PG
    // system catalog tables. Both cases above are primarily used for modifying the system catalog,
    // so there is no need to set this flag here.
    LOG_IF(
        DFATAL,
        !(YBCIsInitDbModeEnvVarSet() ||
          (IsTxnInProgress() && yb_non_ddl_txn_for_sys_tables_allowed)))
        << "Unexpected call of " << __PRETTY_FUNCTION__ << " outside DDL";
    return;
  }

  ddl_state_->has_docdb_schema_changes = true;
}

std::string PgTxnManager::TxnStateDebugStr() const {
  return YB_CLASS_TO_STRING(
      ddl_state,
      read_only,
      deferrable,
      txn_in_progress,
      serial_no,
      pg_isolation_level,
      isolation_level);
}

bool PgTxnManager::ShouldResetReadTime(std::optional<ReadTimeAction> read_time_action) const {
  return read_time_action && *read_time_action == ReadTimeAction::RESET;
}

bool PgTxnManager::ShouldClamp() const {
  return yb_read_after_commit_visibility == YB_RELAXED_READ_AFTER_COMMIT_VISIBILITY ||
         clamp_uncertainty_window_;
}

Status PgTxnManager::CheckConflictsAcrossReadTimeOptions(
    const tserver::PgPerformOptionsPB::ReadTimeOptionsPB& read_time_options,
    std::optional<ReadTimeAction> read_time_action,
    tserver::ReadTimeManipulation manipulation,
    NonTransactionalWrites ops_has_non_transactional_writes) const {
  const auto has_read_time = read_time_options.has_read_time();

  if (need_restart_ || manipulation == tserver::ReadTimeManipulation::RESTART) {
    RSTATUS_DCHECK(
        !IsSerializableIsolation(), IllegalState,
        "Serializable transactions do not face read restart errors.");
    RSTATUS_DCHECK(
        !ShouldResetReadTime(read_time_action) && !ops_has_non_transactional_writes, IllegalState,
        "Non transactional writes do not face read restart errors");
    RSTATUS_DCHECK(
        !ShouldClamp(), IllegalState, "Clamped reads do not face read restart errors.");
    RSTATUS_DCHECK(
        !need_defer_read_point_, IllegalState, "Deferred reads do not face read restart errors.");
    RSTATUS_DCHECK(
        !has_read_time, IllegalState,
        "Reads as of a time (e.g., backfill, yb_read_time) do not face read restart errors.");
    RSTATUS_DCHECK(
        !UsesFollowerReads(), IllegalState, "Follower reads do not face read restart errors.");
  }

  if (ShouldResetReadTime(read_time_action)) {
    RSTATUS_DCHECK(
        manipulation != tserver::ReadTimeManipulation::ENSURE_READ_TIME_IS_SET, IllegalState,
        "Reset picks the read time in the storage layer, incompatible with "
        "ENSURE_READ_TIME_IS_SET which picks read time in the query layer");
    RSTATUS_DCHECK(
        !has_read_time, IllegalState,
        "Non transactional writes must pick a read time in the storage layer.");
    RSTATUS_DCHECK(
        !UsesFollowerReads(), IllegalState,
        "Follower reads are read only and do not occur with non transactional writes.");
  }

  if (IsSerializableIsolation()) {
    RSTATUS_DCHECK(
        !ShouldResetReadTime(read_time_action), IllegalState,
        "Reset read time occurs under non-transactional isolation, not serializable.");
    RSTATUS_DCHECK(
        !has_read_time, IllegalState,
        "Serializable transaction picks the read time in the storage layer and"
        " hence cannot use a passed read time.");
    RSTATUS_DCHECK(
        manipulation != tserver::ReadTimeManipulation::ENSURE_READ_TIME_IS_SET, IllegalState,
        "Serializable transaction picks the read time in the storage layer and"
        " ENSURE_READ_TIME picks read time in the query layer.");
    RSTATUS_DCHECK(
        !UsesFollowerReads(), IllegalState,
        "Follower reads are read only transactions and serializable is not applicable.");
  }
  return Status::OK();
}

Status PgTxnManager::SetupReadTimeOptions(
    tserver::PgPerformOptionsPB::ReadTimeOptionsPB& read_time_options,
    std::optional<ReadTimeAction> read_time_action,
    NonTransactionalWrites ops_has_non_transactional_writes) {
  read_time_options.set_read_time_serial_no(serial_no_.read_time());
  read_time_options.set_read_time_serial_no_history_min(serial_no_.min_read_time());

  // read_time may be set by
  // - PgSession::SetReadTimeIfPresent.
  // - cross txn snapshot mechanism below.
  if (crosstxn_snapshot_read_time_is_used_) {
    RETURN_NOT_OK(CheckConflictWithCrossTxnSnapshotTime());
    if (auto it = crosstxn_explicit_snapshot_read_time_.find(serial_no_.read_time());
        it != crosstxn_explicit_snapshot_read_time_.end()) {
      // i.e., the "USE SNAPSHOT" option is set with the CREATE REPLICATION SLOT command
      SCHECK(
          !read_time_options.has_read_time(), NotSupported,
          "Cannot use a snapshot if a read time is already passed");
      ReadHybridTime::FromUint64(it->second).ToPB(read_time_options.mutable_read_time());
      return Status::OK();
    }
  }

  const auto manipulation =
      GetActualReadTimeManipulator(isolation_level_, read_time_manipulation_, read_time_action);
  if (!IsDdlModeWithSeparateTransaction()) {
    // The state in read_time_manipulation_ is only for kPlain transactions. And if YSQL switches to
    // kDdl mode for sometime, we should keep read_time_manipulation_ as is so that once YSQL
    // switches back to kDdl mode, the read_time_manipulation_ is not lost.
    read_time_manipulation_ = tserver::ReadTimeManipulation::NONE;
  }

  RETURN_NOT_OK(CheckConflictsAcrossReadTimeOptions(
      read_time_options, read_time_action, manipulation, ops_has_non_transactional_writes));

  if (ShouldResetReadTime(read_time_action)) {
    read_time_options.mutable_read_time()->Clear();
    return Status::OK();
  }

  if (IsSerializableIsolation() ||
      ops_has_non_transactional_writes ||
      read_time_options.has_read_time()) {
    return Status::OK(); // Other read time options below do not apply.
  }

  if (UsesFollowerReads()) {
    read_time_options.mutable_follower_read_staleness_ms()->set_value(
        static_cast<uint32_t>(*follower_read_staleness_ms_));
    return Status::OK();
  }

  if (need_restart_) {
    read_time_options.set_restart_transaction(true);
    need_restart_ = false;
    return Status::OK();
  }

  if (!IsDdlModeWithSeparateTransaction() &&
      manipulation == tserver::ReadTimeManipulation::RESTART) {
    read_time_options.set_read_time_manipulation(tserver::ReadTimeManipulation::RESTART);
    return Status::OK();
  }

  if (ShouldClamp()) {
    read_time_options.set_clamp_uncertainty_window(true);
    return Status::OK();
  }

  if (need_defer_read_point_) {
    // Two ways to defer read point:
    // 1. SET TRANSACTION READ ONLY DEFERRABLE
    // 2. SET yb_read_after_commit_visibility = 'deferred'
    read_time_options.set_defer_read_point(true);
    return Status::OK();
  }

  // In parallel execution (leader and workers), always pick read time on the proxy instead
  // of a remote tserver. This is required because the "used_read_time" mechanism
  // doesn't work with parallel rpcs. In other words, if some operation from Pg to the proxy
  // doesn't have a read time picked already and expects one to be picked on the remote tserver,
  // no simultaneous operation should be performed before the response for the rpc is received.
  //
  // For serializable isolation, there is no read time.
  if (!IsDdlModeWithSeparateTransaction() &&
      (manipulation == tserver::ReadTimeManipulation::ENSURE_READ_TIME_IS_SET ||
       (pg_callbacks_.IsInParallelMode() &&
        !yb_skip_ensure_read_time_in_parallel_execution && !UsesFollowerReads()))) {
    read_time_options.set_read_time_manipulation(
        tserver::ReadTimeManipulation::ENSURE_READ_TIME_IS_SET);
  }

  return Status::OK();
}

Status PgTxnManager::SetupPerformOptions(
    SetupPerformOptionsAccessorTag, tserver::PgPerformOptionsPB& options,
    NonTransactionalWrites ops_has_non_transactional_writes,
    std::optional<ReadTimeAction> read_time_action) {
  if (!IsDdlModeWithSeparateTransaction() && !txn_in_progress_) {
    IncTxnSerialNo();
  }
  options.set_isolation(isolation_level_);
  options.set_ddl_mode(IsDdlMode());
  options.set_ddl_use_regular_transaction_block(IsDdlModeWithRegularTransactionBlock());
  options.set_yb_non_ddl_txn_for_sys_tables_allowed(yb_non_ddl_txn_for_sys_tables_allowed);
  options.set_trace_requested(enable_tracing_);
  options.set_txn_serial_no(serial_no_.txn());
  options.set_active_sub_transaction_id(active_sub_transaction_id_);
  options.set_xcluster_target_ddl_bypass(yb_xcluster_target_ddl_bypass);
  options.set_pg_txn_start_us(pg_txn_start_us_);
  options.set_is_using_table_locks(using_table_locks_);
  options.set_read_from_followers(UsesFollowerReads());

  if (use_saved_priority_) {
    options.set_use_existing_priority(true);
  } else if (priority_) {
    options.set_priority(*priority_);
  }

  RETURN_NOT_OK(SetupReadTimeOptions(
      *options.mutable_read_time_options(), read_time_action, ops_has_non_transactional_writes));

  options.set_force_global_transaction(yb_force_global_transaction);
  options.set_force_tablespace_locality(yb_force_tablespace_locality);
  options.set_force_tablespace_locality_oid(yb_force_tablespace_locality_oid);
  return Status::OK();
}

double PgTxnManager::GetTransactionPriority() const {
  if (!priority_.has_value()) {
    return 0.0;
  }

  if (*priority_ <= yb::kRegularTxnUpperBound) {
    return ToTxnPriority(*priority_,
                         yb::kRegularTxnLowerBound,
                         yb::kRegularTxnUpperBound);
  }

  return ToTxnPriority(*priority_,
                       yb::kHighPriTxnLowerBound,
                       yb::kHighPriTxnUpperBound);
}

YbcTxnPriorityRequirement PgTxnManager::GetTransactionPriorityType() const {
  if (!priority_.has_value() || (*priority_ <= yb::kRegularTxnUpperBound)) {
    return kLowerPriorityRange;
  }
  if (*priority_ < yb::kHighPriTxnUpperBound) {
    return kHigherPriorityRange;
  }
  return kHighestPriority;
}

void PgTxnManager::IncTxnSerialNo() {
  serial_no_.IncTxn(
      is_read_time_history_cutoff_disabled_,
      pg_callbacks_.GetCatalogSnapshotReadPoint(
          0 /* table_oid*/, false /* create_if_not_exists */));
  active_sub_transaction_id_ = kMinSubTransactionId;
  crosstxn_explicit_snapshot_read_time_.clear();
}

void PgTxnManager::DumpSessionState(YbcPgSessionState* session_data) {
  VLOG(2) << "DumpSessionState: txn_serial_no=" << serial_no_.txn()
          << ", read_time_serial_no=" << serial_no_.read_time();
  session_data->txn_serial_no = serial_no_.txn();
  session_data->read_time_serial_no = serial_no_.read_time();
  session_data->active_sub_transaction_id = active_sub_transaction_id_;
  // TODO (#30932): Dump and Restore other necessary session state here.
}

void PgTxnManager::RestoreSessionState(const YbcPgSessionState& session_data) {
  VLOG(2) << "RestoreSessionState: txn_serial_no=" << session_data.txn_serial_no
          << ", read_time_serial_no=" << session_data.read_time_serial_no;
  serial_no_.Set(session_data.txn_serial_no, session_data.read_time_serial_no);
  active_sub_transaction_id_ = session_data.active_sub_transaction_id;
  VLOG_TXN_STATE(2);
}

YbcReadPointHandle PgTxnManager::GetCurrentReadPoint() const {
  return serial_no_.read_time();
}

YbcReadPointHandle PgTxnManager::GetMaxReadPoint() const { return serial_no_.max_read_time(); }

TxnReadPoint PgTxnManager::GetCurrentReadPointState() const {
  return TxnReadPoint{
      serial_no_.txn(), serial_no_.read_time(), clamp_uncertainty_window_,
      follower_read_staleness_ms_};
}

Status PgTxnManager::RestoreReadPoint(YbcReadPointHandle read_point) {
  if (VLOG_IS_ON(2) || yb_debug_log_snapshot_mgmt) {
    LOG(INFO) << "Setting read time serial_no to : " << read_point
              << ", current read time serial number: " << serial_no_.read_time()
              << " for txn serial number: " << serial_no_.txn();
  }
  return serial_no_.RestoreReadTime(read_point);
}

Status PgTxnManager::RestoreReadPoint(const TxnReadPoint& saved_read_point) {
  // Only restore if the current txn matches the saved
  if (serial_no_.txn() != saved_read_point.txn) {
    if (VLOG_IS_ON(2) || yb_debug_log_snapshot_mgmt) {
      LOG(INFO) << "Skipping RestoreReadPoint: saved txn " << saved_read_point.txn
                << " but current is " << serial_no_.txn();
    }
    return Status::OK();
  }
  clamp_uncertainty_window_ = saved_read_point.is_clamped;
  follower_read_staleness_ms_ = saved_read_point.follower_read_staleness_ms;
  return RestoreReadPoint(saved_read_point.read_time_serial_no);
}

Result<YbcReadPointHandle> PgTxnManager::RegisterSnapshotReadTime(
    uint64_t read_time, bool use_read_time) {
  RETURN_NOT_OK(CheckConflictWithCrossTxnSnapshotTime());
  const auto read_time_serial_no = serial_no_.read_time();
  auto [it, inserted] =
      crosstxn_explicit_snapshot_read_time_.emplace(read_time_serial_no, read_time);
  RSTATUS_DCHECK(
      inserted, IllegalState, "Current read point already has assigned snapshot read time");
  if (use_read_time) {
    crosstxn_snapshot_read_time_is_used_ = true;
  }
  return it->first;
}

Result<std::string> PgTxnManager::ExportSnapshot(
    SetupPerformOptionsAccessorTag tag, const YbcPgTxnSnapshot& snapshot,
    std::optional<YbcReadPointHandle> explicit_read_time) {
  RETURN_NOT_OK(CheckConflictWithCrossTxnSnapshotTime());
  tserver::PgExportTxnSnapshotRequestPB req;
  auto& snapshot_pb = *req.mutable_snapshot();
  snapshot_pb.set_db_oid(snapshot.db_id);
  snapshot_pb.set_isolation_level(snapshot.iso_level);
  snapshot_pb.set_read_only(snapshot.read_only);
  std::optional<uint64_t> crosstxn_explicit_read_time_value;
  std::optional read_time_action{ReadTimeAction::ENSURE_IS_SET};
  if (explicit_read_time.has_value()) {
    const auto i = crosstxn_explicit_snapshot_read_time_.find(*explicit_read_time);
    RSTATUS_DCHECK(
        i != crosstxn_explicit_snapshot_read_time_.end(), IllegalState, "Bad read time handle");
    crosstxn_explicit_read_time_value = i->second;
    read_time_action.reset();
  }
  auto& options = *req.mutable_options();
  RETURN_NOT_OK(
      SetupPerformOptions(tag, options, NonTransactionalWrites::kFalse, read_time_action));

  if (crosstxn_explicit_read_time_value) {
    ReadHybridTime::FromUint64(*crosstxn_explicit_read_time_value).ToPB(
        options.mutable_read_time_options()->mutable_read_time());
  }
  auto res = client_->ExportTxnSnapshot(&req);
  if (res.ok()) {
    has_exported_snapshots_ = true;
  }
  return res;
}

Result<YbcPgTxnSnapshot> PgTxnManager::ImportSnapshot(
    SetupPerformOptionsAccessorTag tag, std::string_view snapshot_id) {
  RETURN_NOT_OK(CheckConflictWithCrossTxnSnapshotTime());
  tserver::PgPerformOptionsPB options;
  RETURN_NOT_OK(SetupPerformOptions(tag, options, NonTransactionalWrites::kFalse));
  const auto snapshot = VERIFY_RESULT(client_->ImportTxnSnapshot(snapshot_id, std::move(options)));
  crosstxn_snapshot_read_time_is_used_ = true;

  return YbcPgTxnSnapshot{
      .db_id = snapshot.db_oid(),
      .iso_level = implicit_cast<int>(snapshot.isolation_level()),
      .read_only = snapshot.read_only()};
}

Status PgTxnManager::CheckConflictWithCrossTxnSnapshotTime() const {
  SCHECK(
      !UsesFollowerReads(), NotSupported,
      "Cannot set both 'transaction snapshot' and 'yb_read_from_followers' in the same "
      "transaction.");
  SCHECK_EQ(
      yb_read_time, 0, NotSupported,
      "Cannot set both 'transaction snapshot' and 'yb_read_time' in the same transaction.");
  SCHECK_EQ(
      yb_read_after_commit_visibility, YB_STRICT_READ_AFTER_COMMIT_VISIBILITY, NotSupported,
      "Cannot set both 'transaction snapshot' and 'yb_read_after_commit_visibility' in the same "
      "transaction.");
  SCHECK(!IsDdlMode(), NotSupported, "Cannot run DDL with exported/imported snapshot.");
  SCHECK(
      !IsSerializableIsolation(), NotSupported,
      "Serializable transaction picks its own snapshot in the storage layer.");
  SCHECK(
      !deferrable_,
      NotSupported, "Deferred read point can't be used with exported/imported snapshot.");
  return Status::OK();
}

void PgTxnManager::ClearExportedTxnSnapshots() {
  if (!has_exported_snapshots_) {
    return;
  }
  has_exported_snapshots_ = false;
  const auto s = client_->ClearExportedTxnSnapshots();
  LOG_IF(DFATAL, !s.ok()) << "Faced error while deleting exported snapshots. Error Details: " << s;
}

Status PgTxnManager::RollbackToSubTransaction(
    SetupPerformOptionsAccessorTag tag, SubTransactionId id) {
  if (!txn_in_progress_) {
    VLOG_TXN_STATE(2) << "No transaction in progress, nothing to rollback.";
    return Status::OK();
  }
  if (isolation_level_ == IsolationLevel::NON_TRANSACTIONAL &&
      !IsTableLockingEnabledForCurrentTxn()) {
    VLOG(4) << "This isn't a distributed transaction, so nothing to rollback.";
    return Status::OK();
  }
  tserver::PgPerformOptionsPB options;
  RETURN_NOT_OK(SetupPerformOptions(tag, options, NonTransactionalWrites::kFalse));
  return client_->RollbackToSubTransaction(id, &options);
}

bool PgTxnManager::TryAcquireObjectLock(
    const YbcObjectLockId& lock_id, docdb::ObjectLockFastpathLockType lock_type) {
  // It is safe to use fast path locking only for statements that would eventually be associated
  // with kPlain type at PgClientSession, since fast path locking always assigns locks under the
  // plain transaction.
  if (IsDdlMode() && !IsDdlModeWithRegularTransactionBlock()) {
    return false;
  }

  if (!client_->TryAcquireObjectLockInSharedMemory(
        active_sub_transaction_id_, lock_id, lock_type)) {
    return false;
  }
  DEBUG_ONLY(DEBUG_UpdateLastObjectLockingInfo());
  return true;
}

Status PgTxnManager::AcquireObjectLock(
    SetupPerformOptionsAccessorTag tag,
    const YbcObjectLockId& lock_id, YbcObjectLockMode mode,
    bool is_session_lock,
    std::optional<PgTablespaceOid> tablespace_oid) {
  RETURN_NOT_OK(CalculateIsolation(
      false /* read_only, doesn't matter */,
      GetTxnPriorityRequirement(RowMarkType::ROW_MARK_ABSENT),
      IsLocalObjectLockOp(mode <= YbcObjectLockMode::YB_OBJECT_ROW_EXCLUSIVE_LOCK)));
  tserver::PgPerformOptionsPB options;
  RETURN_NOT_OK(SetupPerformOptions(tag, options, NonTransactionalWrites::kFalse));
  RETURN_NOT_OK(client_->AcquireObjectLock(
      &options, lock_id, mode, is_session_lock, tablespace_oid));
  DEBUG_ONLY(DEBUG_UpdateLastObjectLockingInfo());
  return Status::OK();
}

void PgTxnManager::SetTransactionHasWrites() {
  has_writes_ = true;
}

Result<bool> PgTxnManager::TransactionHasNonTransactionalWrites() const {
  RSTATUS_DCHECK(
      txn_in_progress_, IllegalState,
      "Transaction is not in progress, cannot check fast-path writes");
  return has_writes_ && isolation_level_ == NON_TRANSACTIONAL;
}

YbcTxnPriorityRequirement PgTxnManager::GetTxnPriorityRequirement(RowMarkType row_mark_type) const {
  if (IsDdlMode()) {
    // DDLs acquire object locks to serialize conflicting concurrent DDLs. Concurrent DDLs that
    // don't conflict can make progress without blocking each other.
    //
    // However, if object locks are disabled, concurrent DDLs are disallowed for safety.
    // This is done by relying on conflicting increments to the catalog version (most DDLs do this
    // except some like CREATE TABLE). Note that global DDLs (those that affect catalog tables
    // shared across databases) conflict with all other DDLs since they increment all per-db
    // catalog versions.
    //
    // We want ANALYZE DDLs spawned by auto-analyze to be pre-empted in case of such concurrent
    // DDL conflicts. To achieve this, all regular DDL take a FOR KEY SHARE lock on the catalog
    // version row with a high priority and ANALZYE spawned by auto-analyze takes a
    // FOR UPDATE exclusive lock with a lower priority. Given DDLs run with fail-on-conflict
    // concurrency control, these priorities achieve the goal.
    //
    // With object level locking, priorities are meaningless since DDLs don't rely on DocDB's
    // conflict resolution for concurrent DDLs.
    if (!yb_use_internal_auto_analyze_service_conn)
      return kHighestPriority;
    else
      return kHigherPriorityRange;
  }
  if (GetPgIsolationLevel() == PgIsolationLevel::READ_COMMITTED) {
    return kHighestPriority;
  }
  return RowMarkNeedsHigherPriority(row_mark_type) ? kHigherPriorityRange : kLowerPriorityRange;
}

}  // namespace yb::pggate
