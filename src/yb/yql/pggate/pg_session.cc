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

#include <boost/functional/hash.hpp>

#include "yb/common/pg_types.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/placement_info.h"
#include "yb/common/ql_expr.h"
#include "yb/common/ql_value.h"
#include "yb/common/row_mark.h"
#include "yb/common/schema.h"
#include "yb/common/transaction_error.h"

#include "yb/gutil/casts.h"

#include "yb/tserver/pg_client.messages.h"
#include "yb/tserver/tserver_shared_mem.h"

#include "yb/util/flag_tags.h"
#include "yb/util/format.h"
#include "yb/util/result.h"
#include "yb/util/shared_mem.h"
#include "yb/util/status_format.h"
#include "yb/util/string_util.h"

#include "yb/yql/pggate/pg_client.h"
#include "yb/yql/pggate/pg_expr.h"
#include "yb/yql/pggate/pg_op.h"
#include "yb/yql/pggate/pg_txn_manager.h"
#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pggate/ybc_pggate.h"

using namespace std::literals;

DEFINE_int32(ysql_wait_until_index_permissions_timeout_ms, 60 * 60 * 1000, // 60 min.
             "DEPRECATED: use backfill_index_client_rpc_timeout_ms instead.");
TAG_FLAG(ysql_wait_until_index_permissions_timeout_ms, advanced);
DECLARE_int32(TEST_user_ddl_operation_timeout_sec);

DEFINE_bool(ysql_log_failed_docdb_requests, false, "Log failed docdb requests.");
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
  RunHelper(PgSession* pg_session, SessionType session_type)
      : pg_session_(*pg_session), session_type_(session_type) {
  }

  Status Apply(const PgTableDesc& table,
                       const PgsqlOpPtr& op,
                       uint64_t* in_txn_limit,
                       bool force_non_bufferable) {
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
      // Buffered operations can't be combined within single RPC with non bufferable operation
      // in case non bufferable operation has preset in_txn_limit.
      // Buffered operations must be flushed independently in this case.
      // Also operations for catalog session can be combined with buffered operations
      // as catalog session is used for read-only operations.
      if ((IsTransactional() && in_txn_limit && *in_txn_limit) || IsCatalog()) {
        RETURN_NOT_OK(buffer.Flush());
      } else {
        operations_ = VERIFY_RESULT(buffer.FlushTake(table, *op, IsTransactional()));
        read_only = read_only && operations_.empty();
      }
    }

    if (PREDICT_FALSE(yb_debug_log_docdb_requests)) {
      LOG(INFO) << "Applying operation: " << op->ToString();
    }

    operations_.Add(op, table.id());

    if (!IsTransactional()) {
      return Status::OK();
    }

    const auto row_mark_type = GetRowMarkType(*op);
    const auto txn_priority_requirement =
      pg_session_.GetIsolationLevel() == PgIsolationLevel::READ_COMMITTED ||
      RowMarkNeedsHigherPriority(row_mark_type)
          ? kHigherPriorityRange : kLowerPriorityRange;
    read_only = read_only && !IsValidRowMarkType(row_mark_type);

    return pg_session_.pg_txn_manager_->CalculateIsolation(
        read_only, txn_priority_requirement, in_txn_limit);
  }

  Result<PerformFuture> Flush() {
    if (operations_.empty()) {
      // All operations were buffered, no need to flush.
      return PerformFuture();
    }

    return pg_session_.Perform(std::move(operations_), IsCatalog());
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
    const tserver::TServerSharedObject* tserver_shared_object,
    const YBCPgCallbacks& pg_callbacks)
    : pg_client_(*pg_client),
      pg_txn_manager_(std::move(pg_txn_manager)),
      clock_(std::move(clock)),
      buffer_(std::bind(
          &PgSession::FlushOperations, this, std::placeholders::_1, std::placeholders::_2),
          buffering_settings_),
      tserver_shared_object_(tserver_shared_object),
      pg_callbacks_(pg_callbacks) {
      Update(&buffering_settings_);
}

PgSession::~PgSession() = default;

//--------------------------------------------------------------------------------------------------

Status PgSession::ConnectDatabase(const std::string& database_name) {
  connected_database_ = database_name;
  return Status::OK();
}

