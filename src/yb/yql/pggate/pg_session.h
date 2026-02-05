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
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "yb/client/client_fwd.h"
#include "yb/client/tablet_server.h"

#include "yb/common/pg_types.h"
#include "yb/common/transaction.h"

#include "yb/gutil/ref_counted.h"

#include "yb/util/lw_function.h"
#include "yb/util/result.h"

#include "yb/yql/pggate/insert_on_conflict_buffer.h"
#include "yb/yql/pggate/pg_client.h"
#include "yb/yql/pggate/pg_doc_metrics.h"
#include "yb/yql/pggate/pg_flush_future.h"
#include "yb/yql/pggate/pg_gate_fwd.h"
#include "yb/yql/pggate/pg_operation_buffer.h"
#include "yb/yql/pggate/pg_perform_future.h"
#include "yb/yql/pggate/pg_tabledesc.h"
#include "yb/yql/pggate/pg_txn_manager.h"

namespace yb::pggate {

YB_STRONGLY_TYPED_BOOL(OpBuffered);
YB_STRONGLY_TYPED_BOOL(InvalidateOnPgClient);
YB_STRONGLY_TYPED_BOOL(UseCatalogSession);
YB_STRONGLY_TYPED_BOOL(ForceNonBufferable);

// This class is not thread-safe as it is mostly used by a single-threaded PostgreSQL backend
// process.
class PgSession final : public RefCountedThreadSafe<PgSession> {
 public:
  // Public types.
  using ScopedRefPtr = scoped_refptr<PgSession>;

  // Constructors.
  PgSession(
      PgClient& pg_client,
      scoped_refptr<PgTxnManager> pg_txn_manager,
      const YbcPgCallbacks& pg_callbacks,
      YbcPgExecStatsState& stats_state,
      bool is_pg_binary_upgrade,
      std::reference_wrapper<const WaitEventWatcher> wait_event_watcher,
      BufferingSettings& buffering_settings);
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
  // Operations on Database Objects.
  //------------------------------------------------------------------------------------------------

  // API for database operations.
  Status DropDatabase(const std::string& database_name, PgOid database_oid);

  Status GetCatalogMasterVersion(uint64_t *version);

  Result<int> GetXClusterRole(uint32_t db_oid);

  Status CancelTransaction(const unsigned char* transaction_id);

  // API for sequences data operations.
  Status CreateSequencesDataTable();

  Status InsertSequenceTuple(int64_t db_oid,
                             int64_t seq_oid,
                             uint64_t ysql_catalog_version,
                             bool is_db_catalog_version_mode,
                             int64_t last_val,
                             bool is_called);

  Result<bool> UpdateSequenceTuple(int64_t db_oid,
                                   int64_t seq_oid,
                                   uint64_t ysql_catalog_version,
                                   bool is_db_catalog_version_mode,
                                   int64_t last_val,
                                   bool is_called,
                                   std::optional<int64_t> expected_last_val,
                                   std::optional<bool> expected_is_called);

  Result<std::pair<int64_t, int64_t>> FetchSequenceTuple(int64_t db_oid,
                                                         int64_t seq_oid,
                                                         uint64_t ysql_catalog_version,
                                                         bool is_db_catalog_version_mode,
                                                         uint32_t fetch_count,
                                                         int64_t inc_by,
                                                         int64_t min_value,
                                                         int64_t max_value,
                                                         bool cycle);

  Result<std::pair<int64_t, bool>> ReadSequenceTuple(int64_t db_oid,
                                                     int64_t seq_oid,
                                                     uint64_t ysql_catalog_version,
                                                     bool is_db_catalog_version_mode);

  //------------------------------------------------------------------------------------------------
  // Operations on Tablegroup.
  //------------------------------------------------------------------------------------------------

  // API for schema operations.
  // TODO(neil) Schema should be a sub-database that have some specialized property.
  Status CreateSchema(const std::string& schema_name, bool if_not_exist);
  Status DropSchema(const std::string& schema_name, bool if_exist);

  // API for table operations.
  Status DropTable(const PgObjectId& table_id, bool use_regular_transaction_block);
  Status DropIndex(
      const PgObjectId& index_id,
      bool use_regular_transaction_block,
      client::YBTableName* indexed_table_name = nullptr);
  Result<PgTableDescPtr> LoadTable(const PgObjectId& table_id);
  void InvalidateTableCache(
      const PgObjectId& table_id, InvalidateOnPgClient invalidate_on_pg_client);
  Result<client::TableSizeInfo> GetTableDiskSize(const PgObjectId& table_oid);

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
  Status FlushBufferedOperations(const YbcFlushDebugContext& debug_context);
  Status FlushBufferedOperations(YbcFlushReason reason);
  // Drop all pending buffered operations. Buffering mode remain unchanged.
  void DropBufferedOperations();

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

  template<class... Args>
  Result<PerformFuture> RunAsync(
      const PgsqlOpPtr* ops, size_t ops_count, const PgTableDesc& table,
      Args&&... args) {
    const auto generator = [ops, end = ops + ops_count, &table]() mutable {
        using TO = TableOperation<PgsqlOpPtr>;
        return ops != end ? TO{.operation = ops++, .table = &table} : TO();
    };
    return RunAsync(make_lw_function(generator), std::forward<Args>(args)...);
  }

