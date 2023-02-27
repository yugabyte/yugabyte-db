//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_session.h"

#include <algorithm>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <utility>

#include <boost/functional/hash.hpp>

#include "opentelemetry/sdk/version/version.h"

#include "yb/client/table_info.h"

#include "yb/common/pg_types.h"
#include "yb/common/placement_info.h"
#include "yb/common/ql_expr.h"
#include "yb/common/ql_value.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/row_mark.h"
#include "yb/common/schema.h"
#include "yb/common/transaction_error.h"

#include "yb/gutil/casts.h"

#include "yb/tserver/pg_client.messages.h"

#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/string_util.h"

#include "yb/yql/pggate/pg_client.h"
#include "yb/yql/pggate/pg_expr.h"
#include "yb/yql/pggate/pg_op.h"
#include "yb/yql/pggate/pg_txn_manager.h"
#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pggate/ybc_pggate.h"

using namespace std::literals;

DEPRECATE_FLAG(int32, ysql_wait_until_index_permissions_timeout_ms, "11_2022");
DECLARE_int32(TEST_user_ddl_operation_timeout_sec);

DEFINE_UNKNOWN_bool(ysql_log_failed_docdb_requests, false, "Log failed docdb requests.");
DEFINE_test_flag(bool, ysql_ignore_add_fk_reference, false,
                 "Don't fill YSQL's internal cache for FK check to force read row from a table");

namespace yb {
namespace pggate {

namespace {

template<class Container, class Key>
void Erase(Container* container, const Key& key) {
  auto it = container->find(key);
  if (it != container->end()) {
    container->erase(it);
  }
}

YB_DEFINE_ENUM(SessionType, (kRegular)(kTransactional)(kCatalog));

bool IsNeedTransaction(const PgsqlOp& op, bool non_ddl_txn_for_sys_tables_allowed) {
  // op.need_transaction will be false for write operation in case upper level decides that
  // it is single row transaction. But in case the non_ddl_txn_for_sys_tables_allowed flag is true
  // extra read operation is possible within current transaction.
  //
  // Example:
  // CREATE TABLE t (k INT PRIMARY KEY);
  // INSERT INTO t VALUES(1);
  // SET yb_non_ddl_txn_for_sys_tables_allowed = true;
  // INSERT INTO t VALUES(1);
  //
  // Last statement inserts row with k = 1 and this is a single row transaction.
  // But row with k = 1 already exists in the t table. As a result the
  // 'duplicate key value violates unique constraint "t_pkey"' will be raised.
  // But this error contains contraints name which is read from sys table pg_class (in case it
  // is not yet in the postgres's cache). And this read from sys table will be performed in context
  // of currently running transaction (single row) because the yb_non_ddl_txn_for_sys_tables_allowed
  // GUC variable is true. As a result there will be 2 operations in context of single row
  // transaction.
  // To handle this situation correctly is it necessary to start transaction in case write
  // operation doesn't require it, but the non_ddl_txn_for_sys_tables_allowed flag is true.
  return op.need_transaction() || (op.is_write() && non_ddl_txn_for_sys_tables_allowed);
}

Result<bool> ShouldHandleTransactionally(
  const PgTxnManager& txn_manager, const PgTableDesc& table, const PgsqlOp& op) {
  if (!table.schema().table_properties().is_transactional() ||
      !IsNeedTransaction(op, yb_non_ddl_txn_for_sys_tables_allowed) ||
      YBCIsInitDbModeEnvVarSet()) {
    return false;
  }
  const auto has_non_ddl_txn = txn_manager.IsTxnInProgress();
  if (!table.schema().table_properties().is_ysql_catalog_table()) {
    SCHECK(has_non_ddl_txn, IllegalState, "Transactional operation requires transaction");
    return true;
  }
  // Previously, yb_non_ddl_txn_for_sys_tables_allowed flag caused CREATE VIEW to fail with
  // read restart error because subsequent cache refresh used an outdated txn to read from the
  // system catalog,
  // As a quick fix, we prevent yb_non_ddl_txn_for_sys_tables_allowed from affecting reads.
  if (txn_manager.IsDdlMode() || (yb_non_ddl_txn_for_sys_tables_allowed && has_non_ddl_txn)) {
    return true;
  }
  if (op.is_write()) {
    // For consistent read from catalog tables all write operations must be done in transaction.
    return STATUS_FORMAT(IllegalState,
                         "Transaction for catalog table write operation '$0' not found",
                         table.table_name().table_name());
  }
  return false;
}

Result<SessionType> GetRequiredSessionType(
  const PgTxnManager& txn_manager, const PgTableDesc& table, const PgsqlOp& op) {
  if (VERIFY_RESULT(ShouldHandleTransactionally(txn_manager, table, op))) {
    return SessionType::kTransactional;
  }

  return op.is_read() &&
         table.schema().table_properties().is_ysql_catalog_table() &&
        !YBCIsInitDbModeEnvVarSet()
      ? SessionType::kCatalog
      : SessionType::kRegular;
}

bool Empty(const PgOperationBuffer& buffer) {
  return !buffer.Size();
}

void Update(BufferingSettings* buffering_settings) {
  /* Use the gflag value if the session variable is unset for batch size. */
  buffering_settings->max_batch_size = ysql_session_max_batch_size <= 0
    ? FLAGS_ysql_session_max_batch_size
    : static_cast<uint64_t>(ysql_session_max_batch_size);
  buffering_settings->max_in_flight_operations = static_cast<uint64_t>(ysql_max_in_flight_ops);
}

RowMarkType GetRowMarkType(const PgsqlOp& op) {
  return op.is_read()
      ? GetRowMarkTypeFromPB(down_cast<const PgsqlReadOp&>(op).read_request())
      : RowMarkType::ROW_MARK_ABSENT;
}

bool IsReadOnly(const PgsqlOp& op) {
  return op.is_read() && !IsValidRowMarkType(GetRowMarkType(op));
}

} // namespace

//--------------------------------------------------------------------------------------------------
// Class PgSession::RunHelper
//--------------------------------------------------------------------------------------------------

class PgSession::RunHelper {
 public:
  RunHelper(PgSession* pg_session, SessionType session_type, HybridTime in_txn_limit)
      : pg_session_(*pg_session),
        session_type_(session_type),
        in_txn_limit_(in_txn_limit) {
  }

