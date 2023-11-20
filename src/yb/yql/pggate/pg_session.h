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

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "yb/client/client_fwd.h"
#include "yb/client/tablet_server.h"

#include "yb/common/pg_types.h"
#include "yb/common/transaction.h"

#include "yb/gutil/ref_counted.h"

#include "yb/tserver/tserver_util_fwd.h"

#include "yb/util/lw_function.h"
#include "yb/util/oid_generator.h"
#include "yb/util/result.h"
#include "yb/util/wait_state.h"

#include "yb/yql/pggate/pg_client.h"
#include "yb/yql/pggate/pg_doc_metrics.h"
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

class PgTxnManager;
class PgSession;

struct LightweightTableYbctid {
  LightweightTableYbctid(PgOid table_id_, const std::string_view& ybctid_)
      : table_id(table_id_), ybctid(ybctid_) {}
  LightweightTableYbctid(PgOid table_id_, const Slice& ybctid_)
      : LightweightTableYbctid(table_id_, static_cast<std::string_view>(ybctid_)) {}

  PgOid table_id;
  std::string_view ybctid;
};

struct TableYbctid {
  TableYbctid(PgOid table_id_, std::string ybctid_)
      : table_id(table_id_), ybctid(std::move(ybctid_)) {}

  explicit operator LightweightTableYbctid() const {
    return LightweightTableYbctid(table_id, static_cast<std::string_view>(ybctid));
  }

  PgOid table_id;
  std::string ybctid;
};

struct TableYbctidComparator {
  using is_transparent = void;

  bool operator()(const LightweightTableYbctid& l, const LightweightTableYbctid& r) const {
    return l.table_id == r.table_id && l.ybctid == r.ybctid;
  }

  template<class T1, class T2>
  bool operator()(const T1& l, const T2& r) const {
    return (*this)(AsLightweightTableYbctid(l), AsLightweightTableYbctid(r));
  }

 private:
  static const LightweightTableYbctid& AsLightweightTableYbctid(
      const LightweightTableYbctid& value) {
    return value;
  }

  static LightweightTableYbctid AsLightweightTableYbctid(const TableYbctid& value) {
    return LightweightTableYbctid(value);
  }
};

struct TableYbctidHasher {
  using is_transparent = void;

  size_t operator()(const LightweightTableYbctid& value) const;
  size_t operator()(const TableYbctid& value) const;
};

// This class is not thread-safe as it is mostly used by a single-threaded PostgreSQL backend
// process.
class PgSession : public RefCountedThreadSafe<PgSession> {
 public:
  // Public types.
  typedef scoped_refptr<PgSession> ScopedRefPtr;

  // Constructors.
  PgSession(
      PgClient* pg_client,
      const std::string& database_name,
      scoped_refptr<PgTxnManager> pg_txn_manager,
      const YBCPgCallbacks& pg_callbacks,
      YBCPgExecStatsState* stats_state);
  virtual ~PgSession();

  // Resets the read point for catalog tables.
  // Next catalog read operation will read the very latest catalog's state.
  void ResetCatalogReadPoint();
  [[nodiscard]] const ReadHybridTime& catalog_read_time() const { return catalog_read_time_; }

  //------------------------------------------------------------------------------------------------
  // Operations on Session.
  //------------------------------------------------------------------------------------------------

  Status ConnectDatabase(const std::string& database_name);

  Status IsDatabaseColocated(const PgOid database_oid, bool *colocated,
                             bool *legacy_colocated_database);

  //------------------------------------------------------------------------------------------------
  // Operations on Database Objects.
  //------------------------------------------------------------------------------------------------

  // API for database operations.
  Status DropDatabase(const std::string& database_name, PgOid database_oid);

  Status GetCatalogMasterVersion(uint64_t *version);

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

  Status DropTablegroup(const PgOid database_oid,
                        PgOid tablegroup_oid);

  // API for schema operations.
  // TODO(neil) Schema should be a sub-database that have some specialized property.
  Status CreateSchema(const std::string& schema_name, bool if_not_exist);
  Status DropSchema(const std::string& schema_name, bool if_exist);

