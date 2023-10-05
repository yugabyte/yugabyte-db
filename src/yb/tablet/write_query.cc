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

#include "yb/tablet/write_query.h"

#include "yb/client/client.h"
#include "yb/client/error.h"
#include "yb/client/meta_data_cache.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/transaction.h"
#include "yb/client/yb_op.h"

#include "yb/common/index.h"
#include "yb/common/row_mark.h"
#include "yb/common/schema.h"

#include "yb/docdb/conflict_resolution.h"
#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/cql_operation.h"
#include "yb/docdb/doc_write_batch.h"
#include "yb/docdb/pgsql_operation.h"
#include "yb/docdb/redis_operation.h"

#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/operations/write_operation.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metrics.h"
#include "yb/tablet/transaction_participant.h"
#include "yb/tablet/write_query_context.h"

#include "yb/tserver/tserver.pb.h"

#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/sync_point.h"
#include "yb/util/trace.h"
#include "yb/util/flags.h"

using namespace std::placeholders;
using namespace std::literals;

DEFINE_test_flag(bool, writequery_stuck_from_callback_leak, false,
    "Simulate WriteQuery stuck because of the update index flushed rpc call back leak");

namespace yb {
namespace tablet {

namespace {

// Separate Redis / QL / row operations write batches from write_request in preparation for the
// write transaction. Leave just the tablet id behind. Return Redis / QL / row operations, etc.
// in batch_request.
void SetupKeyValueBatch(const tserver::WriteRequestPB& client_request, LWWritePB* out_request) {
  out_request->ref_unused_tablet_id(""); // Backward compatibility.
  auto& out_write_batch = *out_request->mutable_write_batch();
  if (client_request.has_write_batch()) {
    out_write_batch = client_request.write_batch();
  }
  out_write_batch.set_deprecated_may_have_metadata(true);
  if (client_request.has_request_id()) {
    out_request->set_client_id1(client_request.client_id1());
    out_request->set_client_id2(client_request.client_id2());
    out_request->set_request_id(client_request.request_id());
    out_request->set_min_running_request_id(client_request.min_running_request_id());
    if (client_request.has_start_time_micros()) {
      out_request->set_start_time_micros(client_request.start_time_micros());
    }
  }
  out_request->set_batch_idx(client_request.batch_idx());
  // Actually, in production code, we could check for external hybrid time only when there are
  // no ql, pgsql, redis operations.
  // But in CDCServiceTest we have ql write batch with external time.
  if (client_request.has_external_hybrid_time()) {
    out_request->set_external_hybrid_time(client_request.external_hybrid_time());
  }
}

template <class Code, class Resp>
bool CheckSchemaVersion(
    TableInfo* table_info, int schema_version, bool compatible_with_previous_version, Code code,
    int index, Resp* resp_batch) {
  if (IsSchemaVersionCompatible(
          table_info->schema_version, schema_version, compatible_with_previous_version)) {
    return true;
  }

  VLOG(1) << " On " << table_info->table_name
           << " Setting status for write as YQL_STATUS_SCHEMA_VERSION_MISMATCH tserver's: "
           << table_info->schema_version << " vs req's : " << schema_version
           << " is req compatible with prev version: "
           << compatible_with_previous_version;
  while (index >= resp_batch->size()) {
    resp_batch->Add();
  }
  auto resp = resp_batch->Mutable(index);
  resp->Clear();
  resp->set_status(code);
  resp->set_error_message(Format(
      "schema version mismatch for table $0: expected $1, got $2 (compt with prev: $3)",
      table_info->table_id, table_info->schema_version, schema_version,
      compatible_with_previous_version));
  return false;
}

} // namespace

enum class WriteQuery::ExecuteMode {
  kSimple,
  kRedis,
  kCql,
  kPgsql,
};

WriteQuery::WriteQuery(
    int64_t term,
    CoarseTimePoint deadline,
    WriteQueryContext* context,
    TabletPtr tablet,
    rpc::RpcContext* rpc_context,
    tserver::WriteResponsePB* response,
    docdb::OperationKind kind)
    : operation_(std::make_unique<WriteOperation>(std::move(tablet))),
      term_(term),
      deadline_(deadline),
      context_(context),
      rpc_context_(rpc_context),
      response_(response),
      kind_(kind),
      start_time_(CoarseMonoClock::Now()),
      execute_mode_(ExecuteMode::kSimple) {
}

LWWritePB& WriteQuery::request() {
  return *operation_->mutable_request();
}

std::unique_ptr<WriteOperation> WriteQuery::PrepareSubmit() {
  operation_->set_completion_callback(
      [operation = operation_.get(), query = this](const Status& status) {
    std::unique_ptr<WriteQuery> query_holder(query);
    query->Finished(operation, status);
  });
  return std::move(operation_);
}

void WriteQuery::DoStartSynchronization(const Status& status) {
  std::unique_ptr<WriteQuery> self(this);
  // Move submit_token_ so it is released after this function.
  ScopedRWOperation submit_token(std::move(submit_token_));
  // If a restart read is required, then we return this fact to caller and don't perform the write
  // operation.
  if (status.ok() && restart_read_ht_.is_valid()) {
    auto restart_time = response()->mutable_restart_read_time();
    restart_time->set_read_ht(restart_read_ht_.ToUint64());
    auto local_limit = context_->ReportReadRestart();
    if (!local_limit.ok()) {
      Cancel(local_limit.status());
      return;
    }
    restart_time->set_deprecated_max_of_read_time_and_local_limit_ht(local_limit->ToUint64());
    restart_time->set_local_limit_ht(local_limit->ToUint64());
    // Global limit is ignored by caller, so we don't set it.
    Cancel(Status::OK());
    return;
  }

  if (!status.ok()) {
    Cancel(status);
    return;
  }

  context_->Submit(self.release()->PrepareSubmit(), term_);
}

void WriteQuery::Release() {
  // Free DocDB multi-level locks.
  docdb_locks_.Reset();
}

WriteQuery::~WriteQuery() {
}

void WriteQuery::set_client_request(std::reference_wrapper<const tserver::WriteRequestPB> req) {
  client_request_ = &req.get();
  read_time_ = ReadHybridTime::FromReadTimePB(req.get());
  allow_immediate_read_restart_ = !read_time_;
}

void WriteQuery::set_client_request(std::unique_ptr<tserver::WriteRequestPB> req) {
  set_client_request(*req);
  client_request_holder_ = std::move(req);
}

void WriteQuery::Finished(WriteOperation* operation, const Status& status) {
  LOG_IF(DFATAL, operation_) << "Finished not submitted operation: " << status;

  auto tablet_result = operation->tablet_safe();
  if (!tablet_result.ok()) {
    LOG(DFATAL) << "Could not obtain tablet from operation "
                << operation->ToString() << " to finish write query: "
                << tablet_result.status()
                << ". Operation status: " << status;
    Complete(status.ok() ? tablet_result.status() : status);
    return;
  }

  auto tablet = *tablet_result;
  if (status.ok()) {
    TabletMetrics* metrics = tablet->metrics();
    if (metrics) {
      auto op_duration_usec =
          MonoDelta(CoarseMonoClock::now() - start_time_).ToMicroseconds();
      metrics->ql_write_latency->Increment(op_duration_usec);
    }
  }

  auto& metadata = *tablet->metadata();

  for (const auto& sv : operation->request()->write_batch().table_schema_version()) {
    if (!status.IsAborted()) {
      CHECK_LE(metadata.schema_version(), sv.schema_version()) << ", status: " << status;
    }
  }

  Complete(status);
}

void WriteQuery::Cancel(const Status& status) {
  LOG_IF(DFATAL, !operation_) << "Cancelled submitted operation: " << status;

  Complete(status);
}

void WriteQuery::Complete(const Status& status) {
  Release();

  if (callback_) {
    callback_(status);
  }
}

void WriteQuery::ExecuteDone(const Status& status) {
  docdb_locks_ = std::move(prepare_result_.lock_batch);
  scoped_read_operation_.Reset();
  switch (execute_mode_) {
    case ExecuteMode::kSimple:
      SimpleExecuteDone(status);
      return;
    case ExecuteMode::kRedis:
      RedisExecuteDone(status);
      return;
    case ExecuteMode::kCql:
      CqlExecuteDone(status);
      return;
    case ExecuteMode::kPgsql:
      PgsqlExecuteDone(status);
      return;
  }
  FATAL_INVALID_ENUM_VALUE(ExecuteMode, execute_mode_);
}

Result<bool> WriteQuery::PrepareExecute() {
  if (client_request_) {
    auto* request = operation().AllocateRequest();
    SetupKeyValueBatch(*client_request_, request);

    if (!client_request_->redis_write_batch().empty()) {
      return RedisPrepareExecute();
    }

    if (!client_request_->ql_write_batch().empty()) {
      return CqlPrepareExecute();
    }

    if (!client_request_->pgsql_write_batch().empty()) {
      return PgsqlPrepareExecute();
    }

    if (client_request_->has_write_batch() && client_request_->has_external_hybrid_time()) {
      StartSynchronization(std::move(self_), Status::OK());
      return false;
    }
  } else {
    const auto* request = operation().request();
    if (request && request->has_write_batch()) {
      const auto& write_batch = request->write_batch();
      // We allow the empty case if transaction is set since that is an update in transaction
      // metadata.
      if (!write_batch.read_pairs().empty() || write_batch.has_transaction()) {
        return SimplePrepareExecute();
      }
    }
  }

  // Empty write should not happen, but we could handle it.
  // Just report it as error in release mode.
  LOG(DFATAL) << "Empty write: " << AsString(client_request_) << ", " << AsString(request());

  return STATUS(InvalidArgument, "Empty write");
}

Status WriteQuery::InitExecute(ExecuteMode mode) {
  auto tablet = VERIFY_RESULT(tablet_safe());
  scoped_read_operation_ = tablet->CreateNonAbortableScopedRWOperation();
  if (!scoped_read_operation_.ok()) {
    return MoveStatus(scoped_read_operation_);
  }
  execute_mode_ = mode;
  return Status::OK();
}

Result<bool> WriteQuery::RedisPrepareExecute() {
  RETURN_NOT_OK(InitExecute(ExecuteMode::kRedis));

  // Since we take exclusive locks, it's okay to use Now as the read TS for writes.
  const auto& redis_write_batch = client_request_->redis_write_batch();

  doc_ops_.reserve(redis_write_batch.size());
  for (const auto& redis_request : redis_write_batch) {
    doc_ops_.emplace_back(new docdb::RedisWriteOperation(redis_request));
  }

  return true;
}

Result<bool> WriteQuery::SimplePrepareExecute() {
  RETURN_NOT_OK(InitExecute(ExecuteMode::kSimple));
  return true;
}

Result<bool> WriteQuery::CqlPrepareExecute() {
  auto tablet = VERIFY_RESULT(tablet_safe());
  RETURN_NOT_OK(InitExecute(ExecuteMode::kCql));

  auto& metadata = *tablet->metadata();
  VLOG(2) << "Schema version for  " << metadata.table_name() << ": " << metadata.schema_version();

  if (!CqlCheckSchemaVersion()) {
    return false;
  }

  docdb::AddTableSchemaVersion(
      Uuid::Nil(), metadata.schema_version(), request().mutable_write_batch());

  const auto& ql_write_batch = client_request_->ql_write_batch();

  doc_ops_.reserve(ql_write_batch.size());

  auto txn_op_ctx = VERIFY_RESULT(tablet->CreateTransactionOperationContext(
      client_request_->write_batch().transaction(),
      /* is_ysql_catalog_table */ false,
      &client_request_->write_batch().subtransaction()));
  auto table_info = metadata.primary_table_info();
  for (const auto& req : ql_write_batch) {
    QLResponsePB* resp = response_->add_ql_response_batch();
    auto write_op = std::make_unique<docdb::QLWriteOperation>(
        req,
        table_info->schema_version,
        table_info->doc_read_context,
        table_info->index_map,
        tablet->unique_index_key_schema(),
        txn_op_ctx);
    RETURN_NOT_OK(write_op->Init(resp));
    doc_ops_.emplace_back(std::move(write_op));
  }

  return true;
}

Result<bool> WriteQuery::PgsqlPrepareExecute() {
  auto tablet = VERIFY_RESULT(tablet_safe());
  RETURN_NOT_OK(InitExecute(ExecuteMode::kPgsql));

  if (!PgsqlCheckSchemaVersion()) {
    return false;
  }

  const auto& pgsql_write_batch = client_request_->pgsql_write_batch();

  doc_ops_.reserve(pgsql_write_batch.size());

  TransactionOperationContext txn_op_ctx;

  auto& metadata = *tablet->metadata();
  // Colocated via DB/tablegroup/syscatalog.
  bool colocated = metadata.colocated() || tablet->is_sys_catalog();

  for (const auto& req : pgsql_write_batch) {
    PgsqlResponsePB* resp = response_->add_pgsql_response_batch();
    // Table-level tombstones should not be requested for non-colocated tables.
    if ((req.stmt_type() == PgsqlWriteRequestPB::PGSQL_TRUNCATE_COLOCATED) && !colocated) {
      LOG(WARNING) << "cannot create table-level tombstone for a non-colocated table";
      resp->set_skipped(true);
      continue;
    }
    const TableInfoPtr table_info = VERIFY_RESULT(metadata.GetTableInfo(req.table_id()));
    docdb::AddTableSchemaVersion(
        table_info->cotable_id, table_info->schema_version, request().mutable_write_batch());
    if (doc_ops_.empty()) {
      // Use the value of is_ysql_catalog_table from the first operation in the batch.
      txn_op_ctx = VERIFY_RESULT(tablet->CreateTransactionOperationContext(
          client_request_->write_batch().transaction(),
          table_info->schema().table_properties().is_ysql_catalog_table(),
          &client_request_->write_batch().subtransaction()));
    }
    auto write_op = std::make_unique<docdb::PgsqlWriteOperation>(
        req,
        table_info->doc_read_context,
        txn_op_ctx,
        rpc_context_ ? &rpc_context_->sidecars() : nullptr);
    RETURN_NOT_OK(write_op->Init(resp));
    doc_ops_.emplace_back(std::move(write_op));
  }

  return true;
}

void WriteQuery::Execute(std::unique_ptr<WriteQuery> query) {
  auto* query_ptr = query.get();
  query_ptr->self_ = std::move(query);

  auto prepare_result = query_ptr->PrepareExecute();

  if (!prepare_result.ok()) {
    query_ptr->ExecuteDone(prepare_result.status());
    return;
  }

  if (!prepare_result.get()) {
    return;
  }

  auto status = query_ptr->DoExecute();
  if (!status.ok()) {
    query_ptr->ExecuteDone(status);
  }
}

// The conflict management policy (as defined in conflict_resolution.h) to be used is determined
// based on the following -
//   1. For explicit row level locking, YSQL sets the "wait_policy" field which maps to a
//      corresponding ConflictManagementPolicy as detailed in the WaitPolicy enum in common.proto.
//   2. For everything else, either the WAIT_ON_CONFLICT or the FAIL_ON_CONFLICT policy is used
//      based on whether wait queues are enabled or not.
docdb::ConflictManagementPolicy GetConflictManagementPolicy(
    const docdb::WaitQueue* wait_queue, const docdb::LWKeyValueWriteBatchPB& write_batch) {
  // Either write_batch.read_pairs is not empty or doc_ops is non empty. Both can't be non empty
  // together. This is because read_pairs is filled only in case of a read operation that has a
  // row mark or is part of a serializable txn.
  // 1. In case doc_ops are present, we either use the WAIT_ON_CONFLICT or the FAIL_ON_CONFLICT
  //    policy based on whether wait queues are enabled or not.
  // 2. In case of a read rpc that has wait_policy, we use the corresponding conflict management
  //    policy.

  auto conflict_management_policy = wait_queue ? docdb::WAIT_ON_CONFLICT : docdb::FAIL_ON_CONFLICT;
  const auto& pairs = write_batch.read_pairs();
  if (!pairs.empty() && write_batch.has_wait_policy()) {
    switch (write_batch.wait_policy()) {
      case WAIT_BLOCK:
        if (wait_queue) {
          conflict_management_policy = docdb::WAIT_ON_CONFLICT;
        } else {
          YB_LOG_EVERY_N(WARNING, 100)
              << "Received WAIT_BLOCK request from query layer but wait queues are not enabled at "
              << "tserver. Reverting to WAIT_ERROR behavior.";
        }
        break;
      case WAIT_SKIP:
        conflict_management_policy = docdb::SKIP_ON_CONFLICT;
        break;
      case WAIT_ERROR:
        conflict_management_policy = docdb::FAIL_ON_CONFLICT;
        break;
      default:
        LOG(WARNING) << "Unknown wait policy " << write_batch.wait_policy();
    }
  }

  VLOG(2) << FullyDecodeTransactionId(write_batch.transaction().transaction_id())
          << ": effective conflict_management_policy=" << conflict_management_policy;

  return conflict_management_policy;
}

Status WriteQuery::DoExecute() {
  auto tablet = VERIFY_RESULT(tablet_safe());
  auto& write_batch = *request().mutable_write_batch();
  isolation_level_ = VERIFY_RESULT(tablet->GetIsolationLevelFromPB(write_batch));
  const RowMarkType row_mark_type = GetRowMarkTypeFromPB(write_batch);
  const auto& metadata = *tablet->metadata();

  const bool transactional_table = metadata.schema()->table_properties().is_transactional() ||
                                   force_txn_path_;

  if (!transactional_table && isolation_level_ != IsolationLevel::NON_TRANSACTIONAL) {
    YB_LOG_EVERY_N_SECS(DFATAL, 30)
        << "An attempt to perform a transactional operation on a non-transactional table: "
        << operation_->ToString();
  }

  docdb::PartialRangeKeyIntents partial_range_key_intents(metadata.UsePartialRangeKeyIntents());
  prepare_result_ = VERIFY_RESULT(docdb::PrepareDocWriteOperation(
      doc_ops_, write_batch.read_pairs(), tablet->metrics()->write_lock_latency,
      tablet->metrics()->failed_batch_lock, isolation_level_, kind(), row_mark_type,
      transactional_table, write_batch.has_transaction(), deadline(), partial_range_key_intents,
      tablet->shared_lock_manager()));

  TEST_SYNC_POINT("WriteQuery::DoExecute::PreparedDocWriteOps");

  auto* transaction_participant = tablet->transaction_participant();
  docdb::WaitQueue* wait_queue = nullptr;

  if (transaction_participant && execute_mode_ != ExecuteMode::kCql) {
    // TODO(wait-queues): https://github.com/yugabyte/yugabyte-db/issues/17557
    // For now, we disable this behavior in YCQL, both because many tests depend on FAIL_ON_CONFLICT
    // behavior and because we do not want to change the behavior of existing CQL clusters. We may
    // revisit this in the future in case there are performance benefits to enabling
    // WAIT_ON_CONFLICT in YCQL as well.
    //
    // Note that we do not check execute_mode_ == ExecuteMode::kPgsql, since this condition is not
    // sufficient to include all YSQL traffic -- some YSQL traffic may have ExecuteMode::kSimple,
    // such as writes performed as part of an explicit lock read.
    wait_queue = transaction_participant->wait_queue();
  }

  if (!tablet->txns_enabled() || !transactional_table) {
    CompleteExecute();
    return Status::OK();
  }

  if (isolation_level_ == IsolationLevel::NON_TRANSACTIONAL) {
    auto now = tablet->clock()->Now();
    auto conflict_management_policy = GetConflictManagementPolicy(wait_queue, write_batch);

    {
      // TODO(#19498): Enable the below check if possible. Right now we can't enable it because the
      // read time for all YCQL operations is picked on the YCQL query layer, and this might be
      // indicative of some correctness bugs similar to #19407 which was seen on YSQL.

      // Read time should not be picked until conflict resolution is successful for the single shard
      // operation path. This is because ResolveOperationConflicts() doesn't check regular db for
      // conflicting data committed in regular db. If in future, we have to read data before
      // conflict resolution, we should check conflicts in regular db too.

      // RSTATUS_DCHECK(
      //     !read_time_, IllegalState,
      //     "Read time was picked before conflict resolution for a single shard operation.");
    }
    return docdb::ResolveOperationConflicts(
        doc_ops_, conflict_management_policy, now, tablet->doc_db(),
        partial_range_key_intents, transaction_participant,
        tablet->metrics()->transaction_conflicts.get(), &prepare_result_.lock_batch,
        wait_queue,
        [this, now](const Result<HybridTime>& result) {
          if (!result.ok()) {
            ExecuteDone(result.status());
            TRACE("InvokeCallback");
            return;
          }
          NonTransactionalConflictsResolved(now, *result);
          TRACE("NonTransactionalConflictsResolved");
        });
  }

  if (isolation_level_ == IsolationLevel::SERIALIZABLE_ISOLATION &&
      prepare_result_.need_read_snapshot) {
    boost::container::small_vector<RefCntPrefix, 16> paths;
    for (const auto& doc_op : doc_ops_) {
      paths.clear();
      IsolationLevel ignored_isolation_level;
      RETURN_NOT_OK(doc_op->GetDocPaths(
          docdb::GetDocPathsMode::kLock, &paths, &ignored_isolation_level));
      for (const auto& path : paths) {
        auto key = path.as_slice();
        auto& pair = write_batch.mutable_read_pairs()->emplace_back();
        pair.dup_key(key);
        // Empty values are disallowed by docdb.
        // https://github.com/YugaByte/yugabyte-db/issues/736
        pair.dup_value(std::string(1, docdb::KeyEntryTypeAsChar::kNullLow));
      }
    }
  }

  auto conflict_management_policy = GetConflictManagementPolicy(wait_queue, write_batch);

  // TODO(wait-queues): Ensure that wait_queue respects deadline() during conflict resolution.
  return docdb::ResolveTransactionConflicts(
      doc_ops_, conflict_management_policy, write_batch, tablet->clock()->Now(),
      read_time_ ? read_time_.read : HybridTime::kMax,
      tablet->doc_db(), partial_range_key_intents,
      transaction_participant, tablet->metrics()->transaction_conflicts.get(),
      &prepare_result_.lock_batch, wait_queue,
      [this](const Result<HybridTime>& result) {
        if (!result.ok()) {
          ExecuteDone(result.status());
          TRACE("ExecuteDone");
          return;
        }
        TransactionalConflictsResolved();
        TRACE("TransactionalConflictsResolved");
      });
}

void WriteQuery::NonTransactionalConflictsResolved(HybridTime now, HybridTime result) {
  auto tablet_result = tablet_safe();
  if (!tablet_result.ok()) {
    ExecuteDone(tablet_result.status());
    return;
  }
  auto tablet = *tablet_result;

  if (now != result) {
    tablet->clock()->Update(result);
  }

  CompleteExecute();
}

void WriteQuery::TransactionalConflictsResolved() {
  auto status = DoTransactionalConflictsResolved();
  if (!status.ok()) {
    LOG(DFATAL) << status;
    ExecuteDone(status);
  }
}

Status WriteQuery::DoTransactionalConflictsResolved() {
  auto tablet = VERIFY_RESULT(tablet_safe());
  if (!read_time_) {
    auto safe_time = VERIFY_RESULT(tablet->SafeTime(RequireLease::kTrue));
    read_time_ = ReadHybridTime::FromHybridTimeRange(
        {safe_time, tablet->clock()->NowRange().second});
  } else if (prepare_result_.need_read_snapshot &&
             isolation_level_ == IsolationLevel::SERIALIZABLE_ISOLATION) {
    return STATUS_FORMAT(
        InvalidArgument,
        "Read time should NOT be specified for serializable isolation level: $0",
        read_time_);
  }

  CompleteExecute();
  return Status::OK();
}

void WriteQuery::CompleteExecute() {
  ExecuteDone(DoCompleteExecute());
}

Status WriteQuery::DoCompleteExecute() {
  auto tablet = VERIFY_RESULT(tablet_safe());
  auto read_op = prepare_result_.need_read_snapshot
      ? VERIFY_RESULT(ScopedReadOperation::Create(tablet.get(),
                                                  RequireLease::kTrue,
                                                  read_time_))
      : ScopedReadOperation();
  // Actual read hybrid time used for read-modify-write operation.
  auto real_read_time = prepare_result_.need_read_snapshot
      ? read_op.read_time()
      // When need_read_snapshot is false, this time is used only to write TTL field of record.
      : ReadHybridTime::SingleTime(tablet->clock()->Now());

  // We expect all read operations for this transaction to be done in AssembleDocWriteBatch. Once
  // read_txn goes out of scope, the read point is deregistered.
  bool local_limit_updated = false;

  // This loop may be executed multiple times multiple times only for serializable isolation or
  // when read_time was not yet picked for snapshot isolation.
  // In all other cases it is executed only once.
  auto init_marker_behavior = tablet->table_type() == TableType::REDIS_TABLE_TYPE
      ? docdb::InitMarkerBehavior::kRequired
      : docdb::InitMarkerBehavior::kOptional;
  for (;;) {
    RETURN_NOT_OK(docdb::AssembleDocWriteBatch(
        doc_ops_, deadline(), real_read_time, tablet->doc_db(),
        request().mutable_write_batch(), init_marker_behavior,
        tablet->monotonic_counter(), &restart_read_ht_,
        tablet->metadata()->table_name()));

    // For serializable isolation we don't fix read time, so could do read restart locally,
    // instead of failing whole transaction.
    if (!restart_read_ht_.is_valid() || !allow_immediate_read_restart_) {
      break;
    }

    real_read_time.read = restart_read_ht_;
    if (!local_limit_updated) {
      local_limit_updated = true;
      real_read_time.local_limit = std::min(
          real_read_time.local_limit, VERIFY_RESULT(tablet->SafeTime(RequireLease::kTrue)));
    }

    restart_read_ht_ = HybridTime();

    request().mutable_write_batch()->mutable_write_pairs()->clear();

    for (auto& doc_op : doc_ops_) {
      doc_op->ClearResponse();
    }
  }

  if (allow_immediate_read_restart_ &&
      isolation_level_ != IsolationLevel::NON_TRANSACTIONAL &&
      response_) {
    real_read_time.ToPB(response_->mutable_used_read_time());
  }

  if (restart_read_ht_.is_valid()) {
    return Status::OK();
  }

  return Status::OK();
}

Result<TabletPtr> WriteQuery::tablet_safe() const {
  return operation_->tablet_safe();
}

void WriteQuery::AdjustYsqlQueryTransactionality(size_t ysql_batch_size) {
  auto tablet_result = tablet_safe();
  if (!tablet_result.ok()) {
    YB_LOG_EVERY_N_SECS(WARNING, 1)
        << "Cannot adjust YSQL query transactionality, the tablet has already been destroyed. "
        << "ysql_batch_size=" << ysql_batch_size;
    return;
  }
  force_txn_path_ = ysql_batch_size > 0 && (*tablet_result)->is_sys_catalog();
}

void WriteQuery::RedisExecuteDone(const Status& status) {
  if (!status.ok() || restart_read_ht().is_valid()) {
    StartSynchronization(std::move(self_), status);
    return;
  }
  for (auto& doc_op : doc_ops_) {
    auto* redis_write_operation = down_cast<docdb::RedisWriteOperation*>(doc_op.get());
    response_->add_redis_response_batch()->Swap(&redis_write_operation->response());
  }

  StartSynchronization(std::move(self_), Status::OK());
}

bool WriteQuery::CqlCheckSchemaVersion() {
  auto tablet_result = tablet_safe();
  if (!tablet_result.ok()) {
    YB_LOG_EVERY_N_SECS(WARNING, 1)
        << "Tablet has already been destroyed in WriteQuery::CqlCheckSchemaVersion.";
    return false;
  }
  auto tablet = *tablet_result;

  constexpr auto error_code = QLResponsePB::YQL_STATUS_SCHEMA_VERSION_MISMATCH;
  auto& metadata = *tablet->metadata();
  const auto& req_batch = client_request_->ql_write_batch();
  auto& resp_batch = *response_->mutable_ql_response_batch();

  auto table_info = metadata.primary_table_info();
  int index = 0;
  int num_mismatches = 0;
  for (const auto& req : req_batch) {
    if (!CheckSchemaVersion(
            table_info.get(), req.schema_version(), req.is_compatible_with_previous_version(),
            error_code, index, &resp_batch)) {
      ++num_mismatches;
    }
    ++index;
  }

  if (num_mismatches != 0) {
    SchemaVersionMismatch(error_code, req_batch.size(), &resp_batch);
    return false;
  }

  return true;
}

void WriteQuery::CqlExecuteDone(const Status& status) {
  if (!CqlCheckSchemaVersion()) {
    return;
  }

  if (restart_read_ht().is_valid()) {
    StartSynchronization(std::move(self_), Status::OK());
    return;
  }

  if (status.ok()) {
    UpdateQLIndexes();
  } else {
    CompleteQLWriteBatch(status);
  }
}

template <class Code, class Resp>
void WriteQuery::SchemaVersionMismatch(Code code, int size, Resp* resp) {
  for (int i = 0; i != size; ++i) {
    auto* entry = resp->size() > i ? resp->Mutable(i) : resp->Add();
    if (entry->status() == code) {
      continue;
    }
    entry->Clear();
    entry->set_status(code);
    entry->set_error_message("Other request entry schema version mismatch");
  }
  auto self = std::move(self_);
  submit_token_.Reset();
  Cancel(Status::OK());
}

void WriteQuery::CompleteQLWriteBatch(const Status& status) {
  if (!status.ok()) {
    StartSynchronization(std::move(self_), status);
    return;
  }
  auto tablet_result = tablet_safe();
  if (!tablet_result.ok()) {
    StartSynchronization(std::move(self_), tablet_result.status());
    return;
  }
  auto tablet = *tablet_result;

  bool is_unique_index = tablet->metadata()->is_unique_index();

  for (auto& doc_op : doc_ops_) {
    std::unique_ptr<docdb::QLWriteOperation> ql_write_op(
        down_cast<docdb::QLWriteOperation*>(doc_op.release()));
    if (is_unique_index &&
        ql_write_op->request().type() == QLWriteRequestPB::QL_STMT_INSERT &&
        ql_write_op->response()->has_applied() && !ql_write_op->response()->applied()) {
      // If this is an insert into a unique index and it fails to apply, report duplicate value err.
      // has_applied is only true if we have evaluated the requests if_expr and is only false if
      // that if_expr ws not satisfied or if the remote index was unique and had a duplicate value
      // to the one we're trying to insert here.
      VLOG(1) << "Could not apply operation to remote index " << AsString(ql_write_op->request())
               << " due to " << AsString(ql_write_op->response());
      ql_write_op->response()->set_error_message(
          Format("Duplicate value disallowed by unique index $0",
          tablet->metadata()->table_name()));
      ql_write_op->response()->set_status(QLResponsePB::YQL_STATUS_USAGE_ERROR);
    } else if (ql_write_op->rowblock() != nullptr) {
      // If the QL write op returns a rowblock, move the op to the transaction state to return the
      // rows data as a sidecar after the transaction completes.
      ql_write_ops_.emplace_back(std::move(ql_write_op));
    }
  }

  StartSynchronization(std::move(self_), Status::OK());
}

struct UpdateQLIndexesTask {
  WriteQuery* query;

