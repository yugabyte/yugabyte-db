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

#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "yb/client/client_fwd.h"
#include "yb/client/tablet_server.h"

#include "yb/common/pg_types.h"
#include "yb/common/transaction.h"

#include "yb/gutil/ref_counted.h"

#include "yb/util/debug-util.h"
#include "yb/util/result.h"

#include "yb/yql/pggate/insert_on_conflict_buffer.h"
#include "yb/yql/pggate/pg_client.h"
#include "yb/yql/pggate/pg_doc_metrics.h"
#include "yb/yql/pggate/pg_flush_future.h"
#include "yb/yql/pggate/pg_gate_fwd.h"
#include "yb/yql/pggate/pg_operation_buffer.h"
#include "yb/yql/pggate/pg_perform_future.h"
#include "yb/yql/pggate/pg_session_fwd.h"
#include "yb/yql/pggate/pg_setup_perform_options_accessor_tag.h"
#include "yb/yql/pggate/pg_tabledesc.h"
#include "yb/yql/pggate/pg_tools.h"
#include "yb/yql/pggate/pg_txn_manager.h"

namespace yb::pggate {

class PgFlushDebugContext;

struct PgSessionRunOptions {
  HybridTime in_txn_limit{};
  ForceNonBufferable force_non_bufferable{ForceNonBufferable::kFalse};
  std::optional<PgSessionRunOperationMarker> marker{};

  friend bool operator==(const PgSessionRunOptions&, const PgSessionRunOptions&) = default;

  std::string ToString() const {
      return YB_STRUCT_TO_STRING(in_txn_limit, force_non_bufferable, marker);
  }
};

// This class is not thread-safe as it is mostly used by a single-threaded PostgreSQL backend
// process.
class PgSession final : public RefCountedThreadSafe<PgSession> {
 public:
  // Public types.
  using ScopedRefPtr = PgSessionPtr;
  using RunRWOperationsHook = std::function<Status(std::optional<PgSessionRunOperationMarker>)>;

  // Constructors.
  PgSession(
      PgClient& pg_client,
      scoped_refptr<PgTxnManager> pg_txn_manager,
      const YbcPgCallbacks& pg_callbacks,
      YbcPgExecStatsState& stats_state,
      bool is_pg_binary_upgrade,
      std::reference_wrapper<const WaitEventWatcher> wait_event_watcher,
      BufferingSettings& buffering_settings,
      RunRWOperationsHook&& hook);
  ~PgSession();

  // Resets the read point for catalog tables.
  // Next catalog read operation will read the very latest catalog's state.
  void ResetCatalogReadPoint();
  [[nodiscard]] const ReadHybridTime& catalog_read_time() const { return catalog_read_time_; }

  //------------------------------------------------------------------------------------------------
  // Operations on Session.
  //------------------------------------------------------------------------------------------------

  Status IsDatabaseColocated(const PgOid database_oid, bool *colocated,
                             bool *legacy_colocated_database);

  //------------------------------------------------------------------------------------------------
  // Operations on Tablegroup.
  //------------------------------------------------------------------------------------------------

  // API for schema operations.
  // TODO(neil) Schema should be a sub-database that have some specialized property.
  Status CreateSchema(const std::string& schema_name, bool if_not_exist);
  Status DropSchema(const std::string& schema_name, bool if_exist);

  // API for table operations.
  Status DropTable(
      const PgObjectId& table_id, bool use_regular_transaction_block, CoarseTimePoint deadline);
  Status DropIndex(
      const PgObjectId& index_id, bool use_regular_transaction_block,
      client::YBTableName* indexed_table_name, CoarseTimePoint deadline);
  Result<PgTableDescPtr> LoadTable(const PgObjectId& table_id);
  void InvalidateTableCache(
      const PgObjectId& table_id, InvalidateOnPgClient invalidate_on_pg_client);

  // Start operation buffering. Buffering must not be in progress.
  Status StartOperationsBuffering();
  // Flush all pending buffered operation and stop further buffering.
  // Buffering must be in progress.
  Status StopOperationsBuffering();
  // Drop all pending buffered operations and stop further buffering. Buffering may be in any state.
  void ResetOperationsBuffering();
  // Adjust buffer batch size.
  Status AdjustOperationsBuffering(int multiple = 1);

