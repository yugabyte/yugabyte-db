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

#ifndef YB_YQL_PGGATE_PG_SESSION_H_
#define YB_YQL_PGGATE_PG_SESSION_H_

#include <unordered_set>

#include <boost/optional.hpp>
#include <boost/unordered_set.hpp>

#include "yb/client/client_fwd.h"
#include "yb/client/session.h"
#include "yb/client/transaction.h"

#include "yb/common/pg_types.h"
#include "yb/common/transaction.h"

#include "yb/gutil/ref_counted.h"

#include "yb/server/hybrid_clock.h"

#include "yb/tserver/tserver_util_fwd.h"

#include "yb/util/lw_function.h"
#include "yb/util/oid_generator.h"
#include "yb/util/result.h"

#include "yb/yql/pggate/pg_client.h"
#include "yb/yql/pggate/pg_gate_fwd.h"
#include "yb/yql/pggate/pg_env.h"
#include "yb/yql/pggate/pg_tabledesc.h"
#include "yb/yql/pggate/pg_txn_manager.h"

namespace yb {
namespace pggate {

YB_STRONGLY_TYPED_BOOL(OpBuffered);
YB_STRONGLY_TYPED_BOOL(InvalidateOnPgClient);

class PgTxnManager;
class PgSession;

struct BufferableOperations {
  PgsqlOps operations;
  PgObjectIds relations;

  void Add(PgsqlOpPtr op, const PgObjectId& relation) {
    operations.push_back(std::move(op));
    relations.push_back(relation);
  }

  void Clear() {
    operations.clear();
    relations.clear();
  }

  void Swap(BufferableOperations* rhs) {
    operations.swap(rhs->operations);
    relations.swap(rhs->relations);
  }

  bool empty() const {
    return operations.empty();
  }

  size_t size() const {
    return operations.size();
  }
};

struct PgForeignKeyReference {
  PgForeignKeyReference(PgOid table_id, std::string ybctid);
  PgOid table_id;
  std::string ybctid;
};

// Represents row id (ybctid) from the DocDB's point of view.
class RowIdentifier {
 public:
  RowIdentifier(const Schema& schema, const PgsqlWriteRequestPB& request);
  inline const std::string& ybctid() const;
  inline const std::string& table_id() const;

 private:
  const std::string* table_id_;
  const std::string* ybctid_;
  std::string        ybctid_holder_;
};

YB_STRONGLY_TYPED_BOOL(IsTransactionalSession);
YB_STRONGLY_TYPED_BOOL(IsReadOnlyOperation);
YB_STRONGLY_TYPED_BOOL(IsCatalogOperation);

class PerformFuture {
 public:
  PerformFuture() = default;
  PerformFuture(std::future<PerformResult> future, PgSession* session, PgObjectIds* relations);

  bool Valid() const;

  CHECKED_STATUS Get();
 private:
  std::future<PerformResult> future_;
  PgSession* session_ = nullptr;
  PgObjectIds relations_;
};

// This class is not thread-safe as it is mostly used by a single-threaded PostgreSQL backend
// process.
class PgSession : public RefCountedThreadSafe<PgSession> {
 public:
  // Public types.
  typedef scoped_refptr<PgSession> ScopedRefPtr;

  // Constructors.
  PgSession(PgClient* pg_client,
            const string& database_name,
            scoped_refptr<PgTxnManager> pg_txn_manager,
            scoped_refptr<server::HybridClock> clock,
            const tserver::TServerSharedObject* tserver_shared_object,
            const YBCPgCallbacks& pg_callbacks);
  virtual ~PgSession();

  // Resets the read point for catalog tables.
  // Next catalog read operation will read the very latest catalog's state.
  void ResetCatalogReadPoint();

  //------------------------------------------------------------------------------------------------
  // Operations on Session.
  //------------------------------------------------------------------------------------------------

  CHECKED_STATUS ConnectDatabase(const std::string& database_name);

  CHECKED_STATUS IsDatabaseColocated(const PgOid database_oid, bool *colocated);