  Status Apply(const PgTableDesc& table, PgsqlOpPtr op, ForceNonBufferable force_non_bufferable) {
    // Refer src/yb/yql/pggate/README for details about buffering.
    //
    // TODO(#16261): Consider the following scenario:
    // 1) A read op arrives here and adds itself to operations_.
    // 2) Two write ops follow which change the same row and are added to operations_.
    //    Should they not be flushed separately? (as per requirement 2b in the README)
    //
    // Moreover, consider a situation in which they are flushed separately, but the in_txn_limit
    // picked might be incorrect:
    // 1) A non-bufferable write op on key k1 enters Apply() and is flushed.
    // 2) A read which has a preset in_txn_limit enters and is added to operations_. The
    //    in_txn_limit happens to be smaller than the write time of the write op in (1).
    // 3) Another write to key k1 is added to operations_. But it will end up using the preset
    //    in_txn_limit. Isn't this incorrect?
    //
    // Both scenarios might not occur with any sort of SQL statement. But we should still try to
    // find statements that can lead to this and fix these cases. Even if we don't find anything
    // that leads to such scenarios, we should fix these anyway (unless we prove these scenarios
    // can never occur).

    VLOG(2) << "Apply " << (op->is_read() ? "read" : "write") << " op, table name: "
            << table.table_name().table_name() << ", table id: " << table.id();
    auto& buffer = pg_session_.buffer_;

    // Try buffering this operation if it is a write operation, buffering is enabled and no
    // operations have been already applied to current session (yb session does not exist).
    if (operations_.empty() && pg_session_.buffering_enabled_ &&
        !force_non_bufferable && op->is_write()) {
        if (PREDICT_FALSE(yb_debug_log_docdb_requests)) {
          LOG(INFO) << "Buffering operation: " << op->ToString();
        }
        return buffer.Add(table,
                          PgsqlWriteOpPtr(std::move(op), down_cast<PgsqlWriteOp*>(op.get())),
                          IsTransactional());
    }
    bool read_only = op->is_read();
    // Flush all buffered operations (if any) before performing non-bufferable operation
    if (!Empty(buffer)) {
      SCHECK(operations_.empty(),
            IllegalState,
            "Buffered operations must be flushed before applying first non-bufferable operation");
      // Buffered write operations can't be combined within a single RPC with non-bufferable read
      // operations in case the reads have a preset in_txn_limit. This is because as per requirement
      // 1 and 2b) in README.txt, the writes and reads will require a different in_txn_limit. In
      // case the in_txn_limit wasn't yet picked for the reads, they can be flushed together with
      // the writes since both would use the current hybrid time as the in_txn_limit.
      //
      // Note however that the buffer has to be flushed before the non-buffered op if:
      // 1) it is a non-buffered read that is on a table that is touched by the buffered writes
      // 2) it is a non-buffered write that touches the same key as a buffered write
      //
      // Also operations for a catalog session are read-only operations. And "buffer" only buffers
      // write operations. Since the writes and catalog reads belong to different session types (as
      // per PgClientSession), we can't send them to the local tserver proxy in 1 rpc, so we flush
      // the buffer before performing catalog reads.
      if ((IsTransactional() && in_txn_limit_) || IsCatalog()) {
        RETURN_NOT_OK(buffer.Flush());
      } else {
        operations_ = VERIFY_RESULT(buffer.FlushTake(table, *op, IsTransactional()));
        read_only = read_only && operations_.empty();
      }
    }

    if (PREDICT_FALSE(yb_debug_log_docdb_requests)) {
      LOG(INFO) << "Applying operation: " << op->ToString();
    }

    const auto row_mark_type = GetRowMarkType(*op);

    operations_.Add(std::move(op), table.id());

    if (!IsTransactional()) {
      return Status::OK();
    }

    const auto txn_priority_requirement =
      pg_session_.GetIsolationLevel() == PgIsolationLevel::READ_COMMITTED
        ? kHighestPriority :
          (RowMarkNeedsHigherPriority(row_mark_type) ? kHigherPriorityRange : kLowerPriorityRange);
    read_only = read_only && !IsValidRowMarkType(row_mark_type);

    return pg_session_.pg_txn_manager_->CalculateIsolation(read_only, txn_priority_requirement);
  }