  // API for table operations.
  Status DropTable(const PgObjectId& table_id);
  Status DropIndex(
      const PgObjectId& index_id,
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

  // Flush all pending buffered operations. Buffering mode remain unchanged.
  Status FlushBufferedOperations();
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
    std::string key;
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

  Status GetIndexBackfillProgress(std::vector<PgObjectId> index_ids, uint64_t** backfill_statuses);

  //------------------------------------------------------------------------------------------------
  // Access functions.
  // TODO(neil) Need to double check these code later.
  // - This code in CQL processor has a lock. CQL comment: It can be accessed by multiple calls in
  //   parallel so they need to be thread-safe for shared reads / exclusive writes.
  //
  // - Currently, for each session, server executes the client requests sequentially, so the
  //   the following mutex is not necessary. I don't think we're capable of parallel-processing
  //   multiple statements within one session.
  //
  // TODO(neil) MUST ADD A LOCK FOR ACCESSING AND MODIFYING DATABASE BECAUSE WE USE THIS VARIABLE
  // AS INDICATOR FOR ALIVE OR DEAD SESSIONS.

  // Access functions for connected database.
  const char* connected_dbname() const {
    return connected_database_.c_str();
  }

  const std::string& connected_database() const {
    return connected_database_;
  }
  void set_connected_database(const std::string& database) {
    connected_database_ = database;
  }
  void reset_connected_database() {
    connected_database_ = "";
  }

  // Generate a new random and unique rowid. It is a v4 UUID.
  std::string GenerateNewRowid() {
    return GenerateObjectId(true /* binary_id */);
  }

  void InvalidateAllTablesCache();

  void InvalidateForeignKeyReferenceCache() {
    fk_reference_cache_.clear();
    fk_reference_intent_.clear();
    fk_intent_region_local_tables_.clear();
  }

  // Check if initdb has already been run before. Needed to make initdb idempotent.
  Result<bool> IsInitDbDone();

  using YbctidReader =
      LWFunction<Status(std::vector<TableYbctid>*, const std::unordered_set<PgOid>&)>;
  Result<bool> ForeignKeyReferenceExists(
      const LightweightTableYbctid& key, const YbctidReader& reader);
  void AddForeignKeyReferenceIntent(const LightweightTableYbctid& key, bool is_region_local);
  void AddForeignKeyReference(const LightweightTableYbctid& key);

  // Deletes the row referenced by ybctid from FK reference cache.
  void DeleteForeignKeyReference(const LightweightTableYbctid& key);

  Result<int> TabletServerCount(bool primary_only = false);

  // Sets the specified timeout in the rpc service.
  void SetTimeout(int timeout_ms);

  Status ValidatePlacement(const std::string& placement_info);

  void TrySetCatalogReadPoint(const ReadHybridTime& read_ht);

  PgClient& pg_client() const {
    return pg_client_;
  }

  Status SetActiveSubTransaction(SubTransactionId id);
  Status RollbackToSubTransaction(SubTransactionId id);

  void ResetHasWriteOperationsInDdlMode();
  bool HasWriteOperationsInDdlMode() const;

  void SetDdlHasSyscatalogChanges();

  Result<bool> CheckIfPitrActive();

  PgDocMetrics& metrics() { return metrics_; }

  Status SetTopLevelNodeId();

  void SetQueryId(int64_t query_id);

  void SetTopLevelRequestId();

  void SetWaitEventInfo(util::WaitStateCode wait_event) {
    uint32_t enum_int = to_underlying(wait_event);
    pg_callbacks_.SignalWaitStart(enum_int);
  }

  void UnsetWaitEventInfo() {
    pg_callbacks_.SignalWaitEnd();
  }

  Result<client::RpcsInfo> ActiveUniverseHistory();
  
  Result<tserver::PgTableIDMetadataResponsePB>TableIDMetadata();

  // Check whether the specified table has a CDC stream.
  Result<bool> IsObjectPartOfXRepl(const PgObjectId& table_id);

 private:
  Result<PgTableDescPtr> DoLoadTable(const PgObjectId& table_id, bool fail_on_cache_hit);
  Result<PerformFuture> FlushOperations(BufferableOperations ops, bool transactional);

  class RunHelper;

  struct PerformOptions {
    UseCatalogSession use_catalog_session = UseCatalogSession::kFalse;
    EnsureReadTimeIsSet ensure_read_time_is_set = EnsureReadTimeIsSet::kFalse;
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

  // Connected database.
  std::string connected_database_;

  // A transaction manager allowing to begin/abort/commit transactions.
  scoped_refptr<PgTxnManager> pg_txn_manager_;

  ReadHybridTime catalog_read_time_;

  // Execution status.
  Status status_;
  std::string errmsg_;

  CoarseTimePoint invalidate_table_cache_time_;
  std::unordered_map<PgObjectId, PgTableDescPtr, PgObjectIdHash> table_cache_;
  using TableYbctidSet = std::unordered_set<TableYbctid, TableYbctidHasher, TableYbctidComparator>;
  TableYbctidSet fk_reference_cache_;
  TableYbctidSet fk_reference_intent_;
  std::unordered_set<PgOid> fk_intent_region_local_tables_;

  PgDocMetrics metrics_;

  // Should write operations be buffered?
  bool buffering_enabled_ = false;
  BufferingSettings buffering_settings_;
  PgOperationBuffer buffer_;

  const YBCPgCallbacks& pg_callbacks_;
  bool has_write_ops_in_ddl_mode_ = false;
  std::variant<TxnSerialNoPerformInfo> last_perform_on_txn_serial_no_;

  util::AUHMetadata auh_metadata_;
};

}  // namespace yb::pggate