  //------------------------------------------------------------------------------------------------
  // Operations on Database Objects.
  //------------------------------------------------------------------------------------------------

  // API for database operations.
  CHECKED_STATUS DropDatabase(const std::string& database_name, PgOid database_oid);

  CHECKED_STATUS GetCatalogMasterVersion(uint64_t *version);

  // API for sequences data operations.
  CHECKED_STATUS CreateSequencesDataTable();

  CHECKED_STATUS InsertSequenceTuple(int64_t db_oid,
                                     int64_t seq_oid,
                                     uint64_t ysql_catalog_version,
                                     int64_t last_val,
                                     bool is_called);

  Result<bool> UpdateSequenceTuple(int64_t db_oid,
                                   int64_t seq_oid,
                                   uint64_t ysql_catalog_version,
                                   int64_t last_val,
                                   bool is_called,
                                   boost::optional<int64_t> expected_last_val,
                                   boost::optional<bool> expected_is_called);

  Result<std::pair<int64_t, bool>> ReadSequenceTuple(int64_t db_oid,
                                                     int64_t seq_oid,
                                                     uint64_t ysql_catalog_version);

  CHECKED_STATUS DeleteSequenceTuple(int64_t db_oid, int64_t seq_oid);

  CHECKED_STATUS DeleteDBSequences(int64_t db_oid);

  //------------------------------------------------------------------------------------------------
  // Operations on Tablegroup.
  //------------------------------------------------------------------------------------------------

  CHECKED_STATUS DropTablegroup(const PgOid database_oid,
                                PgOid tablegroup_oid);

  // API for schema operations.
  // TODO(neil) Schema should be a sub-database that have some specialized property.
  CHECKED_STATUS CreateSchema(const std::string& schema_name, bool if_not_exist);
  CHECKED_STATUS DropSchema(const std::string& schema_name, bool if_exist);

  // API for table operations.
  CHECKED_STATUS DropTable(const PgObjectId& table_id);
  CHECKED_STATUS DropIndex(
      const PgObjectId& index_id,
      client::YBTableName* indexed_table_name = nullptr);
  Result<PgTableDescPtr> LoadTable(const PgObjectId& table_id);
  void InvalidateTableCache(
      const PgObjectId& table_id, InvalidateOnPgClient invalidate_on_pg_client);

  // Start operation buffering. Buffering must not be in progress.
  CHECKED_STATUS StartOperationsBuffering();
  // Flush all pending buffered operation and stop further buffering.
  // Buffering must be in progress.
  CHECKED_STATUS StopOperationsBuffering();
  // Drop all pending buffered operations and stop further buffering. Buffering may be in any state.
  void ResetOperationsBuffering();

  // Flush all pending buffered operations. Buffering mode remain unchanged.
  CHECKED_STATUS FlushBufferedOperations();
  // Drop all pending buffered operations. Buffering mode remain unchanged.
  void DropBufferedOperations();

  PgIsolationLevel GetIsolationLevel();

  // Run (apply + flush) list of given operations to read and write database content.
  struct TableOperation {
    const PgsqlOpPtr* operation = nullptr;
    const PgTableDesc* table = nullptr;
  };

  using OperationGenerator = LWFunction<TableOperation()>;

  template<class... Args>
  Result<PerformFuture> RunAsync(
    const PgsqlOpPtr* ops, size_t ops_count, const PgTableDesc& table, Args&&... args) {
    const auto generator = [ops, end = ops + ops_count, &table]() mutable {
        return ops != end
            ? TableOperation { .operation = ops++, .table = &table }
            : TableOperation();
    };
    return RunAsync(make_lw_function(generator), std::forward<Args>(args)...);
  }

  Result<PerformFuture> RunAsync(
    const OperationGenerator& generator, uint64_t* read_time, bool force_non_bufferable);

  // Smart driver functions.
  // -------------
  Result<client::TabletServersInfo> ListTabletServers();

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

  const string& connected_database() const {
    return connected_database_;
  }
  void set_connected_database(const std::string& database) {
    connected_database_ = database;
  }
  void reset_connected_database() {
    connected_database_ = "";
  }