  Result<PerformFuture> Flush(std::optional<CacheOptions>&& cache_options) {
    if (operations_.empty()) {
      // All operations were buffered, no need to flush.
      return PerformFuture();
    }

    if (PREDICT_FALSE(yb_debug_log_docdb_requests)) {
      LOG(INFO) << "Flushing collected operations, using session type: "
                << ToString(session_type_) << " num ops: " << operations_.size();
    }

    return pg_session_.Perform(
        std::move(operations_),
        {.use_catalog_session = IsCatalog(),
         .cache_options = std::move(cache_options),
         .in_txn_limit = in_txn_limit_
        });
  }

 private:
  inline static bool IsTransactional(SessionType type) {
    return type == SessionType::kTransactional;
  }

  inline bool IsTransactional() const {
    return IsTransactional(session_type_);
  }

  inline UseCatalogSession IsCatalog() const {
    return UseCatalogSession(session_type_ == SessionType::kCatalog);
  }

  PgSession& pg_session_;
  const SessionType session_type_;
  BufferableOperations operations_;
  const HybridTime in_txn_limit_;
};

//--------------------------------------------------------------------------------------------------
// Class TableYbctidHasher
//--------------------------------------------------------------------------------------------------

size_t TableYbctidHasher::operator()(const LightweightTableYbctid& value) const {
  size_t hash = 0;
  boost::hash_combine(hash, value.table_id);
  boost::hash_range(hash, value.ybctid.begin(), value.ybctid.end());
  return hash;
}

size_t TableYbctidHasher::operator()(const TableYbctid& value) const {
  return (*this)(static_cast<LightweightTableYbctid>(value));
}

//--------------------------------------------------------------------------------------------------
// Class PgSession
//--------------------------------------------------------------------------------------------------

PgSession::PgSession(
    PgClient* pg_client,
    const std::string& database_name,
    scoped_refptr<PgTxnManager> pg_txn_manager,
    scoped_refptr<server::HybridClock> clock,
    const YBCPgCallbacks& pg_callbacks)
    : pg_client_(*pg_client),
      pg_txn_manager_(std::move(pg_txn_manager)),
      clock_(std::move(clock)),
      buffer_(std::bind(
          &PgSession::FlushOperations, this, std::placeholders::_1, std::placeholders::_2),
          buffering_settings_),
      pg_callbacks_(pg_callbacks) {
      Update(&buffering_settings_);
}

PgSession::~PgSession() = default;

//--------------------------------------------------------------------------------------------------

Status PgSession::ConnectDatabase(const std::string& database_name) {
  connected_database_ = database_name;
  return Status::OK();
}

Status PgSession::IsDatabaseColocated(const PgOid database_oid, bool *colocated,
                                      bool *legacy_colocated_database) {
  auto resp = VERIFY_RESULT(pg_client_.GetDatabaseInfo(database_oid));
  *colocated = resp.colocated();
  if (resp.has_legacy_colocated_database())
    *legacy_colocated_database = resp.legacy_colocated_database();
  else
    *legacy_colocated_database = true;
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status PgSession::DropDatabase(const std::string& database_name, PgOid database_oid) {
  tserver::PgDropDatabaseRequestPB req;
  req.set_database_name(database_name);
  req.set_database_oid(database_oid);

  RETURN_NOT_OK(pg_client_.DropDatabase(&req, CoarseTimePoint()));
  RETURN_NOT_OK(DeleteDBSequences(database_oid));
  return Status::OK();
}

// This function is only used to get the protobuf-based catalog version, not using
// the pg_yb_catalog_version table.
Status PgSession::GetCatalogMasterVersion(uint64_t *version) {
  *version = VERIFY_RESULT(pg_client_.GetCatalogMasterVersion());
  return Status::OK();
}

Status PgSession::CreateSequencesDataTable() {
  return pg_client_.CreateSequencesDataTable();
}

Status PgSession::InsertSequenceTuple(int64_t db_oid,
                                      int64_t seq_oid,
                                      uint64_t ysql_catalog_version,
                                      bool is_db_catalog_version_mode,
                                      int64_t last_val,
                                      bool is_called) {
  return pg_client_.InsertSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode, last_val, is_called);
}

Result<bool> PgSession::UpdateSequenceTuple(int64_t db_oid,
                                            int64_t seq_oid,
                                            uint64_t ysql_catalog_version,
                                            bool is_db_catalog_version_mode,
                                            int64_t last_val,
                                            bool is_called,
                                            std::optional<int64_t> expected_last_val,
                                            std::optional<bool> expected_is_called) {
  return pg_client_.UpdateSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode, last_val, is_called,
      expected_last_val, expected_is_called);
}

Result<std::pair<int64_t, int64_t>> PgSession::FetchSequenceTuple(int64_t db_oid,
                                                                  int64_t seq_oid,
                                                                  uint64_t ysql_catalog_version,
                                                                  bool is_db_catalog_version_mode,
                                                                  uint32_t fetch_count,
                                                                  int64_t inc_by,
                                                                  int64_t min_value,
                                                                  int64_t max_value,
                                                                  bool cycle) {
  return pg_client_.FetchSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode, fetch_count, inc_by,
      min_value, max_value, cycle);
}

