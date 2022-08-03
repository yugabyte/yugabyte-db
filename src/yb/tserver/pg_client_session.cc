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

#include "yb/tserver/pg_client_session.h"

#include <mutex>

#include "yb/client/batcher.h"
#include "yb/client/client.h"
#include "yb/client/error.h"
#include "yb/client/namespace_alterer.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/transaction.h"
#include "yb/client/transaction_pool.h"
#include "yb/client/yb_op.h"

#include "yb/common/ql_type.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/transaction_error.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/casts.h"

#include "yb/rpc/rpc_context.h"

#include "yb/tserver/pg_client.pb.h"
#include "yb/tserver/pg_create_table.h"
#include "yb/tserver/pg_table_cache.h"

#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/string_util.h"
#include "yb/util/yb_pg_errcodes.h"

#include "yb/yql/pggate/util/pg_doc_data.h"

DECLARE_bool(ysql_serializable_isolation_for_ddl_txn);

namespace yb {
namespace tserver {

namespace {

constexpr const size_t kPgSequenceLastValueColIdx = 2;
constexpr const size_t kPgSequenceIsCalledColIdx = 3;

std::string SessionLogPrefix(uint64_t id) {
  return Format("S $0: ", id);
}

string GetStatusStringSet(const client::CollectedErrors& errors) {
  std::set<string> status_strings;
  for (const auto& error : errors) {
    status_strings.insert(error->status().ToString());
  }
  return RangeToString(status_strings.begin(), status_strings.end());
}

bool IsHomogeneousErrors(const client::CollectedErrors& errors) {
  if (errors.size() < 2) {
    return true;
  }
  auto i = errors.begin();
  const auto& status = (**i).status();
  const auto codes = status.ErrorCodesSlice();
  for (++i; i != errors.end(); ++i) {
    const auto& s = (**i).status();
    if (s.code() != status.code() || codes != s.ErrorCodesSlice()) {
      return false;
    }
  }
  return true;
}

boost::optional<YBPgErrorCode> PsqlErrorCode(const Status& status) {
  const uint8_t* err_data = status.ErrorData(PgsqlErrorTag::kCategory);
  if (err_data) {
    return PgsqlErrorTag::Decode(err_data);
  }
  return boost::none;
}

// Get a common Postgres error code from the status and all errors, and append it to a previous
// Status.
// If any of those have different conflicting error codes, previous result is returned as-is.
Status AppendPsqlErrorCode(const Status& status,
                                   const client::CollectedErrors& errors) {
  boost::optional<YBPgErrorCode> common_psql_error =  boost::make_optional(false, YBPgErrorCode());
  for(const auto& error : errors) {
    const auto psql_error = PsqlErrorCode(error->status());
    if (!common_psql_error) {
      common_psql_error = psql_error;
    } else if (psql_error && common_psql_error != psql_error) {
      common_psql_error = boost::none;
      break;
    }
  }
  return common_psql_error ? status.CloneAndAddErrorCode(PgsqlError(*common_psql_error)) : status;
}

// Get a common transaction error code for all the errors and append it to the previous Status.
Status AppendTxnErrorCode(const Status& status, const client::CollectedErrors& errors) {
  TransactionErrorCode common_txn_error = TransactionErrorCode::kNone;
  for (const auto& error : errors) {
    const TransactionErrorCode txn_error = TransactionError(error->status()).value();
    if (txn_error == TransactionErrorCode::kNone ||
        txn_error == common_txn_error) {
      continue;
    }
    if (common_txn_error == TransactionErrorCode::kNone) {
      common_txn_error = txn_error;
      continue;
    }
    // If we receive a list of errors, with one as kConflict and others as kAborted, we retain the
    // error as kConflict, since in case of a batched request the first operation would receive the
    // kConflict and all the others would receive the kAborted error.
    if ((txn_error == TransactionErrorCode::kConflict &&
         common_txn_error == TransactionErrorCode::kAborted) ||
        (txn_error == TransactionErrorCode::kAborted &&
         common_txn_error == TransactionErrorCode::kConflict)) {
      common_txn_error = TransactionErrorCode::kConflict;
      continue;
    }

    // In all the other cases, reset the common_txn_error to kNone.
    common_txn_error = TransactionErrorCode::kNone;
    break;
  }

  return (common_txn_error != TransactionErrorCode::kNone) ?
    status.CloneAndAddErrorCode(TransactionError(common_txn_error)) : status;
}

// Given a set of errors from operations, this function attempts to combine them into one status
// that is later passed to PostgreSQL and further converted into a more specific error code.
Status CombineErrorsToStatus(const client::CollectedErrors& errors, const Status& status) {
  if (errors.empty())
    return status;

  if (status.IsIOError() &&
      // TODO: move away from string comparison here and use a more specific status than IOError.
      // See https://github.com/YugaByte/yugabyte-db/issues/702
      status.message() == client::internal::Batcher::kErrorReachingOutToTServersMsg &&
      IsHomogeneousErrors(errors)) {
    const auto& result = errors.front()->status();
    if (errors.size() == 1) {
      return result;
    }
    return Status(result.code(),
                  __FILE__,
                  __LINE__,
                  GetStatusStringSet(errors),
                  result.ErrorCodesSlice(),
                  DupFileName::kFalse);
  }

  Status result =
    status.ok()
    ? STATUS(InternalError, GetStatusStringSet(errors))
    : status.CloneAndAppend(". Errors from tablet servers: " + GetStatusStringSet(errors));

  return AppendTxnErrorCode(AppendPsqlErrorCode(result, errors), errors);
}

Status ProcessUsedReadTime(uint64_t session_id,
                           const client::YBPgsqlOp& op,
                           PgPerformResponsePB* resp,
                           const PgClientSession::UsedReadTimePtr& used_read_time_weak_ptr) {
  const auto op_used_read_time = op.type() == client::YBOperation::PGSQL_READ
      ? down_cast<const client::YBPgsqlReadOp&>(op).used_read_time()
      : ReadHybridTime();
  if (!op_used_read_time) {
    return Status::OK();
  }

  if (op.table()->schema().table_properties().is_ysql_catalog_table()) {
    // Non empty used_read_time field in catalog read operation means this is the very first
    // catalog read operation after catalog read time resetting. read_time for the operation
    // has been chosen by master. All further reads from catalog must use same read point.
    auto catalog_read_time = op_used_read_time;

    // We set global limit to local limit to avoid read restart errors because they are
    // disruptive to system catalog reads and it is not always possible to handle them there.
    // This might lead to reading slightly outdated state of the system catalog if a recently
    // committed DDL transaction used a transaction status tablet whose leader's clock is skewed
    // and is in the future compared to the master leader's clock.
    // TODO(dmitry) This situation will be handled in context of #7964.
    catalog_read_time.global_limit = catalog_read_time.local_limit;
    catalog_read_time.ToPB(resp->mutable_catalog_read_time());
  }

  auto used_read_time_ptr = used_read_time_weak_ptr.lock();
  if (used_read_time_ptr) {
    auto& used_read_time = *used_read_time_ptr;
    {
      std::lock_guard<simple_spinlock> guard(used_read_time.lock);
      if (PREDICT_FALSE(static_cast<bool>(used_read_time.value))) {
        return STATUS_FORMAT(IllegalState,
                             "Session read time already set $0 used read time is $1",
                             used_read_time.value,
                             op_used_read_time);
      }
      used_read_time.value = op_used_read_time;
    }
    VLOG(3) << SessionLogPrefix(session_id) << "Update used read time: " << op_used_read_time;
  }
  return Status::OK();
}

Status HandleResponse(uint64_t session_id,
                      const client::YBPgsqlOp& op,
                      PgPerformResponsePB* resp,
                      const PgClientSession::UsedReadTimePtr& used_read_time) {
  const auto& response = op.response();
  if (response.status() == PgsqlResponsePB::PGSQL_STATUS_OK) {
    return ProcessUsedReadTime(session_id, op, resp, used_read_time);
  }

  auto status = STATUS(
      QLError, response.error_message(), Slice(), PgsqlRequestStatus(response.status()));

  if (response.has_pg_error_code()) {
    status = status.CloneAndAddErrorCode(
        PgsqlError(static_cast<YBPgErrorCode>(response.pg_error_code())));
  }

  if (response.has_txn_error_code()) {
    status = status.CloneAndAddErrorCode(
        TransactionError(static_cast<TransactionErrorCode>(response.txn_error_code())));
  }

  return status;
}

Status GetTable(const TableId& table_id, PgTableCache* cache, client::YBTablePtr* table) {
  if (*table && (**table).id() == table_id) {
    return Status::OK();
  }
  *table = VERIFY_RESULT(cache->Get(table_id));
  return Status::OK();
}

Result<PgClientSessionOperations> PrepareOperations(
    const PgPerformRequestPB& req, client::YBSession* session, PgTableCache* table_cache) {
  auto write_time = HybridTime::FromPB(req.write_time());
  std::vector<std::shared_ptr<client::YBPgsqlOp>> ops;
  ops.reserve(req.ops().size());
  client::YBTablePtr table;
  bool finished = false;
  auto se = ScopeExit([&finished, session] {
    if (!finished) {
      session->Abort();
    }
  });
  for (const auto& op : req.ops()) {
    if (op.has_read()) {
      const auto& read = op.read();
      RETURN_NOT_OK(GetTable(read.table_id(), table_cache, &table));
      const auto read_op = std::make_shared<client::YBPgsqlReadOp>(
          table, const_cast<PgsqlReadRequestPB*>(&read));
      if (op.read_from_followers()) {
        read_op->set_yb_consistency_level(YBConsistencyLevel::CONSISTENT_PREFIX);
      }
      ops.push_back(read_op);
      session->Apply(std::move(read_op));
    } else {
      const auto& write = op.write();
      RETURN_NOT_OK(GetTable(write.table_id(), table_cache, &table));
      const auto write_op = std::make_shared<client::YBPgsqlWriteOp>(
          table, const_cast<PgsqlWriteRequestPB*>(&write));
      if (write_time) {
        write_op->SetWriteTime(write_time);
        write_time = HybridTime::kInvalid;
      }
      ops.push_back(write_op);
      session->Apply(std::move(write_op));
    }
  }
  finished = true;
  return ops;
}

struct PerformData {
  uint64_t session_id;
  const PgPerformRequestPB* req;
  PgPerformResponsePB* resp;
  rpc::RpcContext context;
  PgClientSessionOperations ops;
  PgTableCache* table_cache;
  PgClientSession::UsedReadTimePtr used_read_time;