Status PgSession::IsDatabaseColocated(const PgOid database_oid, bool *colocated) {
  auto resp = VERIFY_RESULT(pg_client_.GetDatabaseInfo(database_oid));
  *colocated = resp.colocated();
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
                                      int64_t last_val,
                                      bool is_called) {
  return pg_client_.InsertSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, last_val, is_called);
}

Result<bool> PgSession::UpdateSequenceTuple(int64_t db_oid,
                                            int64_t seq_oid,
                                            uint64_t ysql_catalog_version,
                                            int64_t last_val,
                                            bool is_called,
                                            boost::optional<int64_t> expected_last_val,
                                            boost::optional<bool> expected_is_called) {
  return pg_client_.UpdateSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, last_val, is_called, expected_last_val,
      expected_is_called);
}

Result<std::pair<int64_t, bool>> PgSession::ReadSequenceTuple(int64_t db_oid,
                                                              int64_t seq_oid,
                                                              uint64_t ysql_catalog_version) {
  return pg_client_.ReadSequenceTuple(db_oid, seq_oid, ysql_catalog_version);
}

Status PgSession::DeleteSequenceTuple(int64_t db_oid, int64_t seq_oid) {
  return pg_client_.DeleteSequenceTuple(db_oid, seq_oid);
}

Status PgSession::DeleteDBSequences(int64_t db_oid) {
  return pg_client_.DeleteDBSequences(db_oid);
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

Result<PgTableDescPtr> PgSession::LoadTable(const PgObjectId& table_id) {
  VLOG(3) << "Loading table descriptor for " << table_id;

  auto cached_table_it = table_cache_.find(table_id);
  bool exists = cached_table_it != table_cache_.end();
  if (exists && cached_table_it->second) {
    return cached_table_it->second;
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

PgIsolationLevel PgSession::GetIsolationLevel() {
  return pg_txn_manager_->GetPgIsolationLevel();
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

    // Use 0 as the value of in_txn_limit to force setting current time as txn limit
    uint64_t in_txn_limit = 0;
    RETURN_NOT_OK(pg_txn_manager_->CalculateIsolation(
        false /* read_only */, txn_priority_requirement, &in_txn_limit));
  }

  // In case of flushing of non-transactional operations it is required to set read time with the
  // very first (and all further) request as flushing is done asynchronously (i.e. YSQL may send
  // multiple bunch of operations in parallel). As a result PgClientService is unable to use read
  // time from remote t-server or generate its own.
  const auto ensure_read_time_set_for_current_txn_serial_no = !transactional;
  return Perform(
      std::move(ops), UseCatalogSession::kFalse, ensure_read_time_set_for_current_txn_serial_no);
}

Result<PerformFuture> PgSession::Perform(
    BufferableOperations ops,
    UseCatalogSession use_catalog_session,
    bool ensure_read_time_set_for_current_txn_serial_no) {
  DCHECK(!ops.empty());
  tserver::PgPerformOptionsPB options;
  if (use_catalog_session) {
    if (catalog_read_time_) {
      if (*catalog_read_time_) {
        catalog_read_time_->ToPB(options.mutable_read_time());
      } else {
        options.mutable_read_time();
      }
    }
    options.set_use_catalog_session(true);
  } else {
    const auto txn_serial_no = pg_txn_manager_->SetupPerformOptions(&options);
    ProcessPerformOnTxnSerialNo(
        txn_serial_no, ensure_read_time_set_for_current_txn_serial_no, &options);
  }
  bool global_transaction = yb_force_global_transaction;
  for (auto i = ops.operations.begin(); !global_transaction && i != ops.operations.end(); ++i) {
    global_transaction = !(*i)->is_region_local();
  }
  options.set_force_global_transaction(global_transaction);

  auto promise = std::make_shared<std::promise<PerformResult>>();

  pg_client_.PerformAsync(&options, &ops.operations, [promise](const PerformResult& result) {
    promise->set_value(result);
  });
  return PerformFuture(promise->get_future(), this, std::move(ops.relations));
}

void PgSession::ProcessPerformOnTxnSerialNo(uint64_t txn_serial_no,
                                            bool ensure_read_time_set_for_current_txn_serial_no,
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
}

Result<uint64_t> PgSession::GetSharedCatalogVersion() {
  if (tserver_shared_object_) {
    return (**tserver_shared_object_).ysql_catalog_version();
  } else {
    return STATUS(NotSupported, "Tablet server shared memory has not been opened");
  }
}

Result<uint64_t> PgSession::GetSharedAuthKey() {
  if (tserver_shared_object_) {
    return (**tserver_shared_object_).postgres_auth_key();
  } else {
    return STATUS(NotSupported, "Tablet server shared memory has not been opened");
  }
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

Status PgSession::PatchStatus(const Status& status, const PgObjectIds& relations) {
  if (PgsqlRequestStatus(status) == PgsqlResponsePB::PGSQL_STATUS_DUPLICATE_KEY_ERROR) {
    auto op_index = OpIndex::ValueFromStatus(status);
    if (op_index && *op_index < relations.size()) {
      return STATUS(AlreadyPresent, PgsqlError(YBPgErrorCode::YB_PG_UNIQUE_VIOLATION))
          .CloneAndAddErrorCode(RelationOid(relations[*op_index].object_oid));
    }
  }
  return status;
}

Result<int> PgSession::TabletServerCount(bool primary_only) {
  return pg_client_.TabletServerCount(primary_only);
}

Result<client::TabletServersInfo> PgSession::ListTabletServers() {
  return pg_client_.ListLiveTabletServers(false);
}

bool PgSession::ShouldUseFollowerReads() const {
  return pg_txn_manager_->ShouldUseFollowerReads();
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
  RETURN_NOT_OK(FlushBufferedOperations());
  tserver::PgPerformOptionsPB* options_ptr = nullptr;
  tserver::PgPerformOptionsPB options;
  if (pg_txn_manager_->GetIsolationLevel() == IsolationLevel::NON_TRANSACTIONAL) {
    auto txn_priority_requirement = kLowerPriorityRange;
    if (pg_txn_manager_->GetPgIsolationLevel() == PgIsolationLevel::READ_COMMITTED) {
      txn_priority_requirement = kHighestPriority;
    }
    RETURN_NOT_OK(pg_txn_manager_->CalculateIsolation(
        false /* read_only_op */, txn_priority_requirement));
    options_ptr = &options;
    pg_txn_manager_->SetupPerformOptions(&options);
  }
  return pg_client_.SetActiveSubTransaction(id, options_ptr);
}

Status PgSession::RollbackToSubTransaction(SubTransactionId id) {
  // See comment in SetActiveSubTransaction -- we must flush buffered operations before updating any
  // SubTransactionMetadata.
  // TODO(read committed): performance improvement -
  // don't wait for ops which have already been sent ahead by pg_session i.e., to the batcher, then
  // rpc layer and beyond. For such ops, rely on aborted sub txn list in status tablet to invalidate
  // writes which will be asynchronously written to txn participants.
  RETURN_NOT_OK(FlushBufferedOperations());
  return pg_client_.RollbackToSubTransaction(id);
}

void PgSession::ResetHasWriteOperationsInDdlMode() {
  has_write_ops_in_ddl_mode_ = false;
}

bool PgSession::HasWriteOperationsInDdlMode() const {
  return has_write_ops_in_ddl_mode_ && pg_txn_manager_->IsDdlMode();
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

Result<PerformFuture> PgSession::RunAsync(
  const OperationGenerator& generator, uint64_t* in_txn_limit, bool force_non_bufferable) {
  auto table_op = generator();
  SCHECK(table_op.operation, IllegalState, "Operation list must not be empty");
  const auto* table = table_op.table;
  const auto* op = table_op.operation;
  const auto group_session_type = VERIFY_RESULT(GetRequiredSessionType(
      *pg_txn_manager_, *table, **op));
  RunHelper runner(this, group_session_type);
  const auto ddl_mode = pg_txn_manager_->IsDdlMode();
  for (; table_op.operation; table_op = generator()) {
    table = table_op.table;
    op = table_op.operation;
    const auto op_session_type = VERIFY_RESULT(GetRequiredSessionType(
        *pg_txn_manager_, *table, **op));
    SCHECK_EQ(op_session_type,
              group_session_type,
              IllegalState,
              "Operations on different sessions can't be mixed");
    has_write_ops_in_ddl_mode_ = has_write_ops_in_ddl_mode_ || (ddl_mode && !IsReadOnly(**op));
    RETURN_NOT_OK(runner.Apply(*table, *op, in_txn_limit, force_non_bufferable));
  }
  return runner.Flush();
}

Result<bool> PgSession::CheckIfPitrActive() {
  return pg_client_.CheckIfPitrActive();
}

}  // namespace pggate
}  // namespace yb