Result<std::pair<int64_t, bool>> PgSession::ReadSequenceTuple(int64_t db_oid,
                                                              int64_t seq_oid,
                                                              uint64_t ysql_catalog_version,
                                                              bool is_db_catalog_version_mode) {
  return pg_client_.ReadSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode);
}

Status PgSession::DeleteSequenceTuple(int64_t db_oid, int64_t seq_oid) {
  return pg_client_.DeleteSequenceTuple(db_oid, seq_oid);
}

Status PgSession::DeleteDBSequences(int64_t db_oid) {
  return pg_client_.DeleteDBSequences(db_oid);
}

//--------------------------------------------------------------------------------------------------

Status PgSession::StartTraceForQuery() {
  LOG(INFO) << "Will get a tracer";
  this->query_tracer_ = trace::Provider::GetTracerProvider()->GetTracer("pg_session", OPENTELEMETRY_SDK_VERSION);
  LOG(INFO) << "Got tracer. Will get span";
  this->query_span_ = this->query_tracer_->StartSpan("HandleRequest");
  LOG(INFO) << "Got a span";
  return Status::OK();
}

Status PgSession::StopTraceForQuery() {
  this->query_span_->End();
  return Status::OK();
}
//--------------------------------------------------------------------------------------------------

Status PgSession::DropTable(const PgObjectId& table_id) {
  tserver::PgDropTableRequestPB req;
  table_id.ToPB(req.mutable_table_id());
  return ResultToStatus(pg_client_.DropTable(&req, CoarseTimePoint()));
}

Status PgSession::DropIndex(
    const PgObjectId& index_id,
    client::YBTableName* indexed_table_name) {
  tserver::PgDropTableRequestPB req;
  index_id.ToPB(req.mutable_table_id());
  req.set_index(true);
  auto result = VERIFY_RESULT(pg_client_.DropTable(&req, CoarseTimePoint()));
  if (indexed_table_name) {
    *indexed_table_name = std::move(result);
  }
  return Status::OK();
}

Status PgSession::DropTablegroup(const PgOid database_oid,
                                 PgOid tablegroup_oid) {
  tserver::PgDropTablegroupRequestPB req;
  PgObjectId tablegroup_id(database_oid, tablegroup_oid);
  tablegroup_id.ToPB(req.mutable_tablegroup_id());
  Status s = pg_client_.DropTablegroup(&req, CoarseTimePoint());
  InvalidateTableCache(PgObjectId(database_oid, tablegroup_oid), InvalidateOnPgClient::kFalse);
  return s;
}

