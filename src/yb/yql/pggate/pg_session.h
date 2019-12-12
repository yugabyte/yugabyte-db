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

#include <boost/optional.hpp>

#include "yb/client/client.h"
#include "yb/client/callbacks.h"
#include "yb/client/schema.h"
#include "yb/client/yb_table_name.h"

#include "yb/gutil/ref_counted.h"
#include "yb/gutil/callback.h"
#include "yb/util/oid_generator.h"
#include "yb/util/result.h"

#include "yb/server/hybrid_clock.h"

#include "yb/tserver/tserver_util_fwd.h"

#include "yb/yql/pggate/pg_env.h"
#include "yb/yql/pggate/pg_column.h"
#include "yb/yql/pggate/pg_tabledesc.h"

namespace yb {
namespace pggate {

YB_STRONGLY_TYPED_BOOL(OpBuffered);

class PgTxnManager;

// Convenience typedefs.
typedef std::vector<std::shared_ptr<client::YBPgsqlOp>> PgsqlOpBuffer;

struct PgApplyOutcome {
  OpBuffered buffered;
  client::YBSessionPtr yb_session;
};

// This class is not thread-safe as it is mostly used by a single-threaded PostgreSQL backend
// process.
class PgSession : public RefCountedThreadSafe<PgSession> {
 public:
  // Public types.
  typedef scoped_refptr<PgSession> ScopedRefPtr;

  // Constructors.
  PgSession(client::YBClient* client,
            const string& database_name,
            scoped_refptr<PgTxnManager> pg_txn_manager,
            scoped_refptr<server::HybridClock> clock,
            const tserver::TServerSharedObject* tserver_shared_object);
  virtual ~PgSession();

  //------------------------------------------------------------------------------------------------
  // Operations on Session.
  //------------------------------------------------------------------------------------------------
  // Reset.
  void Reset();

  CHECKED_STATUS ConnectDatabase(const std::string& database_name);

  //------------------------------------------------------------------------------------------------
  // Operations on Database Objects.
  //------------------------------------------------------------------------------------------------

  // API for database operations.
  CHECKED_STATUS CreateDatabase(const std::string& database_name,
                                PgOid database_oid,
                                PgOid source_database_oid,
                                PgOid nexte_oid);
  CHECKED_STATUS DropDatabase(const std::string& database_name, PgOid database_oid);
  client::YBNamespaceAlterer *NewNamespaceAlterer(const std::string& namespace_name,
                                                  PgOid database_oid);

  CHECKED_STATUS ReserveOids(PgOid database_oid,
                             PgOid nexte_oid,
                             uint32_t count,
                             PgOid *begin_oid,
                             PgOid *end_oid);

  CHECKED_STATUS GetCatalogMasterVersion(uint64_t *version);

  // API for sequences data operations.
  CHECKED_STATUS CreateSequencesDataTable();

  CHECKED_STATUS InsertSequenceTuple(int64_t db_oid,
                                     int64_t seq_oid,
                                     uint64_t ysql_catalog_version,
                                     int64_t last_val,
                                     bool is_called);

  CHECKED_STATUS UpdateSequenceTuple(int64_t db_oid,
                                     int64_t seq_oid,
                                     uint64_t ysql_catalog_version,
                                     int64_t last_val,
                                     bool is_called,
                                     boost::optional<int64_t> expected_last_val,
                                     boost::optional<bool> expected_is_called,
                                     bool* skipped);

  CHECKED_STATUS ReadSequenceTuple(int64_t db_oid,
                                   int64_t seq_oid,
                                   uint64_t ysql_catalog_version,
                                   int64_t *last_val,
                                   bool *is_called);

  CHECKED_STATUS DeleteSequenceTuple(int64_t db_oid, int64_t seq_oid);

  CHECKED_STATUS DeleteDBSequences(int64_t db_oid);

  // API for schema operations.
  // TODO(neil) Schema should be a sub-database that have some specialized property.
  CHECKED_STATUS CreateSchema(const std::string& schema_name, bool if_not_exist);
  CHECKED_STATUS DropSchema(const std::string& schema_name, bool if_exist);