  void FlushDone(client::FlushStatus* flush_status) {
    auto status = CombineErrorsToStatus(flush_status->errors, flush_status->status);
    if (status.ok()) {
      status = ProcessResponse();
    }
    if (!status.ok()) {
      StatusToPB(status, resp->mutable_status());
    }
    context.RespondSuccess();
  }

  Status ProcessResponse() {
    int idx = 0;
    for (const auto& op : ops) {
      const auto status = HandleResponse(session_id, *op, resp, used_read_time);
      if (!status.ok()) {
        if (PgsqlRequestStatus(status) == PgsqlResponsePB::PGSQL_STATUS_SCHEMA_VERSION_MISMATCH) {
          table_cache->Invalidate(op->table()->id());
        }
        VLOG(2) << SessionLogPrefix(session_id) << "Failed op " << idx << ": " << status;
        return status.CloneAndAddErrorCode(OpIndex(idx));
      }
      const auto& req_op = req->ops()[idx];
      if (req_op.has_read() && req_op.read().is_for_backfill() &&
          op->response().is_backfill_batch_done()) {
        // After backfill table schema version is updated, so we reset cache in advance.
        table_cache->Invalidate(op->table()->id());
      }
      ++idx;
    }
    auto& responses = *resp->mutable_responses();
    responses.Reserve(narrow_cast<int>(ops.size()));
    for (const auto& op : ops) {
      auto& op_resp = *responses.Add();
      op_resp.Swap(op->mutable_response());
      if (op_resp.has_rows_data_sidecar()) {
        op_resp.set_rows_data_sidecar(narrow_cast<int>(context.AddRpcSidecar(op->rows_data())));
      }
    }

    return Status::OK();
  }
};

client::YBSessionPtr CreateSession(
    client::YBClient* client, const scoped_refptr<ClockBase>& clock) {
  auto result = std::make_shared<client::YBSession>(client, clock);
  result->SetForceConsistentRead(client::ForceConsistentRead::kTrue);
  result->set_allow_local_calls_in_curr_thread(false);
  return result;
}

} // namespace

PgClientSession::PgClientSession(
    client::YBClient* client, const scoped_refptr<ClockBase>& clock,
    std::reference_wrapper<const TransactionPoolProvider> transaction_pool_provider,
    PgTableCache* table_cache, uint64_t id)
    : client_(*client),
      clock_(clock),
      transaction_pool_provider_(transaction_pool_provider.get()),
      table_cache_(*table_cache), id_(id) {
}

uint64_t PgClientSession::id() const {
  return id_;
}

Status PgClientSession::CreateTable(
    const PgCreateTableRequestPB& req, PgCreateTableResponsePB* resp, rpc::RpcContext* context) {
  PgCreateTable helper(req);
  RETURN_NOT_OK(helper.Prepare());
  const auto* metadata = VERIFY_RESULT(GetDdlTransactionMetadata(
      req.use_transaction(), context->GetClientDeadline()));
  RETURN_NOT_OK(helper.Exec(&client(), metadata, context->GetClientDeadline()));
  VLOG_WITH_PREFIX(1) << __func__ << ": " << req.table_name();
  const auto& indexed_table_id = helper.indexed_table_id();
  if (indexed_table_id.IsValid()) {
    table_cache_.Invalidate(indexed_table_id.GetYbTableId());
  }
  return Status::OK();
}

Status PgClientSession::CreateDatabase(
    const PgCreateDatabaseRequestPB& req, PgCreateDatabaseResponsePB* resp,
    rpc::RpcContext* context) {
  return client().CreateNamespace(
      req.database_name(),
      YQL_DATABASE_PGSQL,
      "" /* creator_role_name */,
      GetPgsqlNamespaceId(req.database_oid()),
      req.source_database_oid() != kPgInvalidOid
          ? GetPgsqlNamespaceId(req.source_database_oid()) : "",
      req.next_oid(),
      VERIFY_RESULT(GetDdlTransactionMetadata(req.use_transaction(), context->GetClientDeadline())),
      req.colocated(),
      context->GetClientDeadline());
}

Status PgClientSession::DropDatabase(
    const PgDropDatabaseRequestPB& req, PgDropDatabaseResponsePB* resp, rpc::RpcContext* context) {
  return client().DeleteNamespace(
      req.database_name(),
      YQL_DATABASE_PGSQL,
      GetPgsqlNamespaceId(req.database_oid()),
      context->GetClientDeadline());
}

Status PgClientSession::DropTable(
    const PgDropTableRequestPB& req, PgDropTableResponsePB* resp, rpc::RpcContext* context) {
  const auto yb_table_id = PgObjectId::GetYbTableIdFromPB(req.table_id());
  if (req.index()) {
    client::YBTableName indexed_table;
    RETURN_NOT_OK(client().DeleteIndexTable(
        yb_table_id, &indexed_table, true, context->GetClientDeadline()));
    indexed_table.SetIntoTableIdentifierPB(resp->mutable_indexed_table());
    table_cache_.Invalidate(indexed_table.table_id());
    table_cache_.Invalidate(yb_table_id);
    return Status::OK();
  }

  RETURN_NOT_OK(client().DeleteTable(yb_table_id, true, context->GetClientDeadline()));
  table_cache_.Invalidate(yb_table_id);
  return Status::OK();
}

Status PgClientSession::AlterDatabase(
    const PgAlterDatabaseRequestPB& req, PgAlterDatabaseResponsePB* resp,
    rpc::RpcContext* context) {
  const auto alterer = client().NewNamespaceAlterer(
      req.database_name(), GetPgsqlNamespaceId(req.database_oid()));
  alterer->SetDatabaseType(YQL_DATABASE_PGSQL);
  alterer->RenameTo(req.new_name());
  return alterer->Alter(context->GetClientDeadline());
}

Status PgClientSession::AlterTable(
    const PgAlterTableRequestPB& req, PgAlterTableResponsePB* resp, rpc::RpcContext* context) {
  const auto table_id = PgObjectId::GetYbTableIdFromPB(req.table_id());
  const auto alterer = client().NewTableAlterer(table_id);
  const auto txn = VERIFY_RESULT(GetDdlTransactionMetadata(
      req.use_transaction(), context->GetClientDeadline()));
  if (txn) {
    alterer->part_of_transaction(txn);
  }
  for (const auto& add_column : req.add_columns()) {
    const auto yb_type = QLType::Create(static_cast<DataType>(add_column.attr_ybtype()));
    alterer->AddColumn(add_column.attr_name())
           ->Type(yb_type)->Order(add_column.attr_num())->PgTypeOid(add_column.attr_pgoid());
    // Do not set 'nullable' attribute as PgCreateTable::AddColumn() does not do it.
  }
  for (const auto& rename_column : req.rename_columns()) {
    alterer->AlterColumn(rename_column.old_name())->RenameTo(rename_column.new_name());
  }
  for (const auto& drop_column : req.drop_columns()) {
    alterer->DropColumn(drop_column);
  }
  if (!req.rename_table().table_name().empty()) {
    client::YBTableName new_table_name(
        YQL_DATABASE_PGSQL, req.rename_table().database_name(), req.rename_table().table_name());
    alterer->RenameTo(new_table_name);
  }

  alterer->timeout(context->GetClientDeadline() - CoarseMonoClock::now());
  RETURN_NOT_OK(alterer->Alter());
  table_cache_.Invalidate(table_id);
  return Status::OK();
}

Status PgClientSession::TruncateTable(
    const PgTruncateTableRequestPB& req, PgTruncateTableResponsePB* resp,
    rpc::RpcContext* context) {
  return client().TruncateTable(PgObjectId::GetYbTableIdFromPB(req.table_id()));
}

Status PgClientSession::BackfillIndex(
    const PgBackfillIndexRequestPB& req, PgBackfillIndexResponsePB* resp,
    rpc::RpcContext* context) {
  return client().BackfillIndex(
      PgObjectId::GetYbTableIdFromPB(req.table_id()), /* wait= */ true,
      context->GetClientDeadline());
}

Status PgClientSession::CreateTablegroup(
    const PgCreateTablegroupRequestPB& req, PgCreateTablegroupResponsePB* resp,
    rpc::RpcContext* context) {
  const auto id = PgObjectId::FromPB(req.tablegroup_id());
  auto tablespace_id = PgObjectId::FromPB(req.tablespace_id());
  auto s = client().CreateTablegroup(
      req.database_name(), GetPgsqlNamespaceId(id.database_oid),
      id.GetYbTablegroupId(),
      tablespace_id.IsValid() ? tablespace_id.GetYbTablespaceId() : "");
  if (s.ok()) {
    return Status::OK();
  }

  if (s.IsAlreadyPresent()) {
    return STATUS(InvalidArgument, "Duplicate tablegroup");
  }

  if (s.IsNotFound()) {
    return STATUS(InvalidArgument, "Database not found", req.database_name());
  }

  return STATUS_FORMAT(
      InvalidArgument, "Invalid table definition: $0",
      s.ToString(false /* include_file_and_line */, false /* include_code */));
}

Status PgClientSession::DropTablegroup(
    const PgDropTablegroupRequestPB& req, PgDropTablegroupResponsePB* resp,
    rpc::RpcContext* context) {
  const auto id = PgObjectId::FromPB(req.tablegroup_id());
  const auto status =
      client().DeleteTablegroup(GetPgsqlTablegroupId(id.database_oid, id.object_oid));
  if (status.IsNotFound()) {
    return Status::OK();
  }
  return status;
}

Status PgClientSession::RollbackToSubTransaction(
    const PgRollbackToSubTransactionRequestPB& req, PgRollbackToSubTransactionResponsePB* resp,
    rpc::RpcContext* context) {
  VLOG_WITH_PREFIX_AND_FUNC(2) << req.ShortDebugString();
  SCHECK(Transaction(PgClientSessionKind::kPlain), IllegalState,
         Format("Rollback sub transaction $0, when not transaction is running",
                req.sub_transaction_id()));
  return Transaction(PgClientSessionKind::kPlain)->RollbackToSubTransaction(
      req.sub_transaction_id(), context->GetClientDeadline());
}

Status PgClientSession::SetActiveSubTransaction(
    const PgSetActiveSubTransactionRequestPB& req, PgSetActiveSubTransactionResponsePB* resp,
    rpc::RpcContext* context) {
  VLOG_WITH_PREFIX_AND_FUNC(2) << req.ShortDebugString();

  if (req.has_options()) {
    RETURN_NOT_OK(BeginTransactionIfNecessary(req.options(), context->GetClientDeadline()));
    txn_serial_no_ = req.options().txn_serial_no();
  }

  SCHECK(Transaction(PgClientSessionKind::kPlain), IllegalState,
         Format("Set active sub transaction $0, when not transaction is running",
                req.sub_transaction_id()));

  Transaction(PgClientSessionKind::kPlain)->SetActiveSubTransaction(req.sub_transaction_id());
  return Status::OK();
}

Status PgClientSession::FinishTransaction(
    const PgFinishTransactionRequestPB& req, PgFinishTransactionResponsePB* resp,
    rpc::RpcContext* context) {
  saved_priority_ = boost::none;
  auto kind = req.ddl_mode() ? PgClientSessionKind::kDdl : PgClientSessionKind::kPlain;
  auto& txn = Transaction(kind);
  if (!txn) {
    VLOG_WITH_PREFIX_AND_FUNC(2) << "ddl: " << req.ddl_mode() << ", no running transaction";
    return Status::OK();
  }
  const auto txn_value = std::move(txn);
  Session(kind)->SetTransaction(nullptr);

  if (req.commit()) {
    const auto commit_status = txn_value->CommitFuture().get();
    VLOG_WITH_PREFIX_AND_FUNC(2)
        << "ddl: " << req.ddl_mode() << ", txn: " << txn_value->id()
        << ", commit: " << commit_status;
    return commit_status;
  }

  VLOG_WITH_PREFIX_AND_FUNC(2)
      << "ddl: " << req.ddl_mode() << ", txn: " << txn_value->id() << ", abort";
  txn_value->Abort();
  return Status::OK();
}

Status PgClientSession::Perform(
    const PgPerformRequestPB& req, PgPerformResponsePB* resp, rpc::RpcContext* context) {
  auto session_info = VERIFY_RESULT(SetupSession(req, context->GetClientDeadline()));
  auto* session = session_info.first;
  auto ops = VERIFY_RESULT(PrepareOperations(req, session, &table_cache_));
  auto data = std::make_shared<PerformData>(PerformData {
    .session_id = id_,
    .req = &req,
    .resp = resp,
    .context = std::move(*context),
    .ops = std::move(ops),
    .table_cache = &table_cache_,
    .used_read_time = session_info.second
  });
  session->FlushAsync([data](client::FlushStatus* flush_status) {
    data->FlushDone(flush_status);
  });
  return Status::OK();
}

void PgClientSession::ProcessReadTimeManipulation(ReadTimeManipulation manipulation) {
  switch (manipulation) {
    case ReadTimeManipulation::RESET: {
        // If a txn_ has been created, session_->read_point() returns the read point stored in txn_.
        ConsistentReadPoint* rp = Session(PgClientSessionKind::kPlain)->read_point();
        rp->SetCurrentReadTime();

        VLOG(1) << "Setting current ht as read point " << rp->GetReadTime();
      }
      return;
    case ReadTimeManipulation::RESTART: {
        ConsistentReadPoint* rp = Session(PgClientSessionKind::kPlain)->read_point();
        rp->Restart();

        VLOG(1) << "Restarted read point " << rp->GetReadTime();
      }
      return;
    case ReadTimeManipulation::NONE:
      return;
    case ReadTimeManipulation::ReadTimeManipulation_INT_MIN_SENTINEL_DO_NOT_USE_:
    case ReadTimeManipulation::ReadTimeManipulation_INT_MAX_SENTINEL_DO_NOT_USE_:
      break;
  }
  FATAL_INVALID_ENUM_VALUE(ReadTimeManipulation, manipulation);
}

Result<std::pair<client::YBSession*, PgClientSession::UsedReadTimePtr>>
PgClientSession::SetupSession(const PgPerformRequestPB& req, CoarseTimePoint deadline) {
  const auto& options = req.options();
  PgClientSessionKind kind;
  if (options.use_catalog_session()) {
    kind = PgClientSessionKind::kCatalog;
    EnsureSession(kind);
  } else if (options.ddl_mode()) {
    kind = PgClientSessionKind::kDdl;
    EnsureSession(kind);
    RETURN_NOT_OK(GetDdlTransactionMetadata(true /* use_transaction */, deadline));
  } else {
    kind = PgClientSessionKind::kPlain;
    RETURN_NOT_OK(BeginTransactionIfNecessary(options, deadline));
  }

  auto session = Session(kind).get();
  client::YBTransaction* transaction = Transaction(kind).get();

  VLOG_WITH_PREFIX(4) << __func__ << ": " << options.ShortDebugString();

  UsedReadTimePtr used_read_time;
  if (options.restart_transaction()) {
    if(options.ddl_mode()) {
      return STATUS(NotSupported, "Not supported to restart DDL transaction");
    }
    Transaction(kind) = VERIFY_RESULT(RestartTransaction(session, transaction));
    transaction = Transaction(kind).get();
  } else {
    RSTATUS_DCHECK(
        kind == PgClientSessionKind::kPlain ||
        options.read_time_manipulation() == ReadTimeManipulation::NONE,
        IllegalState,
        "Read time manipulation can't be specified for kDdl/ kCatalog transactions");
    ProcessReadTimeManipulation(options.read_time_manipulation());
    if (options.has_read_time() &&
        (options.read_time().has_read_ht() || options.use_catalog_session())) {
      const auto read_time = options.read_time().has_read_ht()
          ? ReadHybridTime::FromPB(options.read_time()) : ReadHybridTime();
      session->SetReadPoint(read_time);
      if (read_time) {
        VLOG_WITH_PREFIX(3) << "Read time: " << read_time;
      } else {
        VLOG_WITH_PREFIX(3) << "Reset read time: " << session->read_point()->GetReadTime();
      }
    } else if (!transaction &&
               (options.ddl_mode() || txn_serial_no_ != options.txn_serial_no())) {
      session->SetReadPoint(ReadHybridTime());
      if (kind == PgClientSessionKind::kPlain) {
        used_read_time = std::weak_ptr<UsedReadTime>(
            std::shared_ptr<UsedReadTime>(shared_from_this(), &plain_session_used_read_time_));
        std::lock_guard<simple_spinlock>  guard(plain_session_used_read_time_.lock);
        plain_session_used_read_time_.value = ReadHybridTime();
      }
      VLOG_WITH_PREFIX(3) << "Reset read time: " << session->read_point()->GetReadTime();
    } else {
      if (!transaction && kind == PgClientSessionKind::kPlain) {
        RETURN_NOT_OK(CheckPlainSessionReadTime());
      }
      VLOG_WITH_PREFIX(3) << "Keep read time: " << session->read_point()->GetReadTime();
    }
  }

  if (options.defer_read_point()) {
    // This call is idempotent, meaning it has no effect after the first call.
    session->DeferReadPoint();
  }

  if (!options.ddl_mode() && !options.use_catalog_session()) {
    txn_serial_no_ = options.txn_serial_no();

    const auto in_txn_limit = HybridTime::FromPB(options.in_txn_limit_ht());
    if (in_txn_limit) {
      VLOG_WITH_PREFIX(3) << "In txn limit: " << in_txn_limit;
      session->SetInTxnLimit(in_txn_limit);
    }
  }

  session->SetDeadline(deadline);

  return std::make_pair(session, used_read_time);
}

std::string PgClientSession::LogPrefix() {
  return SessionLogPrefix(id_);
}

Status PgClientSession::BeginTransactionIfNecessary(
    const PgPerformOptionsPB& options, CoarseTimePoint deadline) {
  const auto isolation = static_cast<IsolationLevel>(options.isolation());

  auto priority = options.priority();
  auto& session = EnsureSession(PgClientSessionKind::kPlain);
  auto& txn = Transaction(PgClientSessionKind::kPlain);
  if (txn && txn_serial_no_ != options.txn_serial_no()) {
    VLOG_WITH_PREFIX(2)
        << "Abort previous transaction, use existing priority: " << options.use_existing_priority()
        << ", new isolation: " << IsolationLevel_Name(isolation);

    if (options.use_existing_priority()) {
      saved_priority_ = txn->GetPriority();
    }
    txn->Abort();
    session->SetTransaction(nullptr);
    txn = nullptr;
  }

  if (isolation == IsolationLevel::NON_TRANSACTIONAL) {
    return Status::OK();
  }

  if (txn) {
    return txn->isolation() != isolation
        ? STATUS_FORMAT(
            IllegalState,
            "Attempt to change isolation level of running transaction from $0 to $1",
            txn->isolation(), isolation)
        : Status::OK();
  }

  txn = transaction_pool_provider_().Take(
      client::ForceGlobalTransaction(options.force_global_transaction()), deadline);
  if ((isolation == IsolationLevel::SNAPSHOT_ISOLATION ||
           isolation == IsolationLevel::READ_COMMITTED) &&
      txn_serial_no_ == options.txn_serial_no()) {
    RETURN_NOT_OK(CheckPlainSessionReadTime());
    txn->InitWithReadPoint(isolation, std::move(*session->read_point()));
    VLOG_WITH_PREFIX(2) << "Start transaction " << IsolationLevel_Name(isolation)
                        << ", id: " << txn->id()
                        << ", kept read time: " << txn->read_point().GetReadTime();
  } else {
    VLOG_WITH_PREFIX(2) << "Start transaction " << IsolationLevel_Name(isolation)
                        << ", id: " << txn->id()
                        << ", new read time";
    RETURN_NOT_OK(txn->Init(isolation));
  }
  if (saved_priority_) {
    priority = *saved_priority_;
    saved_priority_ = boost::none;
  }
  txn->SetPriority(priority);
  session->SetTransaction(txn);

  return Status::OK();
}

Result<const TransactionMetadata*> PgClientSession::GetDdlTransactionMetadata(
    bool use_transaction, CoarseTimePoint deadline) {
  if (!use_transaction) {
    return nullptr;
  }

  auto& txn = Transaction(PgClientSessionKind::kDdl);
  if (!txn) {
    const auto isolation = FLAGS_ysql_serializable_isolation_for_ddl_txn
        ? IsolationLevel::SERIALIZABLE_ISOLATION : IsolationLevel::SNAPSHOT_ISOLATION;
    txn = VERIFY_RESULT(transaction_pool_provider_().TakeAndInit(isolation, deadline));
    ddl_txn_metadata_ = VERIFY_RESULT(Copy(txn->GetMetadata(deadline).get()));
    EnsureSession(PgClientSessionKind::kDdl)->SetTransaction(txn);
  }

  return &ddl_txn_metadata_;
}

client::YBClient& PgClientSession::client() {
  return client_;
}

Result<client::YBTransactionPtr> PgClientSession::RestartTransaction(
    client::YBSession* session, client::YBTransaction* transaction) {
  if (!transaction) {
    SCHECK(session->IsRestartRequired(), IllegalState,
           "Attempted to restart when session does not require restart");

    const auto old_read_time = session->read_point()->GetReadTime();
    session->RestartNonTxnReadPoint(client::Restart::kTrue);
    const auto new_read_time = session->read_point()->GetReadTime();
    VLOG_WITH_PREFIX(3) << "Restarted read: " << old_read_time << " => " << new_read_time;
    LOG_IF_WITH_PREFIX(DFATAL, old_read_time == new_read_time)
        << "Read time did not change during restart: " << old_read_time << " => " << new_read_time;
    return nullptr;
  }

  if (!transaction->IsRestartRequired()) {
    return STATUS(IllegalState, "Attempted to restart when transaction does not require restart");
  }
  const auto result = VERIFY_RESULT(transaction->CreateRestartedTransaction());
  session->SetTransaction(result);
  VLOG_WITH_PREFIX(3) << "Restarted transaction";
  return result;
}

Status PgClientSession::InsertSequenceTuple(
    const PgInsertSequenceTupleRequestPB& req, PgInsertSequenceTupleResponsePB* resp,
    rpc::RpcContext* context) {
  PgObjectId table_oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
  auto result = table_cache_.Get(table_oid.GetYbTableId());
  if (!result.ok()) {
    RETURN_NOT_OK(CreateSequencesDataTable(&client_, context->GetClientDeadline()));
    // Try one more time.
    result = table_cache_.Get(table_oid.GetYbTableId());
  }
  auto table = VERIFY_RESULT(std::move(result));

  auto psql_write(client::YBPgsqlWriteOp::NewInsert(table));

  auto write_request = psql_write->mutable_request();
  write_request->set_ysql_catalog_version(req.ysql_catalog_version());

  write_request->add_partition_column_values()->mutable_value()->set_int64_value(req.db_oid());
  write_request->add_partition_column_values()->mutable_value()->set_int64_value(req.seq_oid());

  PgsqlColumnValuePB* column_value = write_request->add_column_values();
  column_value->set_column_id(table->schema().ColumnId(kPgSequenceLastValueColIdx));
  column_value->mutable_expr()->mutable_value()->set_int64_value(req.last_val());

  column_value = write_request->add_column_values();
  column_value->set_column_id(table->schema().ColumnId(kPgSequenceIsCalledColIdx));
  column_value->mutable_expr()->mutable_value()->set_bool_value(req.is_called());

  auto& session = EnsureSession(PgClientSessionKind::kSequence);
  session->SetDeadline(context->GetClientDeadline());
  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  return session->TEST_ApplyAndFlush(std::move(psql_write));
}

Status PgClientSession::UpdateSequenceTuple(
    const PgUpdateSequenceTupleRequestPB& req, PgUpdateSequenceTupleResponsePB* resp,
    rpc::RpcContext* context) {
  PgObjectId table_oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
  auto table = VERIFY_RESULT(table_cache_.Get(table_oid.GetYbTableId()));

  std::shared_ptr<client::YBPgsqlWriteOp> psql_write(client::YBPgsqlWriteOp::NewUpdate(table));

  auto write_request = psql_write->mutable_request();
  write_request->set_ysql_catalog_version(req.ysql_catalog_version());

  write_request->add_partition_column_values()->mutable_value()->set_int64_value(req.db_oid());
  write_request->add_partition_column_values()->mutable_value()->set_int64_value(req.seq_oid());

  PgsqlColumnValuePB* column_value = write_request->add_column_new_values();
  column_value->set_column_id(table->schema().ColumnId(kPgSequenceLastValueColIdx));
  column_value->mutable_expr()->mutable_value()->set_int64_value(req.last_val());

  column_value = write_request->add_column_new_values();
  column_value->set_column_id(table->schema().ColumnId(kPgSequenceIsCalledColIdx));
  column_value->mutable_expr()->mutable_value()->set_bool_value(req.is_called());

  auto where_pb = write_request->mutable_where_expr()->mutable_condition();

  if (req.has_expected()) {
    // WHERE clause => WHERE last_val == expected_last_val AND is_called == expected_is_called.
    where_pb->set_op(QL_OP_AND);

    auto cond = where_pb->add_operands()->mutable_condition();
    cond->set_op(QL_OP_EQUAL);
    cond->add_operands()->set_column_id(table->schema().ColumnId(kPgSequenceLastValueColIdx));
    cond->add_operands()->mutable_value()->set_int64_value(req.expected_last_val());

    cond = where_pb->add_operands()->mutable_condition();
    cond->set_op(QL_OP_EQUAL);
    cond->add_operands()->set_column_id(table->schema().ColumnId(kPgSequenceIsCalledColIdx));
    cond->add_operands()->mutable_value()->set_bool_value(req.expected_is_called());
  } else {
    where_pb->set_op(QL_OP_EXISTS);
  }

  // For compatibility set deprecated column_refs
  write_request->mutable_column_refs()->add_ids(
      table->schema().ColumnId(kPgSequenceLastValueColIdx));
  write_request->mutable_column_refs()->add_ids(
      table->schema().ColumnId(kPgSequenceIsCalledColIdx));
  // Same values, to be consumed by current TServers
  write_request->add_col_refs()->set_column_id(
      table->schema().ColumnId(kPgSequenceLastValueColIdx));
  write_request->add_col_refs()->set_column_id(
      table->schema().ColumnId(kPgSequenceIsCalledColIdx));

  auto& session = EnsureSession(PgClientSessionKind::kSequence);
  session->SetDeadline(context->GetClientDeadline());
  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  RETURN_NOT_OK(session->TEST_ApplyAndFlush(psql_write));
  resp->set_skipped(psql_write->response().skipped());
  return Status::OK();
}

Status PgClientSession::ReadSequenceTuple(
    const PgReadSequenceTupleRequestPB& req, PgReadSequenceTupleResponsePB* resp,
    rpc::RpcContext* context) {
  using pggate::PgDocData;
  using pggate::PgWireDataHeader;

  PgObjectId table_oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
  auto table = VERIFY_RESULT(table_cache_.Get(table_oid.GetYbTableId()));

  std::shared_ptr<client::YBPgsqlReadOp> psql_read(client::YBPgsqlReadOp::NewSelect(table));

  auto read_request = psql_read->mutable_request();
  read_request->set_ysql_catalog_version(req.ysql_catalog_version());

  read_request->add_partition_column_values()->mutable_value()->set_int64_value(req.db_oid());
  read_request->add_partition_column_values()->mutable_value()->set_int64_value(req.seq_oid());

  read_request->add_targets()->set_column_id(
      table->schema().ColumnId(kPgSequenceLastValueColIdx));
  read_request->add_targets()->set_column_id(
      table->schema().ColumnId(kPgSequenceIsCalledColIdx));

  // For compatibility set deprecated column_refs
  read_request->mutable_column_refs()->add_ids(
      table->schema().ColumnId(kPgSequenceLastValueColIdx));
  read_request->mutable_column_refs()->add_ids(
      table->schema().ColumnId(kPgSequenceIsCalledColIdx));
  // Same values, to be consumed by current TServers
  read_request->add_col_refs()->set_column_id(
      table->schema().ColumnId(kPgSequenceLastValueColIdx));
  read_request->add_col_refs()->set_column_id(
      table->schema().ColumnId(kPgSequenceIsCalledColIdx));

  auto& session = EnsureSession(PgClientSessionKind::kSequence);
  session->SetDeadline(context->GetClientDeadline());
  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  RETURN_NOT_OK(session->TEST_ReadSync(psql_read));

  Slice cursor;
  int64_t row_count = 0;
  PgDocData::LoadCache(psql_read->rows_data(), &row_count, &cursor);
  if (row_count == 0) {
    return STATUS_SUBSTITUTE(NotFound, "Unable to find relation for sequence $0", req.seq_oid());
  }

  PgWireDataHeader header = PgDocData::ReadDataHeader(&cursor);
  if (header.is_null()) {
    return STATUS_SUBSTITUTE(NotFound, "Unable to find relation for sequence $0", req.seq_oid());
  }
  int64_t last_val = 0;
  size_t read_size = PgDocData::ReadNumber(&cursor, &last_val);
  cursor.remove_prefix(read_size);
  resp->set_last_val(last_val);

  header = PgDocData::ReadDataHeader(&cursor);
  if (header.is_null()) {
    return STATUS_SUBSTITUTE(NotFound, "Unable to find relation for sequence $0", req.seq_oid());
  }
  bool is_called = false;
  read_size = PgDocData::ReadNumber(&cursor, &is_called);
  resp->set_is_called(is_called);
  return Status::OK();
}

Status PgClientSession::DeleteSequenceTuple(
    const PgDeleteSequenceTupleRequestPB& req, PgDeleteSequenceTupleResponsePB* resp,
    rpc::RpcContext* context) {
  PgObjectId table_oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
  auto table = VERIFY_RESULT(table_cache_.Get(table_oid.GetYbTableId()));

  auto psql_delete(client::YBPgsqlWriteOp::NewDelete(table));
  auto delete_request = psql_delete->mutable_request();

  delete_request->add_partition_column_values()->mutable_value()->set_int64_value(req.db_oid());
  delete_request->add_partition_column_values()->mutable_value()->set_int64_value(req.seq_oid());

  auto& session = EnsureSession(PgClientSessionKind::kSequence);
  session->SetDeadline(context->GetClientDeadline());
  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  return session->TEST_ApplyAndFlush(std::move(psql_delete));
}

Status PgClientSession::DeleteDBSequences(
    const PgDeleteDBSequencesRequestPB& req, PgDeleteDBSequencesResponsePB* resp,
    rpc::RpcContext* context) {
  PgObjectId table_oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
  auto table_res = table_cache_.Get(table_oid.GetYbTableId());
  if (!table_res.ok()) {
    // Sequence table is not yet created.
    return Status::OK();
  }

  auto table = std::move(*table_res);
  if (table == nullptr) {
    return Status::OK();
  }

  auto psql_delete(client::YBPgsqlWriteOp::NewDelete(table));
  auto delete_request = psql_delete->mutable_request();

  delete_request->add_partition_column_values()->mutable_value()->set_int64_value(req.db_oid());

  auto& session = EnsureSession(PgClientSessionKind::kSequence);
  session->SetDeadline(context->GetClientDeadline());
  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  return session->TEST_ApplyAndFlush(std::move(psql_delete));
}

client::YBSessionPtr& PgClientSession::EnsureSession(PgClientSessionKind kind) {
  auto& session = Session(kind);
  if (!session) {
    session = CreateSession(&client_, clock_);
  }
  return session;
}

client::YBSessionPtr& PgClientSession::Session(PgClientSessionKind kind) {
  return sessions_[to_underlying(kind)].session;
}

client::YBTransactionPtr& PgClientSession::Transaction(PgClientSessionKind kind) {
  return sessions_[to_underlying(kind)].transaction;
}

Status PgClientSession::CheckPlainSessionReadTime() {
  auto session = Session(PgClientSessionKind::kPlain);
  if (!session->read_point()->GetReadTime()) {
    ReadHybridTime used_read_time;
    {
      std::lock_guard<simple_spinlock> guard(plain_session_used_read_time_.lock);
      used_read_time = plain_session_used_read_time_.value;
    }
    RSTATUS_DCHECK(used_read_time, IllegalState, "Used read time is not set");
    session->SetReadPoint(used_read_time);
    VLOG_WITH_PREFIX(3)
        << "Update read time from used read time: " << session->read_point()->GetReadTime();
  }
  return Status::OK();
}

}  // namespace tserver
}  // namespace yb