//--------------------------------------------------------------------------------------------------

Result<PgTableDescPtr> PgSession::DoLoadTable(const PgObjectId& table_id, bool fail_on_cache_hit) {
  auto cached_table_it = table_cache_.find(table_id);
  const auto exists = cached_table_it != table_cache_.end();
  const auto cache_hit = exists && cached_table_it->second;
  SCHECK(!(fail_on_cache_hit && cache_hit), IllegalState,
        "Cache entry found while cache miss is expected.");
  if (cache_hit) {
    auto status = cached_table_it->second->EnsurePartitionListIsUpToDate(&pg_client_);
    if (status.ok()) {
      return cached_table_it->second;
    }

    // Failed to reload table partitions, let's try to reload the table.
    LOG(WARNING) << Format(
        "Partition list refresh failed for table \"$0\": $1. Invalidating table cache.",
        cached_table_it->second->table_name(), status);
    InvalidateTableCache(table_id, InvalidateOnPgClient::kFalse);
    return DoLoadTable(table_id, /* fail_on_cache_hit */ true);
  }

  VLOG(4) << "Table cache MISS: " << table_id;
  auto table = VERIFY_RESULT(pg_client_.OpenTable(table_id, exists, invalidate_table_cache_time_));
  invalidate_table_cache_time_ = CoarseTimePoint();
  if (exists) {
    cached_table_it->second = table;
  } else {
    table_cache_.emplace(table_id, table);
  }
  return table;
}

Result<PgTableDescPtr> PgSession::LoadTable(const PgObjectId& table_id) {
  VLOG(3) << "Loading table descriptor for " << table_id;
  return DoLoadTable(table_id, /* fail_on_cache_hit */ false);
}

void PgSession::InvalidateTableCache(
    const PgObjectId& table_id, InvalidateOnPgClient invalidate_on_pg_client) {
  if (invalidate_on_pg_client) {
    // Keep special record about this table_id, so when we would open this table again,
    // reopen flag will be sent to pg client service.
    // This flag means that pg client service should remove table from his cache and fetch
    // new data from master.
    // It is optional optimization, but some tests fails w/o it, since they expect that
    // local table information is updated after alter table operation.
    table_cache_[table_id] = nullptr;
  } else {
    auto it = table_cache_.find(table_id);
    if (it != table_cache_.end() && it->second) {
      table_cache_.erase(it);
    }
  }
}

void PgSession::InvalidateAllTablesCache() {
  invalidate_table_cache_time_ = CoarseMonoClock::now();
  table_cache_.clear();
}

Result<client::TableSizeInfo> PgSession::GetTableDiskSize(const PgObjectId& table_oid) {
  return pg_client_.GetTableDiskSize(table_oid);
}

Status PgSession::StartOperationsBuffering() {
  SCHECK(!buffering_enabled_, IllegalState, "Buffering has been already started");
  if (PREDICT_FALSE(!Empty(buffer_))) {
    LOG(DFATAL) << "Buffering hasn't been started yet but "
                << buffer_.Size()
                << " buffered operations found";
  }
  Update(&buffering_settings_);
  buffering_enabled_ = true;
  return Status::OK();
}

Status PgSession::StopOperationsBuffering() {
  SCHECK(buffering_enabled_, IllegalState, "Buffering hasn't been started");
  buffering_enabled_ = false;
  return FlushBufferedOperations();
}

void PgSession::ResetOperationsBuffering() {
  DropBufferedOperations();
  buffering_enabled_ = false;
}

Status PgSession::FlushBufferedOperations() {
  return buffer_.Flush();
}

void PgSession::DropBufferedOperations() {
  buffer_.Clear();
}

void PgSession::GetAndResetOperationFlushRpcStats(uint64_t* count,
                                                  uint64_t* wait_time) {
  buffer_.GetAndResetRpcStats(count, wait_time);
}

PgIsolationLevel PgSession::GetIsolationLevel() {
  return pg_txn_manager_->GetPgIsolationLevel();
}

bool PgSession::IsHashBatchingEnabled() {
  return yb_enable_hash_batch_in &&
      GetIsolationLevel() != PgIsolationLevel::SERIALIZABLE;
}

Result<bool> PgSession::IsInitDbDone() {
  return pg_client_.IsInitDbDone();
}

