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

// No include guards here because this file is expected to be included multiple times.

#pragma once

#include <mutex>
#include <memory>
#include <optional>
#include <utility>

#include "yb/common/clock.h"
#include "yb/common/transaction.h"

#include "yb/docdb/object_lock_shared_fwd.h"

#include "yb/gutil/ref_counted.h"

#include "yb/tserver/pg_client.fwd.h"
#include "yb/tserver/pg_client.pb.h"
#include "yb/tserver/tserver_fwd.h"

#include "yb/util/enums.h"
#include "yb/util/scope_exit.h"

#include "yb/yql/pggate/pg_client.h"
#include "yb/yql/pggate/pg_callbacks.h"
#include "yb/yql/pggate/pg_gate_fwd.h"
#include "yb/yql/pggate/pg_setup_perform_options_accessor_tag.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb::pggate {

// These should match XACT_READ_UNCOMMITED, XACT_READ_COMMITED, XACT_REPEATABLE_READ,
// XACT_SERIALIZABLE from xact.h. Please do not change this enum.
YB_DEFINE_ENUM(
  PgIsolationLevel,
  ((READ_UNCOMMITED, 0))
  ((READ_COMMITTED, 1))
  ((REPEATABLE_READ, 2))
  ((SERIALIZABLE, 3))
);

YB_DEFINE_ENUM(ReadTimeAction, (ENSURE_IS_SET)(RESET));
YB_STRONGLY_TYPED_BOOL(IsLocalObjectLockOp);

class PgTxnManager : public RefCountedThreadSafe<PgTxnManager> {
 public:
  PgTxnManager(
      PgClient* pg_client, scoped_refptr<ClockBase> clock, YbcPgCallbacks pg_callbacks,
      bool enable_table_locking);

  ~PgTxnManager();

  Status BeginTransaction(int64_t start_time);

  Status CalculateIsolation(
      bool read_only_op, YbcTxnPriorityRequirement txn_priority_requirement,
      IsLocalObjectLockOp is_local_object_lock_op = IsLocalObjectLockOp::kFalse);
  Status RecreateTransaction();
  Status RestartTransaction();
  Status ResetTransactionReadPoint(bool is_catalog_snapshot);
  Status EnsureReadPoint();
  Status RestartReadPoint();
  bool IsRestartReadPointRequested();
  void SetActiveSubTransactionId(SubTransactionId id);
  Status SetDdlStateInPlainTransaction();
  Status CommitPlainTransaction(const std::optional<PgDdlCommitInfo>& ddl_commit_info);
  Status AbortPlainTransaction();
  Status SetPgIsolationLevel(int isolation);
  PgIsolationLevel GetPgIsolationLevel() const;
  Status SetReadOnly(bool read_only);
  Status SetEnableTracing(bool tracing);
  Status UpdateFollowerReadsConfig(bool enable_follower_reads, int32_t staleness);
  Status SetDeferrable(bool deferrable);
  Status EnterSeparateDdlTxnMode();
  Status ExitSeparateDdlTxnModeWithAbort();
  Status ExitSeparateDdlTxnModeWithCommit(uint32_t db_oid, bool is_silent_altering);
  void DdlEnableForceCatalogModification();
  void SetDdlHasSyscatalogChanges();
  Status SetInTxnBlock(bool in_txn_blk);
  Status SetReadOnlyStmt(bool read_only_stmt);
  void SetTransactionHasWrites();
  Result<bool> TransactionHasNonTransactionalWrites() const;

  bool IsTxnInProgress() const { return txn_in_progress_; }
  IsolationLevel GetIsolationLevel() const { return isolation_level_; }
  bool IsDdlMode() const { return ddl_state_.has_value(); }
  bool IsDdlModeWithRegularTransactionBlock() const {
    return ddl_state_.has_value() && ddl_state_->use_regular_transaction_block;
  }
  bool IsDdlModeWithSeparateTransaction() const {
    return ddl_state_.has_value() && !ddl_state_->use_regular_transaction_block;
  }
  std::optional<bool> GetDdlForceCatalogModification() const {
    return ddl_state_ ? std::optional(ddl_state_->force_catalog_modification) : std::nullopt;
  }
  bool ShouldEnableTracing() const { return enable_tracing_; }