  // Generate a new random and unique rowid. It is a v4 UUID.
  string GenerateNewRowid() {
    return GenerateObjectId(true /* binary_id */);
  }

  void InvalidateAllTablesCache();

  void InvalidateForeignKeyReferenceCache() {
    fk_reference_cache_.clear();
    fk_reference_intent_.clear();
  }

  // Check if initdb has already been run before. Needed to make initdb idempotent.
  Result<bool> IsInitDbDone();

  // Return the local tserver's catalog version stored in shared memory or an error if the shared
  // memory has not been initialized (e.g. in initdb).
  Result<uint64_t> GetSharedCatalogVersion();
  // Return the local tserver's postgres authentication key stored in shared memory or an error if
  // the shared memory has not been initialized (e.g. in initdb).
  Result<uint64_t> GetSharedAuthKey();

  using YbctidReader =
      LWFunction<Result<std::vector<std::string>>(PgOid, const std::vector<Slice>&)>;
  Result<bool> ForeignKeyReferenceExists(
      PgOid table_id, const Slice& ybctid, const YbctidReader& reader);
  void AddForeignKeyReferenceIntent(PgOid table_id, const Slice& ybctid);
  void AddForeignKeyReference(PgOid table_id, const Slice& ybctid);

  // Deletes the row referenced by ybctid from FK reference cache.
  void DeleteForeignKeyReference(PgOid table_id, const Slice& ybctid);

  CHECKED_STATUS PatchStatus(const Status& status, const PgObjectIds& relations);

  Result<int> TabletServerCount(bool primary_only = false);

  // Sets the specified timeout in the rpc service.
  void SetTimeout(int timeout_ms);

  CHECKED_STATUS ValidatePlacement(const string& placement_info);

  void TrySetCatalogReadPoint(const ReadHybridTime& read_ht);

  PgClient& pg_client() const {
    return pg_client_;
  }

  bool ShouldUseFollowerReads() const;

  CHECKED_STATUS SetActiveSubTransaction(SubTransactionId id);
  CHECKED_STATUS RollbackSubTransaction(SubTransactionId id);

 private:
  using Flusher = LWFunction<Status(BufferableOperations, IsTransactionalSession)>;

  CHECKED_STATUS FlushBufferedOperationsImpl(const Flusher& flusher);
  CHECKED_STATUS FlushOperations(BufferableOperations ops, IsTransactionalSession transactional);

  class RunHelper;

  // Flush buffered write operations from the given buffer.
  Status FlushBufferedWriteOperations(BufferableOperations* write_ops, bool transactional);

  void Perform(PgsqlOps* operations, bool use_catalog_session, const PerformCallback& callback);

  void UpdateInTxnLimit(uint64_t* read_time);

  PgClient& pg_client_;

  // Connected database.
  std::string connected_database_;

  // A transaction manager allowing to begin/abort/commit transactions.
  scoped_refptr<PgTxnManager> pg_txn_manager_;

  const scoped_refptr<server::HybridClock> clock_;

  // YBSession to read data from catalog tables.
  boost::optional<ReadHybridTime> catalog_read_time_;

  // Execution status.
  Status status_;
  string errmsg_;

  CoarseTimePoint invalidate_table_cache_time_;
  std::unordered_map<PgObjectId, PgTableDescPtr, PgObjectIdHash> table_cache_;
  boost::unordered_set<PgForeignKeyReference> fk_reference_cache_;
  boost::unordered_set<PgForeignKeyReference> fk_reference_intent_;

  // Should write operations be buffered?
  bool buffering_enabled_ = false;
  BufferableOperations buffered_ops_;
  BufferableOperations buffered_txn_ops_;
  std::unordered_set<RowIdentifier, boost::hash<RowIdentifier>> buffered_keys_;

  HybridTime in_txn_limit_;

  const tserver::TServerSharedObject* const tserver_shared_object_;
  const YBCPgCallbacks& pg_callbacks_;
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_PG_SESSION_H_