Result<PerformFuture> PgSession::FlushOperations(BufferableOperations ops, bool transactional) {
  if (PREDICT_FALSE(yb_debug_log_docdb_requests)) {
    LOG(INFO) << "Flushing buffered operations, using "
              << (transactional ? "transactional" : "non-transactional")
              << " session (num ops: " << ops.size() << ")";
  }

  if (transactional) {
    auto txn_priority_requirement = kLowerPriorityRange;
    if (GetIsolationLevel() == PgIsolationLevel::READ_COMMITTED) {
      txn_priority_requirement = kHighestPriority;
    }

    RETURN_NOT_OK(pg_txn_manager_->CalculateIsolation(
        false /* read_only */, txn_priority_requirement));
  }

  // In case of flushing of non-transactional operations it is required to set read time with the
  // very first (and all further) request as flushing is done asynchronously (i.e. YSQL may send
  // multiple bunch of operations in parallel). As a result PgClientService is unable to use read
  // time from remote t-server or generate its own.
  return Perform(
      std::move(ops), {.ensure_read_time_is_set = EnsureReadTimeIsSet(!transactional)});
}

Result<PerformFuture> PgSession::Perform(BufferableOperations&& ops, PerformOptions&& ops_options) {
  DCHECK(!ops.empty());
  tserver::PgPerformOptionsPB options;
  if (ops_options.use_catalog_session) {
    if (catalog_read_time_) {
      catalog_read_time_.ToPB(options.mutable_read_time());
    }
    options.set_use_catalog_session(true);
  } else {
    const auto txn_serial_no = pg_txn_manager_->SetupPerformOptions(&options);
    if (pg_txn_manager_->IsTxnInProgress()) {
      options.mutable_in_txn_limit_ht()->set_value(ops_options.in_txn_limit.ToUint64());
    }
    ProcessPerformOnTxnSerialNo(txn_serial_no, ops_options.ensure_read_time_is_set, &options);
  }
  bool global_transaction = yb_force_global_transaction;
  for (auto i = ops.operations.begin(); !global_transaction && i != ops.operations.end(); ++i) {
    global_transaction = !(*i)->is_region_local();
  }
  options.set_force_global_transaction(global_transaction);

  options.set_use_xcluster_database_consistency(
      yb_xcluster_consistency_level == XCLUSTER_CONSISTENCY_DATABASE &&
      !(ops_options.use_catalog_session || pg_txn_manager_->IsDdlMode()));

  auto promise = std::make_shared<std::promise<PerformResult>>();

  // If all operations belong to the same database then set the namespace.
  // System database template1 is ignored as we may read global system catalog like tablespaces
  // in the same batch.
  if (!ops.relations.empty()) {
    PgOid database_oid = kPgInvalidOid;
    for (const auto& relation : ops.relations) {
      if (relation.database_oid == kTemplate1Oid) {
        continue;
      }

      if (PREDICT_FALSE(database_oid != kPgInvalidOid && database_oid != relation.database_oid)) {
        // We do not expect this to be true. Adding a log to catch violation just in case.
        YB_LOG_EVERY_N_SECS(WARNING, 60) << Format(
            "Operations from multiple databases ('$0', '$1') found in a single Perform step",
            database_oid, relation.database_oid);
        database_oid = kPgInvalidOid;
        break;
      }

      database_oid = relation.database_oid;
    }

    if (database_oid != kPgInvalidOid) {
      options.set_namespace_id(GetPgsqlNamespaceId(database_oid));
    }
  }
  options.set_trace_requested(pg_txn_manager_->ShouldEnableTracing());

  if (ops_options.cache_options) {
    auto& cache_options = *ops_options.cache_options;
    auto& caching_info = *options.mutable_caching_info();
    caching_info.set_key(std::move(cache_options.key));
    if (cache_options.lifetime_threshold_ms) {
      caching_info.mutable_lifetime_threshold_ms()->set_value(*cache_options.lifetime_threshold_ms);
    }
  }

  pg_client_.PerformAsync(&options, &ops.operations, [promise](const PerformResult& result) {
    promise->set_value(result);
  });
  return PerformFuture(promise->get_future(), this, std::move(ops.relations));
}

void PgSession::ProcessPerformOnTxnSerialNo(
    uint64_t txn_serial_no,
    EnsureReadTimeIsSet ensure_read_time_set_for_current_txn_serial_no,
    tserver::PgPerformOptionsPB* options) {
  if (txn_serial_no != std::get<0>(last_perform_on_txn_serial_no_).txn_serial_no) {
    last_perform_on_txn_serial_no_.emplace<0>(
        txn_serial_no,
        ensure_read_time_set_for_current_txn_serial_no
            ? ReadHybridTime::FromHybridTimeRange(clock_->NowRange())
            : ReadHybridTime());
  }
  const auto& read_time = std::get<0>(last_perform_on_txn_serial_no_).read_time;
  if (ensure_read_time_set_for_current_txn_serial_no && read_time && !options->has_read_time()) {
    read_time.ToPB(options->mutable_read_time());
  }
  options->set_trace_requested(pg_txn_manager_->ShouldEnableTracing());
}