  // API for table operations.
  client::YBTableCreator *NewTableCreator();
  client::YBTableAlterer *NewTableAlterer(const client::YBTableName& table_name);
  client::YBTableAlterer *NewTableAlterer(const string table_id);
  CHECKED_STATUS DropTable(const PgObjectId& table_id);
  CHECKED_STATUS DropIndex(const PgObjectId& index_id);
  CHECKED_STATUS TruncateTable(const PgObjectId& table_id);
  Result<PgTableDesc::ScopedRefPtr> LoadTable(const PgObjectId& table_id);
  void InvalidateTableCache(const PgObjectId& table_id);

  // Buffer write operations.
  CHECKED_STATUS StartBufferingWriteOperations();
  CHECKED_STATUS FlushBufferedWriteOperations();

  // Apply the given operation to read and write database content. If the operation is a write
  // op, return true if the operation is buffered and should not be flushed except in bulk
  // by FlushBufferedWriteOperations(). False otherwise.
  Result<PgApplyOutcome> PgApplyAsync(const std::shared_ptr<client::YBPgsqlOp>& op,
                                      uint64_t* read_time);

  CHECKED_STATUS PgFlushAsync(StatusFunctor callback, const client::YBSessionPtr& yb_session);
  CHECKED_STATUS RestartTransaction();
  bool HasAppliedOperations() const;

  // Return the number of errors which are pending.
  int CountPendingErrors() const;

  // Return the pending errors.
  std::vector<std::unique_ptr<client::YBError>> GetPendingErrors();

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
    return rowid_generator_.Next(true /* binary_id */);
  }

  void InvalidateCache() {
    table_cache_.clear();
  }

  // Check if initdb has already been run before. Needed to make initdb idempotent.
  CHECKED_STATUS IsInitDbDone(bool* initdb_done);

  // Returns the local tserver's catalog version stored in shared memory, or an error if
  // the shared memory has not been initialized (e.g. in initdb).
  Result<uint64_t> GetSharedCatalogVersion();

  PgTxnManager* pg_txn_manager() { return pg_txn_manager_.get(); }

 private:
  // Returns the appropriate session to use, in most cases the one used by the current transaction.
  // read_only_op - whether this is being done in the context of a read-only operation. For
  //                non-read-only operations we make sure to start a YB transaction.
  // We are returning a raw pointer here because the returned session is owned either by the
  // PgTxnManager or by this object.
  Result<client::YBSession*> GetSession(
      bool transactional,
      bool read_only_op,
      bool is_ysql_catalog_op);

  // Get the appropriate YBSession to apply the given operation to, based on whether or not this
  // is an operation on a transactional table, as well as read-only vs. non-read-only operation.
  Result<client::YBSession*> GetSessionForOp(const std::shared_ptr<client::YBPgsqlOp>& op);

  // Given a set of errors from operations, this function attempts to combine them into one status
  // that is later passed to PostgreSQL and further converted into a more specific error code.
  Status CombineErrorsToStatus(client::CollectedErrors errors, Status status);

  // Given two statuses, include messages from the first status in front and take state
  // from whichever is not OK (if there is one), otherwise return the first status.
  Status CombineStatuses(Status first_status, Status second_status);

  // Flush buffered write operations from the given buffer.
  Status FlushBufferedWriteOperations(PgsqlOpBuffer* write_ops, bool transactional);

  // Whether we should buffer and execute the given operation transactionally.
  bool ShouldBufferTransactionally(client::YBPgsqlOp* op);

  // YBClient, an API that SQL engine uses to communicate with all servers.
  client::YBClient* const client_;

  // YBSession to execute operations.
  std::shared_ptr<client::YBSession> session_;

  // Connected database.
  std::string connected_database_;

  // A transaction manager allowing to begin/abort/commit transactions.
  scoped_refptr<PgTxnManager> pg_txn_manager_;

  const scoped_refptr<server::HybridClock> clock_;

  // Execution status.
  Status status_;
  string errmsg_;

  // Rowid generator.
  ObjectIdGenerator rowid_generator_;

  std::unordered_map<TableId, std::shared_ptr<client::YBTable>> table_cache_;

  // Should write operations be buffered?
  uint buffer_write_ops_ = 0;
  PgsqlOpBuffer buffered_write_ops_;
  PgsqlOpBuffer buffered_txn_write_ops_;

  // True if the read request has a row mark.
  bool has_row_mark_ = false;

  const tserver::TServerSharedObject* const tserver_shared_object_;
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_PG_SESSION_H_