  Status SetupPerformOptions(SetupPerformOptionsAccessorTag tag,
      tserver::PgPerformOptionsPB* options, std::optional<ReadTimeAction> read_time_action = {});

  double GetTransactionPriority() const;
  YbcTxnPriorityRequirement GetTransactionPriorityType() const;

  void DumpSessionState(YbcPgSessionState* session_data);
  void RestoreSessionState(const YbcPgSessionState& session_data);

  [[nodiscard]] YbcReadPointHandle GetCurrentReadPoint() const;
  [[nodiscard]] YbcReadPointHandle GetMaxReadPoint() const;
  Status RestoreReadPoint(YbcReadPointHandle read_point);
  Result<YbcReadPointHandle> RegisterSnapshotReadTime(uint64_t read_time, bool use_read_time);

  Result<std::string> ExportSnapshot(
      SetupPerformOptionsAccessorTag tag, const YbcPgTxnSnapshot& snapshot,
      std::optional<YbcReadPointHandle> explicit_read_time);
  Result<YbcPgTxnSnapshot> ImportSnapshot(
      SetupPerformOptionsAccessorTag tag, std::string_view snapshot_id);
  [[nodiscard]] bool has_exported_snapshots() const { return has_exported_snapshots_; }
  void ClearExportedTxnSnapshots();
  Status RollbackToSubTransaction(SetupPerformOptionsAccessorTag tag, SubTransactionId id);
  [[nodiscard]] bool TryAcquireObjectLock(
      const YbcObjectLockId& lock_id, docdb::ObjectLockFastpathLockType lock_type);

  Status AcquireObjectLock(
      SetupPerformOptionsAccessorTag tag, const YbcObjectLockId& lock_id, YbcObjectLockMode mode);
  struct DdlState {
    bool has_docdb_schema_changes = false;
    bool force_catalog_modification = false;
    bool use_regular_transaction_block = false;

    std::string ToString() const {
      return YB_STRUCT_TO_STRING(
          has_docdb_schema_changes, force_catalog_modification, use_regular_transaction_block);
    }
  };

  YbcTxnPriorityRequirement GetTxnPriorityRequirement(RowMarkType row_mark_type) const;

  auto TemporaryDisableReadTimeHistoryCutoff() {
    const auto original_value = is_read_time_history_cutoff_disabled_;
    is_read_time_history_cutoff_disabled_ = true;
    return ScopeExit(
        [this, original_value] { is_read_time_history_cutoff_disabled_ = original_value; });
  }

  bool IsTableLockingEnabledForCurrentTxn() const;
  bool ShouldEnableTableLocking() const;

 private:
  class SerialNo {
   public:
    SerialNo();
    SerialNo(uint64_t txn_serial_no, uint64_t read_time_serial_no);
    void IncTxn(bool preserve_read_time_history, YbcReadPointHandle catalog_read_time_serial_no);
    void IncReadTime();
    void IncMaxReadTime();
    Status RestoreReadTime(uint64_t read_time_serial_no);
    [[nodiscard]] uint64_t txn() const { return txn_; }
    [[nodiscard]] uint64_t read_time() const { return read_time_; }
    [[nodiscard]] uint64_t min_read_time() const { return min_read_time_; }
    [[nodiscard]] uint64_t max_read_time() const { return max_read_time_; }
    std::string ToString() const {
      return YB_CLASS_TO_STRING(txn, read_time, min_read_time, max_read_time);
    }

   private:
    uint64_t txn_;
    uint64_t read_time_;
    // Txn may have multiple valid read time values (i.e. multiple read times inside
    // same transaction). The [min_read_time_, max_read_time_] segment describes the set of
    // valid values for read_time_ for current txn_. The RestoreReadTime method checks that
    // read_time_serial_no has a valid value in context of current txn_.
    uint64_t min_read_time_;
    uint64_t max_read_time_;
  };

  YB_STRONGLY_TYPED_BOOL(NeedsHigherPriorityTxn);
  YB_STRONGLY_TYPED_BOOL(SavePriority);

