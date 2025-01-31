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

// No include guards here because this file is expected to be included multiple times.

#pragma once

#include <mutex>
#include <optional>
#include <utility>

#include "yb/common/clock.h"
#include "yb/common/transaction.h"
#include "yb/gutil/ref_counted.h"

#include "yb/tserver/pg_client.fwd.h"
#include "yb/tserver/pg_client.pb.h"
#include "yb/tserver/tserver_fwd.h"

#include "yb/util/enums.h"

#include "yb/yql/pggate/pg_client.h"
#include "yb/yql/pggate/pg_gate_fwd.h"
#include "yb/yql/pggate/pg_callbacks.h"

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

YB_STRONGLY_TYPED_BOOL(EnsureReadTimeIsSet);

class PgTxnManager : public RefCountedThreadSafe<PgTxnManager> {
 public:
  PgTxnManager(PgClient* pg_client, scoped_refptr<ClockBase> clock, YbcPgCallbacks pg_callbacks);

  virtual ~PgTxnManager();

  Status BeginTransaction(int64_t start_time);

  Status CalculateIsolation(bool read_only_op, YbcTxnPriorityRequirement txn_priority_requirement);
  Status RecreateTransaction();
  Status RestartTransaction();
  Status ResetTransactionReadPoint();
  Status EnsureReadPoint();
  Status RestartReadPoint();
  bool IsRestartReadPointRequested();
  void SetActiveSubTransactionId(SubTransactionId id);
  Status CommitPlainTransaction();
  Status AbortPlainTransaction();
  Status SetPgIsolationLevel(int isolation);
  PgIsolationLevel GetPgIsolationLevel();
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

  bool IsTxnInProgress() const { return txn_in_progress_; }
  IsolationLevel GetIsolationLevel() const { return isolation_level_; }
  bool IsDdlMode() const { return ddl_state_.has_value(); }
  std::optional<bool> GetDdlForceCatalogModification() const {
    return ddl_state_ ? std::optional(ddl_state_->force_catalog_modification) : std::nullopt;
  }
  bool ShouldEnableTracing() const { return enable_tracing_; }

  Status SetupPerformOptions(
      tserver::PgPerformOptionsPB* options,
      EnsureReadTimeIsSet ensure_read_time = EnsureReadTimeIsSet::kFalse);

  double GetTransactionPriority() const;
  YbcTxnPriorityRequirement GetTransactionPriorityType() const;

  void DumpSessionState(YbcPgSessionState* session_data);
  void RestoreSessionState(const YbcPgSessionState& session_data);

  [[nodiscard]] uint64_t GetCurrentReadTimePoint() const;
  Status RestoreReadTimePoint(uint64_t read_time_point_handle);
  Result<std::string> ExportSnapshot(const YbcPgTxnSnapshot& snapshot);
  Result<YbcPgTxnSnapshot> ImportSnapshot(std::string_view snapshot_id);
  bool HasExportedSnapshots() const;
  void ClearExportedTxnSnapshots();

  struct DdlState {
    bool has_docdb_schema_changes = false;
    bool force_catalog_modification = false;

    std::string ToString() const {
      return YB_STRUCT_TO_STRING(has_docdb_schema_changes, force_catalog_modification);
    }
  };

 private:
  struct DdlCommitInfo {
    uint32_t db_oid;
    bool is_silent_altering;
  };

  class SerialNo {
   public:
    SerialNo();
    SerialNo(uint64_t txn_serial_no, uint64_t read_time_serial_no);
    void IncTxn();
    void IncReadTime();
    Status RestoreReadTime(uint64_t read_time_serial_no);
    [[nodiscard]] uint64_t txn() const { return txn_; }
    [[nodiscard]] uint64_t read_time() const { return read_time_; }

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

  Status FinishPlainTransaction(Commit commit);

  void IncTxnSerialNo();

  Status ExitSeparateDdlTxnMode(const std::optional<DdlCommitInfo>& commit_info);

  Status CheckSnapshotTimeConflict() const;
  Status CheckTxnSnapshotOptions(const tserver::PgPerformOptionsPB& options) const;

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
  uint64_t priority_ = 0;
  SavePriority use_saved_priority_ = SavePriority::kFalse;
  int64_t pg_txn_start_us_ = 0;
  bool snapshot_read_time_is_set_ = false;
  bool has_exported_snapshots_ = false;

  YbcPgCallbacks pg_callbacks_;

  DISALLOW_COPY_AND_ASSIGN(PgTxnManager);
};

}  // namespace yb::pggate