Result<bool> PgSession::ForeignKeyReferenceExists(const LightweightTableYbctid& key,
                                                  const YbctidReader& reader) {
  if (fk_reference_cache_.find(key) != fk_reference_cache_.end()) {
    return true;
  }

  // Check existence of required FK intent.
  // Absence means the key was checked by previous batched request and was not found.
  // We don't need to call the reader in this case.
  auto it = fk_reference_intent_.find(key);
  if (it == fk_reference_intent_.end()) {
    return false;
  }

  std::vector<TableYbctid> ybctids;
  ybctids.reserve(std::min<size_t>(
      fk_reference_intent_.size(), buffering_settings_.max_batch_size));

  // If the reader fails to get the result, we fail the whole operation (and transaction).
  // Hence it's ok to extract (erase) the keys from intent before calling reader.
  auto node = fk_reference_intent_.extract(it);
  ybctids.push_back(std::move(node.value()));

  // Read up to session max batch size keys.
  for (auto it = fk_reference_intent_.begin();
       it != fk_reference_intent_.end() && ybctids.size() < ybctids.capacity(); ) {
    node = fk_reference_intent_.extract(it++);
    ybctids.push_back(std::move(node.value()));
  }

  // Add the keys found in docdb to the FK cache.
  RETURN_NOT_OK(reader(&ybctids, fk_intent_region_local_tables_));
  for (auto& ybctid : ybctids) {
    fk_reference_cache_.insert(std::move(ybctid));
  }

  return fk_reference_cache_.find(key) != fk_reference_cache_.end();
}

void PgSession::AddForeignKeyReferenceIntent(
    const LightweightTableYbctid& key, bool is_region_local) {
  if (fk_reference_cache_.find(key) == fk_reference_cache_.end()) {
    if (is_region_local) {
      fk_intent_region_local_tables_.insert(key.table_id);
    } else {
      fk_intent_region_local_tables_.erase(key.table_id);
    }
    fk_reference_intent_.emplace(key.table_id, std::string(key.ybctid));
  }
}

void PgSession::AddForeignKeyReference(const LightweightTableYbctid& key) {
  if (fk_reference_cache_.find(key) == fk_reference_cache_.end() &&
      PREDICT_TRUE(!FLAGS_TEST_ysql_ignore_add_fk_reference)) {
    fk_reference_cache_.emplace(key.table_id, std::string(key.ybctid));
  }
}

void PgSession::DeleteForeignKeyReference(const LightweightTableYbctid& key) {
  Erase(&fk_reference_cache_, key);
}

Result<int> PgSession::TabletServerCount(bool primary_only) {
  return pg_client_.TabletServerCount(primary_only);
}

Result<client::TabletServersInfo> PgSession::ListTabletServers() {
  return pg_client_.ListLiveTabletServers(false);
}

Status PgSession::GetIndexBackfillProgress(std::vector<PgObjectId> index_ids,
                                           uint64_t** backfill_statuses) {
  return pg_client_.GetIndexBackfillProgress(index_ids, backfill_statuses);
}

void PgSession::SetTimeout(const int timeout_ms) {
  pg_client_.SetTimeout(timeout_ms * 1ms);
}

void PgSession::ResetCatalogReadPoint() {
  catalog_read_time_ = ReadHybridTime();
}

void PgSession::TrySetCatalogReadPoint(const ReadHybridTime& read_ht) {
  if (read_ht) {
    catalog_read_time_ = read_ht;
  }
}

Status PgSession::SetActiveSubTransaction(SubTransactionId id) {
  // It's required that we flush all buffered operations before changing the SubTransactionMetadata
  // used by the underlying batcher and RPC logic, as this will snapshot the current
  // SubTransactionMetadata for use in construction of RPCs for already-queued operations, thereby
  // ensuring that previous operations use previous SubTransactionMetadata. If we do not flush here,
  // already queued operations may incorrectly use this newly modified SubTransactionMetadata when
  // they are eventually sent to DocDB.
  VLOG(4) << "SetActiveSubTransactionId " << id;
  RETURN_NOT_OK(FlushBufferedOperations());
  pg_txn_manager_->SetActiveSubTransactionId(id);

  return Status::OK();
}