  client::YBClient* client = nullptr;
  client::YBTransactionPtr txn;
  client::YBSessionPtr session;
  const ChildTransactionDataPB* child_transaction_data = nullptr;
  std::shared_ptr<client::YBMetaDataCache> metadata_cache;

  std::mutex mutex;
  WriteQuery::IndexOps index_ops GUARDED_BY(mutex);
  size_t counter GUARDED_BY(mutex) = 1;
  Status failure;

  explicit UpdateQLIndexesTask(WriteQuery* query_) : query(query_) {}

  Status Init(const TabletPtr& tablet, docdb::QLWriteOperation* write_op) {
    client = &tablet->client();
    session = std::make_shared<client::YBSession>(client);
    session->SetDeadline(query->deadline());
    if (write_op->request().has_child_transaction_data()) {
      child_transaction_data = &write_op->request().child_transaction_data();
      auto child_data = VERIFY_RESULT(client::ChildTransactionData::FromPB(
          write_op->request().child_transaction_data()));
      txn = std::make_shared<client::YBTransaction>(&tablet->transaction_manager(), child_data);
      session->SetTransaction(txn);
    } else {
      child_transaction_data = nullptr;
    }
    metadata_cache = tablet->YBMetaDataCache();
    if (!metadata_cache) {
      return STATUS(Corruption, "Table metadata cache is not present for index update");
    }

    return Status::OK();
  }

