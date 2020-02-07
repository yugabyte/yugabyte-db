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

#include <memory>

#include "yb/util/logging.h"
#include "yb/yql/pggate/pg_expr.h"
#include "yb/yql/pggate/pg_session.h"
#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pggate/pggate_if_cxx_decl.h"
#include "yb/yql/pggate/pg_txn_manager.h"
#include "yb/yql/pggate/ybc_pggate.h"

#include "yb/client/batcher.h"
#include "yb/client/error.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_creator.h"
#include "yb/client/transaction.h"
#include "yb/client/yb_op.h"

#include "yb/common/pgsql_error.h"
#include "yb/common/ql_protocol_util.h"
#include "yb/common/row_mark.h"

#include "yb/tserver/tserver_shared_mem.h"

#include "yb/util/string_util.h"
#include "yb/util/random_util.h"

#include "yb/master/master.proxy.h"

namespace yb {
namespace pggate {

using std::make_shared;
using std::unique_ptr;
using std::shared_ptr;
using std::string;
using namespace std::literals;  // NOLINT

using client::YBClient;
using client::YBSession;
using client::YBMetaDataCache;
using client::YBSchema;
using client::YBColumnSchema;
using client::YBOperation;
using client::YBTable;
using client::YBTableName;
using client::YBTableType;

using yb::master::IsInitDbDoneRequestPB;
using yb::master::IsInitDbDoneResponsePB;
using yb::master::MasterServiceProxy;

using yb::tserver::TServerSharedObject;

#if defined(__APPLE__) && !defined(NDEBUG)
// We are experiencing more slowness in tests on macOS in debug mode.
const int kDefaultPgYbSessionTimeoutMs = 120 * 1000;
#else
const int kDefaultPgYbSessionTimeoutMs = 60 * 1000;
#endif

DEFINE_int32(pg_yb_session_timeout_ms, kDefaultPgYbSessionTimeoutMs,
             "Timeout for operations between PostgreSQL server and YugaByte DocDB services");

namespace {
//--------------------------------------------------------------------------------------------------
// Constants used for the sequences data table.
//--------------------------------------------------------------------------------------------------
static constexpr const char* const kPgSequencesNamespaceName = "system_postgres";
static constexpr const char* const kPgSequencesDataTableName = "sequences_data";

static const string kPgSequencesDataNamespaceId = GetPgsqlNamespaceId(kPgSequencesDataDatabaseOid);

// Columns names and ids.
static constexpr const char* const kPgSequenceDbOidColName = "db_oid";

static constexpr const char* const kPgSequenceSeqOidColName = "seq_oid";

static constexpr const char* const kPgSequenceLastValueColName = "last_value";
static constexpr const size_t kPgSequenceLastValueColIdx = 2;

static constexpr const char* const kPgSequenceIsCalledColName = "is_called";
static constexpr const size_t kPgSequenceIsCalledColIdx = 3;

string GetStatusStringSet(const client::CollectedErrors& errors) {
  std::set<string> status_strings;
  for (const auto& error : errors) {
    status_strings.insert(error->status().ToString());
  }
  return RangeToString(status_strings.begin(), status_strings.end());
}

// Given a set of errors from operations, this function attempts to combine them into one status
// that is later passed to PostgreSQL and further converted into a more specific error code.
Status CombineErrorsToStatus(client::CollectedErrors errors, Status status) {
  if (errors.empty())
    return status;

  if (status.IsIOError() &&
      // TODO: move away from string comparison here and use a more specific status than IOError.
      // See https://github.com/YugaByte/yugabyte-db/issues/702
      status.message() == client::internal::Batcher::kErrorReachingOutToTServersMsg &&
      errors.size() == 1) {
    return errors.front()->status();
  }
  if (status.ok()) {
    return STATUS(InternalError, GetStatusStringSet(errors));
  }
  return status.CloneAndAppend(". Errors from tablet servers: " + GetStatusStringSet(errors));
}

// Given two statuses, include messages from the first status in front and take state
// from whichever is not OK (if there is one), otherwise return the first status.
Status CombineStatuses(const Status& first_status, const Status& second_status) {
  if (!first_status.ok()) {
    return first_status.CloneAndAppend(second_status.message());
  } else if (!second_status.ok()) {
    return second_status.CloneAndPrepend(first_status.message());
  }
  return first_status;
}

} // namespace

//--------------------------------------------------------------------------------------------------
// Class PgSessionAsyncRunResult
//--------------------------------------------------------------------------------------------------
PgSessionAsyncRunResult::PgSessionAsyncRunResult(std::future<Status> future_status,
                                                 client::YBSessionPtr session)
    :  future_status_(std::move(future_status)),
       session_(std::move(session)) {
}

Status PgSessionAsyncRunResult::GetStatus() {
  DCHECK(InProgress());
  auto status = future_status_.get();
  future_status_ = std::future<Status>();
  return CombineErrorsToStatus(session_->GetPendingErrors(), status);
}

bool PgSessionAsyncRunResult::InProgress() const {
  return future_status_.valid();
}

//--------------------------------------------------------------------------------------------------
// Class PgSession::RunHelper
//--------------------------------------------------------------------------------------------------
PgSession::RunHelper::RunHelper(PgSession* pg_session, bool transactional)
    :  pg_session_(*pg_session),
       transactional_(transactional),
       buffered_ops_(transactional_ ? pg_session_.buffered_txn_write_ops_
                                    : pg_session_.buffered_write_ops_) {
}

Status PgSession::RunHelper::Apply(std::shared_ptr<client::YBPgsqlOp> op, uint64_t* read_time) {
  // If the operation is a write op and we are in buffered write mode,
  // save the op and return invalid future to indicate the op should not be flushed
  // except in bulk by FlushBufferedWriteOperations().
  //
  // We allow read ops while buffering writes because it can happen when building indexes for sys
  // catalog tables during initdb. Continuing read ops to scan the table can be issued while
  // writes to its index are being buffered.
  auto transactional_op = pg_session_.ShouldHandleTransactionally(*op);
  DCHECK_EQ(transactional_op, transactional_)
      << "All operations must be either transactional or non-transactional";
  if (!transactional_op) {
    pg_session_.InvalidateForeignKeyReferenceCache();
  }

  if (pg_session_.buffer_write_ops_ > 0 &&
      op->type() == YBOperation::Type::PGSQL_WRITE) {
    buffered_ops_.push_back(std::move(op));
    return Status::OK();
  }
  bool read_only = op->read_only();
  if (op->type() == YBOperation::Type::PGSQL_READ) {
    const auto& read_req = down_cast<client::YBPgsqlReadOp*>(op.get())->request();
    const bool has_row_marker = IsValidRowMarkType(GetRowMarkTypeFromPB(read_req));
    read_only = read_only && !has_row_marker;
  }
  // Get session for each operation individually to begin transaction if necessary
  // and print debug logs.
  auto session = VERIFY_RESULT(pg_session_.GetSession(transactional_, read_only));
  if (!yb_session_) {
    yb_session_ = session->shared_from_this();
    if (transactional_ && read_time) {
      if (!*read_time) {
        *read_time = pg_session_.clock_->Now().ToUint64();
      }
      yb_session_->SetInTxnLimit(HybridTime(*read_time));
    }
  } else {
    // Session can't be changed as all operations either transactional or non-transactional.
    DCHECK_EQ(yb_session_.get(), session);
  }
  return yb_session_->Apply(std::move(op));
}

Result<PgSessionAsyncRunResult> PgSession::RunHelper::Flush() {
  if (yb_session_) {
    auto future_status = MakeFuture<Status>([this](auto callback) {
      yb_session_->FlushAsync([callback](const Status& status) { callback(status); });
    });
    return PgSessionAsyncRunResult(std::move(future_status), std::move(yb_session_));
  }
  // All operations were buffered, no need to flush.
  return PgSessionAsyncRunResult();
}

//--------------------------------------------------------------------------------------------------
// Class PgSession
//--------------------------------------------------------------------------------------------------

PgSession::PgSession(
    client::YBClient* client,
    const string& database_name,
    scoped_refptr<PgTxnManager> pg_txn_manager,
    scoped_refptr<server::HybridClock> clock,
    const tserver::TServerSharedObject* tserver_shared_object)
    : client_(client),
      session_(client_->NewSession()),
      pg_txn_manager_(std::move(pg_txn_manager)),
      clock_(std::move(clock)),
      tserver_shared_object_(tserver_shared_object) {
  session_->SetTimeout(MonoDelta::FromMilliseconds(FLAGS_pg_yb_session_timeout_ms));
  session_->SetForceConsistentRead(client::ForceConsistentRead::kTrue);
}

PgSession::~PgSession() {
}

//--------------------------------------------------------------------------------------------------

void PgSession::Reset() {
  errmsg_.clear();
  fk_reference_cache_.clear();
  status_ = Status::OK();
}

Status PgSession::ConnectDatabase(const string& database_name) {
  connected_database_ = database_name;
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status PgSession::CreateDatabase(const string& database_name,
                                 const PgOid database_oid,
                                 const PgOid source_database_oid,
                                 const PgOid next_oid,
                                 const bool colocated) {
  return client_->CreateNamespace(database_name,
                                  YQL_DATABASE_PGSQL,
                                  "" /* creator_role_name */,
                                  GetPgsqlNamespaceId(database_oid),
                                  source_database_oid != kPgInvalidOid
                                  ? GetPgsqlNamespaceId(source_database_oid) : "",
                                  next_oid,
                                  colocated);
}

Status PgSession::DropDatabase(const string& database_name, PgOid database_oid) {
  RETURN_NOT_OK(client_->DeleteNamespace(database_name,
                                         YQL_DATABASE_PGSQL,
                                         GetPgsqlNamespaceId(database_oid)));
  RETURN_NOT_OK(DeleteDBSequences(database_oid));
  return Status::OK();
}

client::YBNamespaceAlterer* PgSession::NewNamespaceAlterer(
    const std::string& namespace_name, PgOid database_oid) {
  return client_->NewNamespaceAlterer(namespace_name, GetPgsqlNamespaceId(database_oid));
}

Status PgSession::ReserveOids(const PgOid database_oid,
                              const PgOid next_oid,
                              const uint32_t count,
                              PgOid *begin_oid,
                              PgOid *end_oid) {
  return client_->ReservePgsqlOids(GetPgsqlNamespaceId(database_oid), next_oid, count,
                                   begin_oid, end_oid);
}

Status PgSession::GetCatalogMasterVersion(uint64_t *version) {
  return client_->GetYsqlCatalogMasterVersion(version);
}

Status PgSession::CreateSequencesDataTable() {
  const YBTableName table_name(YQL_DATABASE_PGSQL,
                               kPgSequencesDataNamespaceId,
                               kPgSequencesNamespaceName,
                               kPgSequencesDataTableName);
  RETURN_NOT_OK(client_->CreateNamespaceIfNotExists(kPgSequencesNamespaceName,
                                                    YQLDatabase::YQL_DATABASE_PGSQL,
                                                    "" /* creator_role_name */,
                                                    kPgSequencesDataNamespaceId));

  // Set up the schema.
  client::YBSchemaBuilder schemaBuilder;
  schemaBuilder.AddColumn(kPgSequenceDbOidColName)->HashPrimaryKey()->Type(yb::INT64)->NotNull();
  schemaBuilder.AddColumn(kPgSequenceSeqOidColName)->HashPrimaryKey()->Type(yb::INT64)->NotNull();
  schemaBuilder.AddColumn(kPgSequenceLastValueColName)->Type(yb::INT64)->NotNull();
  schemaBuilder.AddColumn(kPgSequenceIsCalledColName)->Type(yb::BOOL)->NotNull();
  client::YBSchema schema;
  CHECK_OK(schemaBuilder.Build(&schema));

  // Generate the table id.
  pggate::PgObjectId oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);

  // Try to create the table.
  std::unique_ptr<yb::client::YBTableCreator> table_creator(client_->NewTableCreator());

  Status s = table_creator->table_name(table_name)
      .schema(&schema)
      .table_type(yb::client::YBTableType::PGSQL_TABLE_TYPE)
      .table_id(oid.GetYBTableId())
      .hash_schema(YBHashSchema::kPgsqlHash)
      .Create();
  // If we could create it, then all good!
  if (s.ok()) {
    LOG(INFO) << "Table '" << table_name.ToString() << "' created.";
    // If the table was already there, also not an error...
  } else if (s.IsAlreadyPresent()) {
    LOG(INFO) << "Table '" << table_name.ToString() << "' already exists";
  } else {
    // If any other error, report that!
    LOG(ERROR) << "Error creating table '" << table_name.ToString() << "': " << s;
    RETURN_NOT_OK(s);
  }
  return Status::OK();
}

Status PgSession::InsertSequenceTuple(int64_t db_oid,
                                      int64_t seq_oid,
                                      uint64_t ysql_catalog_version,
                                      int64_t last_val,
                                      bool is_called) {
  pggate::PgObjectId oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
  auto result = LoadTable(oid);
  if (!result.ok()) {
    RETURN_NOT_OK(CreateSequencesDataTable());
    // Try one more time.
    result = LoadTable(oid);
  }
  PgTableDesc::ScopedRefPtr t = VERIFY_RESULT(result);

  std::shared_ptr<client::YBPgsqlWriteOp> psql_write;
  psql_write.reset(t->NewPgsqlInsert());

  auto write_request = psql_write->mutable_request();
  write_request->set_ysql_catalog_version(ysql_catalog_version);

  write_request->add_partition_column_values()->mutable_value()->set_int64_value(db_oid);
  write_request->add_partition_column_values()->mutable_value()->set_int64_value(seq_oid);

  PgsqlColumnValuePB* column_value = write_request->add_column_values();
  column_value->set_column_id(t->table()->schema().ColumnId(kPgSequenceLastValueColIdx));
  column_value->mutable_expr()->mutable_value()->set_int64_value(last_val);

  column_value = write_request->add_column_values();
  column_value->set_column_id(t->table()->schema().ColumnId(kPgSequenceIsCalledColIdx));
  column_value->mutable_expr()->mutable_value()->set_bool_value(is_called);

  return session_->ApplyAndFlush(psql_write);
}

Status PgSession::UpdateSequenceTuple(int64_t db_oid,
                                      int64_t seq_oid,
                                      uint64_t ysql_catalog_version,
                                      int64_t last_val,
                                      bool is_called,
                                      boost::optional<int64_t> expected_last_val,
                                      boost::optional<bool> expected_is_called,
                                      bool* skipped) {
  pggate::PgObjectId oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
  PgTableDesc::ScopedRefPtr t = VERIFY_RESULT(LoadTable(oid));

  std::shared_ptr<client::YBPgsqlWriteOp> psql_write;
  psql_write.reset(t->NewPgsqlUpdate());

  auto write_request = psql_write->mutable_request();
  write_request->set_ysql_catalog_version(ysql_catalog_version);

  write_request->add_partition_column_values()->mutable_value()->set_int64_value(db_oid);
  write_request->add_partition_column_values()->mutable_value()->set_int64_value(seq_oid);

  PgsqlColumnValuePB* column_value = write_request->add_column_new_values();
  column_value->set_column_id(t->table()->schema().ColumnId(kPgSequenceLastValueColIdx));
  column_value->mutable_expr()->mutable_value()->set_int64_value(last_val);

  column_value = write_request->add_column_new_values();
  column_value->set_column_id(t->table()->schema().ColumnId(kPgSequenceIsCalledColIdx));
  column_value->mutable_expr()->mutable_value()->set_bool_value(is_called);

  auto where_pb = write_request->mutable_where_expr()->mutable_condition();

  if (expected_last_val && expected_is_called) {
    // WHERE clause => WHERE last_val == expected_last_val AND is_called == expected_is_called.
    where_pb->set_op(QL_OP_AND);

    auto cond = where_pb->add_operands()->mutable_condition();
    cond->set_op(QL_OP_EQUAL);
    cond->add_operands()->set_column_id(t->table()->schema().ColumnId(kPgSequenceLastValueColIdx));
    cond->add_operands()->mutable_value()->set_int64_value(*expected_last_val);

    cond = where_pb->add_operands()->mutable_condition();
    cond->set_op(QL_OP_EQUAL);
    cond->add_operands()->set_column_id(t->table()->schema().ColumnId(kPgSequenceIsCalledColIdx));
    cond->add_operands()->mutable_value()->set_bool_value(*expected_is_called);
  } else {
    where_pb->set_op(QL_OP_EXISTS);
  }

  write_request->mutable_column_refs()->add_ids(
      t->table()->schema().ColumnId(kPgSequenceLastValueColIdx));
  write_request->mutable_column_refs()->add_ids(
      t->table()->schema().ColumnId(kPgSequenceIsCalledColIdx));

  RETURN_NOT_OK(session_->ApplyAndFlush(psql_write));
  if (skipped) {
    *skipped = psql_write->response().skipped();
  }
  return Status::OK();
}

Status PgSession::ReadSequenceTuple(int64_t db_oid,
                                    int64_t seq_oid,
                                    uint64_t ysql_catalog_version,
                                    int64_t *last_val,
                                    bool *is_called) {
  pggate::PgObjectId oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
  PgTableDesc::ScopedRefPtr t = VERIFY_RESULT(LoadTable(oid));

  std::shared_ptr<client::YBPgsqlReadOp> psql_read(t->NewPgsqlSelect());

  auto read_request = psql_read->mutable_request();
  read_request->set_ysql_catalog_version(ysql_catalog_version);

  read_request->add_partition_column_values()->mutable_value()->set_int64_value(db_oid);
  read_request->add_partition_column_values()->mutable_value()->set_int64_value(seq_oid);

  read_request->add_targets()->set_column_id(
      t->table()->schema().ColumnId(kPgSequenceLastValueColIdx));
  read_request->add_targets()->set_column_id(
      t->table()->schema().ColumnId(kPgSequenceIsCalledColIdx));

  read_request->mutable_column_refs()->add_ids(
      t->table()->schema().ColumnId(kPgSequenceLastValueColIdx));
  read_request->mutable_column_refs()->add_ids(
      t->table()->schema().ColumnId(kPgSequenceIsCalledColIdx));

  RETURN_NOT_OK(session_->ReadSync(psql_read));

  Slice cursor;
  int64_t row_count = 0;
  RETURN_NOT_OK(PgDocData::LoadCache(psql_read->rows_data(), &row_count, &cursor));
  if (row_count == 0) {
    return STATUS_SUBSTITUTE(NotFound, "Unable to find relation for sequence $0", seq_oid);
  }

  PgWireDataHeader header = PgDocData::ReadDataHeader(&cursor);
  if (header.is_null()) {
    return STATUS_SUBSTITUTE(NotFound, "Unable to find relation for sequence $0", seq_oid);
  }
  size_t read_size = PgDocData::ReadNumber(&cursor, last_val);
  cursor.remove_prefix(read_size);

  header = PgDocData::ReadDataHeader(&cursor);
  if (header.is_null()) {
    return STATUS_SUBSTITUTE(NotFound, "Unable to find relation for sequence $0", seq_oid);
  }
  read_size = PgDocData::ReadNumber(&cursor, is_called);
  return Status::OK();
}

Status PgSession::DeleteSequenceTuple(int64_t db_oid, int64_t seq_oid) {
  pggate::PgObjectId oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
  PgTableDesc::ScopedRefPtr t = VERIFY_RESULT(LoadTable(oid));

  std::shared_ptr<client::YBPgsqlWriteOp> psql_delete(t->NewPgsqlDelete());
  auto delete_request = psql_delete->mutable_request();

  delete_request->add_partition_column_values()->mutable_value()->set_int64_value(db_oid);
  delete_request->add_partition_column_values()->mutable_value()->set_int64_value(seq_oid);

  return session_->ApplyAndFlush(psql_delete);
}

Status PgSession::DeleteDBSequences(int64_t db_oid) {
  pggate::PgObjectId oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
  Result<PgTableDesc::ScopedRefPtr> r = LoadTable(oid);
  if (!r.ok()) {
    // Sequence table is not yet created.
    return Status::OK();
  }

  PgTableDesc::ScopedRefPtr t = CHECK_RESULT(r);
  if (t == nullptr) {
    return Status::OK();
  }

  std::shared_ptr<client::YBPgsqlWriteOp> psql_delete(t->NewPgsqlDelete());
  auto delete_request = psql_delete->mutable_request();

  delete_request->add_partition_column_values()->mutable_value()->set_int64_value(db_oid);
  return session_->ApplyAndFlush(psql_delete);
}

//--------------------------------------------------------------------------------------------------

unique_ptr<client::YBTableCreator> PgSession::NewTableCreator() {
  return client_->NewTableCreator();
}

unique_ptr<client::YBTableAlterer> PgSession::NewTableAlterer(const YBTableName& table_name) {
  return client_->NewTableAlterer(table_name);
}

unique_ptr<client::YBTableAlterer> PgSession::NewTableAlterer(const string table_id) {
  return client_->NewTableAlterer(table_id);
}

Status PgSession::DropTable(const PgObjectId& table_id) {
  return client_->DeleteTable(table_id.GetYBTableId());
}

Status PgSession::DropIndex(const PgObjectId& index_id) {
  return client_->DeleteIndexTable(index_id.GetYBTableId());
}

Status PgSession::TruncateTable(const PgObjectId& table_id) {
  return client_->TruncateTable(table_id.GetYBTableId());
}

//--------------------------------------------------------------------------------------------------

Result<PgTableDesc::ScopedRefPtr> PgSession::LoadTable(const PgObjectId& table_id) {
  VLOG(3) << "Loading table descriptor for " << table_id;
  const TableId yb_table_id = table_id.GetYBTableId();
  shared_ptr<YBTable> table;

  auto cached_yb_table = table_cache_.find(yb_table_id);
  if (cached_yb_table == table_cache_.end()) {
    Status s = client_->OpenTable(yb_table_id, &table);
    if (!s.ok()) {
      VLOG(3) << "LoadTable: Server returns an error: " << s;
      // TODO: NotFound might not always be the right status here.
      return STATUS_FORMAT(NotFound, "Error loading table with oid $0 in database with oid $1: $2",
                           table_id.object_oid, table_id.database_oid, s.ToUserMessage());
    }
    table_cache_[yb_table_id] = table;
  } else {
    table = cached_yb_table->second;
  }

  DCHECK_EQ(table->table_type(), YBTableType::PGSQL_TABLE_TYPE);

  return make_scoped_refptr<PgTableDesc>(table);
}

void PgSession::InvalidateTableCache(const PgObjectId& table_id) {
  const TableId yb_table_id = table_id.GetYBTableId();
  table_cache_.erase(yb_table_id);
}

Status PgSession::StartBufferingWriteOperations() {
  buffer_write_ops_++;
  return Status::OK();
}

Status PgSession::FlushBufferedWriteOperations(PgsqlOpBuffer* write_ops, bool transactional) {
  Status final_status;
  if (!write_ops->empty()) {
    if (transactional) {
      DCHECK(!YBCIsInitDbModeEnvVarSet());
    }

    auto session =
      VERIFY_RESULT(GetSession(transactional, false /* read_only_op */))->shared_from_this();

    int num_writes = 0;
    for (auto it = write_ops->begin(); it != write_ops->end(); ++it) {
      DCHECK_EQ(ShouldHandleTransactionally(**it), transactional)
          << "Table name: " << it->get()->table()->name().ToString()
          << ", table is transactional: "
          << it->get()->table()->schema().table_properties().is_transactional()
          << ", initdb mode: " << YBCIsInitDbModeEnvVarSet();
      RETURN_NOT_OK(session->Apply(*it));
      num_writes++;

      // Flush and wait for batch to complete if reached max batch size or on final iteration.
      if (num_writes >= FLAGS_ysql_session_max_batch_size || std::next(it) == write_ops->end()) {
        Synchronizer sync;
        StatusFunctor callback = sync.AsStatusFunctor();
        session->FlushAsync([session, callback] (const Status& status) {
                              callback(CombineErrorsToStatus(session->GetPendingErrors(), status));
                            });
        Status s = sync.Wait();
        final_status = CombineStatuses(final_status, s);
        num_writes = 0;
      }
    }
    for (auto it = write_ops->begin(); it != write_ops->end(); ++it) {
      // Handle any QL errors from individual ops.
      std::shared_ptr<client::YBPgsqlOp> op = *it;
      if (!op->succeeded()) {
        const auto& response = op->response();
        YBPgErrorCode pg_error_code = YBPgErrorCode::YB_PG_INTERNAL_ERROR;
        if (response.has_pg_error_code()) {
          pg_error_code = static_cast<YBPgErrorCode>(response.pg_error_code());
        }

        Status s;
        if (response.status() == PgsqlResponsePB::PGSQL_STATUS_DUPLICATE_KEY_ERROR) {
          s = STATUS(AlreadyPresent, op->response().error_message(), Slice(),
                     PgsqlError(pg_error_code));
        } else {
          s = STATUS(QLError, op->response().error_message(), Slice(),
                     PgsqlError(pg_error_code));
        }
        final_status = CombineStatuses(final_status, s);
      }
    }
    write_ops->clear();
  }
  return final_status;
}

Status PgSession::FlushBufferedWriteOperations() {
  CHECK_GT(buffer_write_ops_, 0);
  if (--buffer_write_ops_ > 0) {
    return Status::OK();
  }
  Status final_status;
  Status s;
  s = FlushBufferedWriteOperations(&buffered_write_ops_, false /* transactional */);
  final_status = CombineStatuses(final_status, s);
  if (YBCIsInitDbModeEnvVarSet()) {
    // No transactional operations are expected in the initdb mode.
    DCHECK_EQ(buffered_txn_write_ops_.size(), 0);
  } else {
    s = FlushBufferedWriteOperations(&buffered_txn_write_ops_, true /* transactional */);
    final_status = CombineStatuses(final_status, s);
  }
  return final_status;
}

bool PgSession::ShouldHandleTransactionally(const client::YBPgsqlOp& op) {
  return op.IsTransactional() &&  !YBCIsInitDbModeEnvVarSet() &&
         (!op.IsYsqlCatalogOp() || pg_txn_manager_->IsDdlMode() ||
             // In this mode, used for some tests, we will execute direct statements on YSQL system
             // catalog tables in the user-controlled transaction, as opposed to executing them
             // non-transactionally.
             FLAGS_ysql_enable_manual_sys_table_txn_ctl);
}

Result<YBSession*> PgSession::GetSession(bool transactional, bool read_only_op) {
  if (transactional) {
    YBSession* txn_session = VERIFY_RESULT(pg_txn_manager_->GetTransactionalSession());
    RETURN_NOT_OK(pg_txn_manager_->BeginWriteTransactionIfNecessary(read_only_op));
    VLOG(2) << __PRETTY_FUNCTION__
            << ": read_only_op=" << read_only_op << ", returning transactional session: "
            << txn_session;
    return txn_session;
  }
  VLOG(2) << __PRETTY_FUNCTION__
          << ": read_only_op=" << read_only_op << ", returning non-transactional session "
          << session_.get();
  return session_.get();
}

Result<bool> PgSession::IsInitDbDone() {
  HostPort master_leader_host_port = client_->GetMasterLeaderAddress();
  auto proxy  = std::make_shared<MasterServiceProxy>(
      &client_->proxy_cache(), master_leader_host_port);
  rpc::RpcController rpc;
  IsInitDbDoneRequestPB req;
  IsInitDbDoneResponsePB resp;
  RETURN_NOT_OK(proxy->IsInitDbDone(req, &resp, &rpc));
  if (resp.has_error()) {
    return STATUS_FORMAT(
        RuntimeError,
        "IsInitDbDone RPC response hit error: $0",
        resp.error().ShortDebugString());
  }
  if (resp.done() && resp.has_initdb_error() && !resp.initdb_error().empty()) {
    return STATUS_FORMAT(RuntimeError, "initdb failed: $0", resp.initdb_error());
  }
  VLOG(1) << "IsInitDbDone response: " << resp.ShortDebugString();
  // We return true if initdb finished running, as well as if we know that it created the first
  // table (pg_proc) to make initdb idempotent on upgrades.
  return resp.done() || resp.pg_proc_exists();
}

Result<uint64_t> PgSession::GetSharedCatalogVersion() {
  if (tserver_shared_object_) {
    return (**tserver_shared_object_).ysql_catalog_version();
  } else {
    return STATUS(NotSupported, "Tablet server shared memory has not been opened");
  }
}

bool PgSession::ForeignKeyReferenceExists(uint32_t table_id, std::string&& ybctid) {
  PgForeignKeyReference reference = {table_id, std::move(ybctid)};
  return fk_reference_cache_.find(reference) != fk_reference_cache_.end();
}

Status PgSession::CacheForeignKeyReference(uint32_t table_id, std::string&& ybctid) {
  PgForeignKeyReference reference = {table_id, std::move(ybctid)};
  fk_reference_cache_.emplace(reference);
  return Status::OK();
}

Status PgSession::DeleteForeignKeyReference(uint32_t table_id, std::string&& ybctid) {
  PgForeignKeyReference reference = {table_id, std::move(ybctid)};
  fk_reference_cache_.erase(reference);
  return Status::OK();
}

}  // namespace pggate
}  // namespace yb