  // Flush all pending buffered operations. Buffering mode remain unchanged.
  Result<SetupPerformOptionsAccessorTag> FlushBufferedOperations(
      const PgFlushDebugContext& dbg_ctx);
  // Drop all pending buffered operations. Buffering mode remain unchanged.
  SetupPerformOptionsAccessorTag DropBufferedOperations();

  PgIsolationLevel GetIsolationLevel();

  bool IsHashBatchingEnabled();

  // Run (apply + flush) list of given operations to read and write database content.
  template<class OpPtr>
  struct TableOperation {
    const OpPtr* operation = nullptr;
    const PgTableDesc* table = nullptr;

    bool IsEmpty() const {
      return *this == TableOperation();
    }

    friend bool operator==(const TableOperation&, const TableOperation&) = default;
  };

  using OperationGenerator = LWFunction<TableOperation<PgsqlOpPtr>()>;
  using ReadOperationGenerator = LWFunction<TableOperation<PgsqlReadOpPtr>()>;
  using RunOptions = PgSessionRunOptions;

  struct CacheOptions {
    uint32_t key_group;
    std::string key_value;
    std::optional<uint32_t> lifetime_threshold_ms;
  };

  Result<PerformFuture> RunAsync(
      std::span<const PgsqlOpPtr> ops, const PgTableDesc& table, const RunOptions& options = {});

  Result<PerformFuture> RunAsync(
      const OperationGenerator& generator, const RunOptions& options = {});

  Result<PerformFuture> RunAsync(
      const ReadOperationGenerator& generator, std::optional<CacheOptions>&& cache_options = {});

  std::string GenerateNewYbrowid();

  void InvalidateAllTablesCache(uint64_t min_ysql_catalog_version);
  void UpdateTableCacheMinVersion(uint64_t min_ysql_catalog_version);

  InsertOnConflictBuffer& GetInsertOnConflictBuffer(void* plan);
  InsertOnConflictBuffer& GetInsertOnConflictBuffer();
  void ClearAllInsertOnConflictBuffers();
  void ClearInsertOnConflictBuffer(void* plan);
  bool IsInsertOnConflictBufferEmpty() const;

  // Sets the specified timeout in the rpc service.
  void SetTimeout(int timeout_ms);

  void SetLockTimeout(int lock_timeout_ms);

  Status ValidatePlacements(
      const std::string& live_placement_info,
      const std::string& read_replica_placement_info,
      bool check_satisfiable);

  void TrySetCatalogReadPoint(const ReadHybridTime& read_ht);

  PgClient& pg_client() const { return pg_client_; }

  Status SetupPerformOptionsForDdl(tserver::PgPerformOptionsPB* options);

  void SetTransactionHasWrites();
  Result<bool> CurrentTransactionUsesFastPath() const;

  void ResetHasCatalogWriteOperationsInDdlMode();
  bool HasCatalogWriteOperationsInDdlMode() const;

  void SetDdlHasSyscatalogChanges();

  PgDocMetrics& metrics() { return metrics_; }

  [[nodiscard]] PgWaitEventWatcher StartWaitEvent(ash::WaitStateCode wait_event);

  Status AcquireAdvisoryLock(
      const YbcAdvisoryLockId& lock_id, YbcAdvisoryLockMode mode, bool wait, bool session);
  Status ReleaseAdvisoryLock(const YbcAdvisoryLockId& lock_id, YbcAdvisoryLockMode mode);
  Status ReleaseAllAdvisoryLocks(uint32_t db_oid);

  Status AcquireObjectLock(const YbcObjectLockId& lock_id, YbcObjectLockMode mode);

  YbcReadPointHandle GetCurrentReadPoint() const {
    return pg_txn_manager_->GetCurrentReadPoint();
  }

  TxnReadPoint GetCurrentReadPointState() const {
    return pg_txn_manager_->GetCurrentReadPointState();
  }

  Status RestoreReadPoint(YbcReadPointHandle read_point) {
    return pg_txn_manager_->RestoreReadPoint(read_point);
  }