  Result<PerformFuture> RunAsync(
      const OperationGenerator& generator, HybridTime in_txn_limit,
      ForceNonBufferable force_non_bufferable = ForceNonBufferable::kFalse);
  Result<PerformFuture> RunAsync(
      const ReadOperationGenerator& generator, HybridTime in_txn_limit,
      ForceNonBufferable force_non_bufferable = ForceNonBufferable::kFalse);

  struct CacheOptions {
    uint32_t key_group;
    std::string key_value;
    std::optional<uint32_t> lifetime_threshold_ms;
  };

  Result<PerformFuture> RunAsync(const ReadOperationGenerator& generator, CacheOptions&& options);

  // Lock functions.
  // -------------
  Result<yb::tserver::PgGetLockStatusResponsePB> GetLockStatusData(
      const std::string& table_id, const std::string& transaction_id);

  // Smart driver functions.
  // -------------
  Result<client::TabletServersInfo> ListTabletServers();

  Status GetIndexBackfillProgress(std::vector<PgObjectId> index_ids,
                                  uint64_t* num_rows_read_from_table,
                                  double* num_rows_backfilled);

  std::string GenerateNewYbrowid();

  void InvalidateAllTablesCache(uint64_t min_ysql_catalog_version);

  // Check if initdb has already been run before. Needed to make initdb idempotent.
  Result<bool> IsInitDbDone();

  InsertOnConflictBuffer& GetInsertOnConflictBuffer(void* plan);
  InsertOnConflictBuffer& GetInsertOnConflictBuffer();
  void ClearAllInsertOnConflictBuffers();
  void ClearInsertOnConflictBuffer(void* plan);
  bool IsInsertOnConflictBufferEmpty() const;

  Result<int> TabletServerCount(bool primary_only = false);

  // Sets the specified timeout in the rpc service.
  void SetTimeout(int timeout_ms);

  void SetLockTimeout(int lock_timeout_ms);

  Status ValidatePlacements(
      const std::string& live_placement_info,
      const std::string& read_replica_placement_info,
      bool check_satisfiable);

  void TrySetCatalogReadPoint(const ReadHybridTime& read_ht);

  PgClient& pg_client() const {
    return pg_client_;
  }

  Status SetupIsolationAndPerformOptionsForDdl(
      tserver::PgPerformOptionsPB* options, bool use_regular_transaction_block);

  Status SetActiveSubTransaction(SubTransactionId id);
  Status RollbackToSubTransaction(SubTransactionId id);
  void SetTransactionHasWrites();
  Result<bool> CurrentTransactionUsesFastPath() const;

  void ResetHasCatalogWriteOperationsInDdlMode();
  bool HasCatalogWriteOperationsInDdlMode() const;

  void SetDdlHasSyscatalogChanges();

  Result<bool> CheckIfPitrActive();

  Result<TableKeyRanges> GetTableKeyRanges(
      const PgObjectId& table_id, Slice lower_bound_key, Slice upper_bound_key,
      uint64_t max_num_ranges, uint64_t range_size_bytes, bool is_forward, uint32_t max_key_length);

  PgDocMetrics& metrics() { return metrics_; }

  // Check whether the specified table has a CDC stream.
  Result<bool> IsObjectPartOfXRepl(const PgObjectId& table_id);

  Result<yb::tserver::PgListReplicationSlotsResponsePB> ListReplicationSlots();

  Result<yb::tserver::PgGetReplicationSlotResponsePB> GetReplicationSlot(
      const ReplicationSlotName& slot_name);

  [[nodiscard]] PgWaitEventWatcher StartWaitEvent(ash::WaitStateCode wait_event);

  Result<yb::tserver::PgYCQLStatementStatsResponsePB> YCQLStatementStats();
  Result<yb::tserver::PgActiveSessionHistoryResponsePB> ActiveSessionHistory();

  Result<yb::tserver::PgTabletsMetadataResponsePB> TabletsMetadata();

  Result<yb::tserver::PgServersMetricsResponsePB> ServersMetrics();

  Status SetCronLastMinute(int64_t last_minute);
  Result<int64_t> GetCronLastMinute();

  Status AcquireAdvisoryLock(
      const YbcAdvisoryLockId& lock_id, YbcAdvisoryLockMode mode, bool wait, bool session);
  Status ReleaseAdvisoryLock(const YbcAdvisoryLockId& lock_id, YbcAdvisoryLockMode mode);
  Status ReleaseAllAdvisoryLocks(uint32_t db_oid);

 private:
  Result<PgTableDescPtr> DoLoadTable(
      const PgObjectId& table_id, bool fail_on_cache_hit,
      master::IncludeHidden include_hidden = master::IncludeHidden::kFalse);
  Result<FlushFuture> FlushOperations(
      BufferableOperations&& ops, bool transactional, const YbcFlushDebugContext& debug_context);
  std::string FlushReasonToString(const YbcFlushDebugContext& debug_context) const;

  const std::string LogPrefix() const;

  class RunHelper;

  struct PerformOptions {
    UseCatalogSession use_catalog_session = UseCatalogSession::kFalse;
    std::optional<ReadTimeAction> read_time_action = {};
    std::optional<CacheOptions> cache_options = std::nullopt;
    HybridTime in_txn_limit = {};
  };

  Result<PerformFuture> Perform(BufferableOperations&& ops, PerformOptions&& options);

  template<class Generator>
  Result<PerformFuture> DoRunAsync(
      const Generator& generator, HybridTime in_txn_limit, ForceNonBufferable force_non_bufferable,
      std::optional<CacheOptions>&& cache_options = std::nullopt);

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
};

}  // namespace yb::pggate