  void ResetTxnAndSession();
  void StartNewSession();
  Status UpdateReadTimeForFollowerReadsIfRequired();
  Status RecreateTransaction(SavePriority save_priority);

  static uint64_t NewPriority(YbcTxnPriorityRequirement txn_priority_requirement);

  std::string TxnStateDebugStr() const;

  DdlMode GetDdlModeFromDdlState(
    const std::optional<DdlState> ddl_state, const std::optional<PgDdlCommitInfo>& ddl_commit_info);

  Status FinishPlainTransaction(
      Commit commit, const std::optional<PgDdlCommitInfo>& ddl_commit_info);

  void IncTxnSerialNo();

  Status ExitSeparateDdlTxnMode(const std::optional<PgDdlCommitInfo>& commit_info);

  Status CheckSnapshotTimeConflict() const;

  // ----------------------------------------------------------------------------------------------

  PgClient* client_;
  scoped_refptr<ClockBase> clock_;

  bool txn_in_progress_ = false;
  IsolationLevel isolation_level_ = IsolationLevel::NON_TRANSACTIONAL;
  SerialNo serial_no_;
  SubTransactionId active_sub_transaction_id_ = kMinSubTransactionId;
  bool need_restart_ = false;
  bool need_defer_read_point_ = false;
  tserver::ReadTimeManipulation read_time_manipulation_ = tserver::ReadTimeManipulation::NONE;
  bool in_txn_blk_ = false;
  bool read_only_stmt_ = false;

  // Postgres transaction characteristics.
  PgIsolationLevel pg_isolation_level_ = PgIsolationLevel::REPEATABLE_READ;
  bool read_only_ = false;
  bool enable_tracing_ = false;
  bool enable_follower_reads_ = false;
  uint64_t follower_read_staleness_ms_ = 0;
  HybridTime read_time_for_follower_reads_;
  bool deferrable_ = false;

  std::optional<DdlState> ddl_state_;

  // On a transaction conflict error we want to recreate the transaction with the same priority as
  // the last transaction. This avoids the case where the current transaction gets a higher priority
  // and cancels the other transaction.
  std::optional<uint64_t> priority_;
  SavePriority use_saved_priority_ = SavePriority::kFalse;
  int64_t pg_txn_start_us_ = 0;
  bool using_table_locks_ = false;
  bool snapshot_read_time_is_used_ = false;
  bool has_exported_snapshots_ = false;

  YbcPgCallbacks pg_callbacks_;
  // The transaction manager tracks the following semantics:
  // 1. read_only_: Is the current transaction marked as read-only? A read-only transaction is one
  //                that does not write to non temporary tables. This is a postgres construct that
  //                is determined by the GUCs "default_transaction_read_only" and
  //                "transaction_read_only". Note that a transaction can be marked as read-only
  //                after it has already performed some writes.
  // 2. read_only_stmt_: Does the current statement write to non temporary tables? Relevant in the
  //                     context of read-only transactions, where all statements must be read-only.
  // 3. has_writes_: Has the current transaction performed any writes to non temporary tables?
  //                 This is used to track whether the transaction writes use the "fast path".
  //                 Note that a transaction can be marked as non-read-only and not have any writes.
  //                 The reverse is also true: a transaction can be marked as read-only and still
  //                 have writes (before it was marked as read-only). So, no conclusion can be drawn
  //                 about the transaction's read-only status based on the has_writes_ flag.
  //                 This flag does not include writes made for object locking.
  bool has_writes_ = false;

  const bool enable_table_locking_;

  std::unordered_map<YbcReadPointHandle, uint64_t> explicit_snapshot_read_time_;
  bool is_read_time_history_cutoff_disabled_{false};

#ifndef NDEBUG
 public:
  void DEBUG_CheckOptionsForPerform(const tserver::PgPerformOptionsPB& options) const;
 private:
  void DEBUG_UpdateLastObjectLockingInfo();
  uint64_t debug_last_object_locking_txn_serial_ = 0;
#endif

  DISALLOW_COPY_AND_ASSIGN(PgTxnManager);
};

}  // namespace yb::pggate