  void SanityCheck(docdb::QLWriteOperation* write_op) {
    if (write_op->request().has_child_transaction_data()) {
      DCHECK_ONLY_NOTNULL(child_transaction_data);
      DCHECK_EQ(child_transaction_data->ShortDebugString(),
                write_op->request().child_transaction_data().ShortDebugString());
    } else {
      DCHECK(child_transaction_data == nullptr) <<
          "Value: " << child_transaction_data->ShortDebugString();
    }
  }

  void AddRequests(
      const std::shared_ptr<UpdateQLIndexesTask>& self, docdb::QLWriteOperation* write_op) {
    // Apply the write ops to update the index
    {
      std::lock_guard lock(self->mutex);
      counter += write_op->index_requests().size();
    }
    for (auto& [index_info, index_request] : write_op->index_requests()) {
      auto callback = [self, &index_request = index_request, write_op](const auto& index_table) {
        self->TableResolved(&index_request, write_op, index_table);
      };
      metadata_cache->GetTableAsync(index_info->table_id(), callback);
    }
  }

  void TableResolved(
      QLWriteRequestPB* index_request, docdb::QLWriteOperation* write_op,
      const Result<client::GetTableResult>& index_table) {
    if (!index_table.ok()) {
      std::lock_guard lock(mutex);
      if (failure.ok()) {
        failure = index_table.status();
      }
      CompleteStep();
      return;
    }

    std::shared_ptr<client::YBqlWriteOp> index_op(index_table->table->NewQLWrite());
    index_op->mutable_request()->Swap(index_request);
    index_op->mutable_request()->MergeFrom(*index_request);

    std::lock_guard lock(mutex);
    session->Apply(index_op);
    index_ops.emplace_back(std::move(index_op), write_op);
    CompleteStep();
  }