Status PgSession::RollbackToSubTransaction(SubTransactionId id) {
  // See comment in SetActiveSubTransaction -- we must flush buffered operations before updating any
  // SubTransactionMetadata.
  // TODO(read committed): performance improvement -
  // don't wait for ops which have already been sent ahead by pg_session i.e., to the batcher, then
  // rpc layer and beyond. For such ops, rely on aborted sub txn list in status tablet to invalidate
  // writes which will be asynchronously written to txn participants.
  RETURN_NOT_OK(FlushBufferedOperations());
  tserver::PgPerformOptionsPB options;
  pg_txn_manager_->SetupPerformOptions(&options);
  return pg_client_.RollbackToSubTransaction(id, &options);
}

void PgSession::ResetHasWriteOperationsInDdlMode() {
  has_write_ops_in_ddl_mode_ = false;
}

bool PgSession::HasWriteOperationsInDdlMode() const {
  return has_write_ops_in_ddl_mode_ && pg_txn_manager_->IsDdlMode();
}

void PgSession::SetDdlHasSyscatalogChanges() {
  pg_txn_manager_->SetDdlHasSyscatalogChanges();
}

Status PgSession::ValidatePlacement(const std::string& placement_info) {
  tserver::PgValidatePlacementRequestPB req;

  Result<PlacementInfoConverter::Placement> result =
      PlacementInfoConverter::FromString(placement_info);

  // For validation, if there is no replica_placement option, we default to the
  // cluster configuration which the user is responsible for maintaining
  if (!result.ok() && result.status().IsInvalidArgument()) {
    return Status::OK();
  }

  RETURN_NOT_OK(result);

  PlacementInfoConverter::Placement placement = result.get();
  for (const auto& block : placement.placement_infos) {
    auto pb = req.add_placement_infos();
    pb->set_cloud(block.cloud);
    pb->set_region(block.region);
    pb->set_zone(block.zone);
    pb->set_min_num_replicas(block.min_num_replicas);
    pb->set_leader_preference(block.leader_preference);
  }
  req.set_num_replicas(placement.num_replicas);

  return pg_client_.ValidatePlacement(&req);
}

template<class Generator>
Result<PerformFuture> PgSession::DoRunAsync(
    const Generator& generator, HybridTime in_txn_limit, ForceNonBufferable force_non_bufferable,
    std::optional<CacheOptions>&& cache_options) {
  auto table_op = generator();
  SCHECK(!table_op.IsEmpty(), IllegalState, "Operation list must not be empty");
  const auto* table = table_op.table;
  const auto* op = table_op.operation;
  const auto group_session_type = VERIFY_RESULT(GetRequiredSessionType(
      *pg_txn_manager_, *table, **op));
  RunHelper runner(this, group_session_type, in_txn_limit);
  const auto ddl_mode = pg_txn_manager_->IsDdlMode();
  for (; !table_op.IsEmpty(); table_op = generator()) {
    table = table_op.table;
    op = table_op.operation;
    const auto op_session_type = VERIFY_RESULT(GetRequiredSessionType(
        *pg_txn_manager_, *table, **op));
    SCHECK_EQ(
        op_session_type, group_session_type,
        IllegalState, "Operations on different sessions can't be mixed");
    has_write_ops_in_ddl_mode_ = has_write_ops_in_ddl_mode_ || (ddl_mode && !IsReadOnly(**op));
    RETURN_NOT_OK(runner.Apply(*table, *op, force_non_bufferable));
  }
  return runner.Flush(std::move(cache_options));
}

Result<PerformFuture> PgSession::RunAsync(const OperationGenerator& generator,
                                          HybridTime in_txn_limit,
                                          ForceNonBufferable force_non_bufferable) {
  return DoRunAsync(generator, in_txn_limit, force_non_bufferable);
}

Result<PerformFuture> PgSession::RunAsync(const ReadOperationGenerator& generator,
                                          HybridTime in_txn_limit,
                                          ForceNonBufferable force_non_bufferable) {
  return DoRunAsync(generator, in_txn_limit, force_non_bufferable);
}

Result<PerformFuture> PgSession::RunAsync(
    const ReadOperationGenerator& generator, CacheOptions&& cache_options) {
  SCHECK(!cache_options.key.empty(), InvalidArgument, "Cache key can't be empty");
  // Ensure no buffered requests will be added to cached request.
  RETURN_NOT_OK(buffer_.Flush());
  return DoRunAsync(generator, HybridTime(), ForceNonBufferable::kFalse, std::move(cache_options));
}

Result<bool> PgSession::CheckIfPitrActive() {
  return pg_client_.CheckIfPitrActive();
}

}  // namespace pggate
}  // namespace yb