  // Restores the read point to saved_read_point.read_time, but only if the current
  // txn matches saved_read_point.txn. If txn doesn't match, no restore is performed.
  Status RestoreReadPoint(const TxnReadPoint& saved_read_point) {
    return pg_txn_manager_->RestoreReadPoint(saved_read_point);
  }

  YbcReadPointHandle GetCatalogSnapshotReadPoint(YbcPgOid table_oid, bool create_if_not_exists);

 private:
  Result<PgTableDescPtr> DoLoadTable(
      const PgObjectId& table_id, bool fail_on_cache_hit,
      master::IncludeHidden include_hidden = master::IncludeHidden::kFalse);
  Result<FlushFuture> FlushOperations(
      BufferableOperations&& ops, bool transactional, const PgFlushDebugContext& debug_context);
  std::string FlushReasonToString(const PgFlushDebugContext& debug_context) const;

  std::string LogPrefix() const;

  class RunHelper;

  struct PerformOptions {
    UseCatalogSession use_catalog_session = UseCatalogSession::kFalse;
    bool has_catalog_ops = false;
    std::optional<ReadTimeAction> read_time_action = {};
    std::optional<CacheOptions> cache_options = std::nullopt;
    HybridTime in_txn_limit = {};
  };

  Result<PerformFuture> Perform(BufferableOperations&& ops, PerformOptions&& options);

  template<class Generator>
  Result<PerformFuture> DoRunAsync(
      const Generator& generator, const RunOptions& options,
      std::optional<CacheOptions>&& cache_options = std::nullopt);

  template <class... Args>
  auto SetupPerformOptions(Args&&... args) {
    RSTATUS_DCHECK(buffer_.IsEmpty(), IllegalState, "No buffered operations are expected");
    return SetupPerformOptions(SetupPerformOptionsAccessorTag{}, std::forward<Args>(args)...);
  }

  template <class... Args>
  auto SetupPerformOptions(SetupPerformOptionsAccessorTag tag, Args&&... args) {
    return pg_txn_manager_->SetupPerformOptions(tag, std::forward<Args>(args)...);
  }

  struct TxnSerialNoPerformInfo {
    TxnSerialNoPerformInfo() : TxnSerialNoPerformInfo(0, ReadHybridTime()) {}

    TxnSerialNoPerformInfo(uint64_t txn_serial_no_, const ReadHybridTime& read_time_)
        : txn_serial_no(txn_serial_no_), read_time(read_time_) {
    }

    const uint64_t txn_serial_no;
    const ReadHybridTime read_time;
  };

  PgClient& pg_client_;

  // A transaction manager allowing to begin/abort/commit transactions.
  scoped_refptr<PgTxnManager> pg_txn_manager_;

  ReadHybridTime catalog_read_time_;

  // Execution status.
  Status status_;
  std::string errmsg_;

  uint64_t table_cache_min_ysql_catalog_version_ = 0;
  std::unordered_map<PgObjectId, PgTableDescPtr, PgObjectIdHash> table_cache_;

  using InsertOnConflictPlanBuffer = std::pair<void *, InsertOnConflictBuffer>;
  std::vector<InsertOnConflictPlanBuffer> insert_on_conflict_buffers_;

  PgDocMetrics metrics_;

  const YbcPgCallbacks& pg_callbacks_;

  // Should write operations be buffered?
  bool buffering_enabled_ = false;
  BufferingSettings& buffering_settings_;
  PgOperationBuffer buffer_;

  bool has_catalog_write_ops_in_ddl_mode_ = false;

  // This session is upgrading to PG15.
  const bool is_major_pg_version_upgrade_;

  const WaitEventWatcher& wait_event_watcher_;
  TablespaceCache tablespace_cache_;
  RunRWOperationsHook rw_operations_hook_;
};

template<class PB>
Status SetupPerformOptionsForDdlIfNeeded(PgSession& session, PB& req) {
  return req.use_regular_transaction_block() ?
    session.SetupPerformOptionsForDdl(req.mutable_options()) : Status::OK();
}

}  // namespace yb::pggate