  void Filled() {
    std::lock_guard lock(mutex);
    CompleteStep();
  }

  void CompleteStep() REQUIRES(mutex) {
    if (!--counter) {
      if (!failure.ok()) {
        WriteQuery::StartSynchronization(std::move(query->self_), failure);
        return;
      }
      session->FlushAsync(std::bind(
          &WriteQuery::UpdateQLIndexesFlushed, query, session, txn, std::move(index_ops), _1));
    }
  }
};

void WriteQuery::UpdateQLIndexes() {
  auto tablet_result = tablet_safe();
  if (!tablet_result.ok()) {
    StartSynchronization(std::move(self_), tablet_result.status());
    return;
  }
  auto tablet = *tablet_result;

  std::shared_ptr<UpdateQLIndexesTask> task;
  for (auto& doc_op : doc_ops_) {
    auto* write_op = down_cast<docdb::QLWriteOperation*>(doc_op.get());
    if (write_op->index_requests().empty()) {
      continue;
    }
    if (!task) {
      task = std::make_shared<UpdateQLIndexesTask>(this);
      auto status = task->Init(tablet, write_op);
      if (!status.ok()) {
        StartSynchronization(std::move(self_), status);
        return;
      }
    } else {
      task->SanityCheck(write_op);
    }

    task->AddRequests(task, write_op);
  }

  if (!task) {
    CompleteQLWriteBatch(Status::OK());
    return;
  }

  task->Filled();
}

void WriteQuery::UpdateQLIndexesFlushed(
    const client::YBSessionPtr& session, const client::YBTransactionPtr& txn,
    const IndexOps& index_ops, client::FlushStatus* flush_status) {
  while (GetAtomicFlag(&FLAGS_TEST_writequery_stuck_from_callback_leak)) {
    std::this_thread::sleep_for(100ms);
  }
  std::unique_ptr<WriteQuery> query(std::move(self_));

  const auto& status = flush_status->status;
  if (PREDICT_FALSE(!status.ok())) {
    // When any error occurs during the dispatching of YBOperation, YBSession saves the error and
    // returns IOError. When it happens, retrieves the errors and discard the IOError.
    if (status.IsIOError()) {
      for (const auto& error : flush_status->errors) {
        // return just the first error seen.
        Cancel(error->status());
        return;
      }
    }
    Cancel(status);
    return;
  }

  ChildTransactionResultPB child_result;
  if (txn) {
    auto finish_result = txn->FinishChild();
    if (!finish_result.ok()) {
      query->Cancel(finish_result.status());
      return;
    }
    child_result = std::move(*finish_result);
  }

  // Check the responses of the index write ops.
  for (const auto& pair : index_ops) {
    std::shared_ptr<client::YBqlWriteOp> index_op = pair.first;
    auto* response = pair.second->response();
    DCHECK_ONLY_NOTNULL(response);
    auto* index_response = index_op->mutable_response();

    if (index_response->status() != QLResponsePB::YQL_STATUS_OK) {
      VLOG(1) << "Got response " << index_response->ShortDebugString()
              << " for " << AsString(index_op);
      response->set_status(index_response->status());
      response->set_error_message(std::move(*index_response->mutable_error_message()));
    }
    if (txn) {
      *response->mutable_child_transaction_result() = child_result;
    }
  }

  self_ = std::move(query);
  CompleteQLWriteBatch(Status::OK());
}

bool WriteQuery::PgsqlCheckSchemaVersion() {
  auto tablet_result = tablet_safe();
  if (!tablet_result.ok()) {
    StartSynchronization(std::move(self_), tablet_result.status());
    return false;
  }
  auto tablet = *tablet_result;

  constexpr auto error_code = PgsqlResponsePB::PGSQL_STATUS_SCHEMA_VERSION_MISMATCH;
  auto& metadata = *tablet->metadata();
  const auto& req_batch = client_request_->pgsql_write_batch();
  auto& resp_batch = *response_->mutable_pgsql_response_batch();

  int index = 0;
  int num_mismatches = 0;
  for (const auto& req : req_batch) {
    auto table_info = metadata.GetTableInfo(req.table_id());
    if (!table_info.ok()) {
      StartSynchronization(std::move(self_), table_info.status());
      return false;
    }
    if (!CheckSchemaVersion(
            table_info->get(), req.schema_version(), /* compatible_with_previous_version= */ false,
            error_code, index, &resp_batch)) {
      ++num_mismatches;
    }
    ++index;
  }

  if (num_mismatches != 0) {
    SchemaVersionMismatch(error_code, req_batch.size(), &resp_batch);
    return false;
  }

  return true;
}

void WriteQuery::PgsqlExecuteDone(const Status& status) {
  if (!PgsqlCheckSchemaVersion()) {
    return;
  }

  if (!status.ok() || restart_read_ht_.is_valid()) {
    StartSynchronization(std::move(self_), status);
    return;
  }

  for (auto& doc_op : doc_ops_) {
    // We'll need to return the number of rows inserted, updated, or deleted by each operation.
    std::unique_ptr<docdb::PgsqlWriteOperation> pgsql_write_op(
        down_cast<docdb::PgsqlWriteOperation*>(doc_op.release()));
    pgsql_write_ops_.emplace_back(std::move(pgsql_write_op));
  }

  StartSynchronization(std::move(self_), Status::OK());
}

void WriteQuery::SimpleExecuteDone(const Status& status) {
  StartSynchronization(std::move(self_), status);
}

}  // namespace tablet
}  // namespace yb
