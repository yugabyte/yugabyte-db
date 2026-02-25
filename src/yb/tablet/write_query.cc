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

#include "yb/tablet/write_query.h"

#include "yb/ash/wait_state.h"

#include <boost/logic/tribool.hpp>

#include "yb/client/client.h"
#include "yb/client/error.h"
#include "yb/client/meta_data_cache.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/transaction.h"
#include "yb/client/yb_op.h"

#include "yb/common/row_mark.h"
#include "yb/common/schema.h"

#include "yb/docdb/conflict_resolution.h"
#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/cql_operation.h"
#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/doc_write_batch.h"
#include "yb/docdb/docdb_statistics.h"
#include "yb/docdb/pgsql_operation.h"
#include "yb/docdb/redis_operation.h"

#include "yb/qlexpr/index.h"

#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/operations/write_operation.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metrics.h"
#include "yb/tablet/transaction_participant.h"
#include "yb/tablet/write_query_context.h"

#include "yb/tserver/tserver.pb.h"

#include "yb/util/debug-util.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/scope_exit.h"
#include "yb/util/sync_point.h"
#include "yb/util/trace.h"
#include "yb/util/flags.h"

DECLARE_bool(cdc_propagate_query_comments);

using namespace std::placeholders;
using namespace std::literals;

DEFINE_RUNTIME_bool(disable_alter_vs_write_mutual_exclusion, false,
    "A safety switch to disable the changes from D8710 which makes a schema "
    "operation take an exclusive lock making all write operations wait for it.");
TAG_FLAG(disable_alter_vs_write_mutual_exclusion, advanced);

DEFINE_RUNTIME_bool(skip_prefix_locks, true,
    "Enable to skip writing weak intent locks on intermediate prefixes of the actual PK being "
    "written. This reduces the number of key-value pairs written to the intents db, resulting in "
    "better performance. However, it may lead to high contention when serializable isolation "
    "transactions are present in the workload.");

DEFINE_RUNTIME_AUTO_bool(skip_prefix_locks_for_upgrade, kLocalPersisted, false, true,
    "The gflag is for upgrade and should not be changed. skip prefix lock is enabled only if "
    "both skip_prefix_locks and skip_prefix_locks_for_upgrade are true. To disable the feature, "
    "just set skip_prefix_locks to false.");

DEFINE_RUNTIME_bool(skip_prefix_locks_suppress_warning_for_serializable_op, false,
    "If false, logs a warning when a serializable transaction runs with skip prefix locks enabled."
    "Serializable isolation transactions are not meant to be run if skip_prefix_locks is enabled. "
    "This will result in warnings in the log. Use this flag to suppress the warnings if you are "
    "sure that the serializable isolation transactions are never going to block a lot of other "
    "transactions.");
TAG_FLAG(skip_prefix_locks_suppress_warning_for_serializable_op, advanced);

DEFINE_test_flag(bool, writequery_stuck_from_callback_leak, false,
    "Simulate WriteQuery stuck because of the update index flushed rpc call back leak");

DECLARE_bool(batch_tablet_metrics_update);
DECLARE_bool(ysql_analyze_dump_metrics);
DECLARE_bool(ysql_enable_packed_row);

namespace yb {
namespace tablet {

namespace {

// Separate Redis / QL / row operations write batches from write_request in preparation for the
// write transaction. Leave just the tablet id behind. Return Redis / QL / row operations, etc.
// in batch_request.
void SetupKeyValueBatch(const tserver::WriteRequestMsg& client_request, LWWritePB* out_request) {
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
  }
  if (client_request.has_start_time_micros()) {
    out_request->set_start_time_micros(client_request.start_time_micros());
  }
  if (client_request.has_xrepl_origin_id()) {
    out_request->set_xrepl_origin_id(client_request.xrepl_origin_id());
  }
  out_request->set_batch_idx(client_request.batch_idx());
  // Actually, in production code, we could check for external hybrid time only when there are
  // no ql, pgsql, redis operations.
  // But in CDCServiceTest we have ql write batch with external time.
  if (client_request.has_external_hybrid_time()) {
    out_request->set_external_hybrid_time(client_request.external_hybrid_time());
  }
  // Copy query_comment from first PgsqlWriteRequestPB that has one.
  // Gated behind the flag to avoid WAL bloat when feature is disabled.
  if (FLAGS_cdc_propagate_query_comments) {
    for (const auto& pgsql_req : client_request.pgsql_write_batch()) {
      if (pgsql_req.has_query_comment()) {
        out_request->dup_query_comment(pgsql_req.query_comment());
        break;
      }
    }
  }
}

template <class Code, class Resp>
bool CheckSchemaVersion(
    TableInfo* table_info, int schema_version,
    const std::optional<bool>& compatible_with_previous_version, Code code,
    int index, Resp* resp_batch) {
  const bool compat = compatible_with_previous_version.has_value() ?
                      *compatible_with_previous_version : false;
  if (IsSchemaVersionCompatible(table_info->schema_version, schema_version, compat)) {
    return true;
  }

  VLOG(1) << " On " << table_info->table_name
          << " Setting status for write as " << code << " tserver's: "
          << table_info->schema_version << " vs req's : " << schema_version
          << " is req compatible with prev version: "
          << yb::ToString(compatible_with_previous_version);
  while (index >= resp_batch->size()) {
    resp_batch->Add();
  }
  auto resp = resp_batch->Mutable(index);
  resp->Clear();
  resp->set_status(code);

  std::string compat_str;
  if (compatible_with_previous_version.has_value()) {
    compat_str = Format(" (compt with prev: $0)", compat);
  }
  resp->set_error_message(Format(
      "schema version mismatch for table $0: expected $1, got $2$3",
      table_info->table_id, table_info->schema_version, schema_version,
      compat_str));
  return false;
}

using DocPaths = boost::container::small_vector<RefCntPrefix, 16>;

void AddReadPairs(const DocPaths& paths, docdb::LWKeyValueWriteBatchPB* write_batch,
                  boost::tribool pk_is_known = boost::indeterminate) {
  for (const auto& path : paths) {
    auto& pair = *write_batch->add_read_pairs();
    pair.dup_key(path.as_slice());
    // Empty values are disallowed by docdb.
    // https://github.com/YugaByte/yugabyte-db/issues/736
    pair.dup_value(Slice(&dockv::KeyEntryTypeAsChar::kNullLow, 1));
    if (!boost::indeterminate(pk_is_known)) {
      pair.set_pk_is_known(static_cast<bool>(pk_is_known));
    }
  }
}

[[nodiscard]] bool IsSkipped(const docdb::DocOperation& doc_op) {
  return doc_op.OpType() == docdb::DocOperation::Type::PGSQL_WRITE_OPERATION &&
         down_cast<const docdb::PgsqlWriteOperation&>(doc_op).response()->skipped();
}

// When reset_ops is true, the operations in the provided 'doc_ops' are reset with the current
// schema version.
Status CqlPopulateDocOps(
    const TabletPtr& tablet, const tserver::WriteRequestMsg* client_request,
    docdb::DocOperations* doc_ops, tserver::WriteResponseMsg* resp, bool reset_ops = false) {
  const auto& ql_write_batch = client_request->ql_write_batch();
  doc_ops->reserve(ql_write_batch.size());

  auto txn_op_ctx = VERIFY_RESULT(tablet->CreateTransactionOperationContext(
      client_request->write_batch().transaction(),
      /* is_ysql_catalog_table */ false,
      &client_request->write_batch().subtransaction()));
  auto table_info = tablet->metadata()->primary_table_info();
  for (int i = 0; i < ql_write_batch.size(); i++) {
    auto write_op = std::make_unique<docdb::QLWriteOperation>(
        ql_write_batch[i], table_info->schema_version, table_info->doc_read_context,
        table_info->index_map, table_info->unique_index_key_projection, txn_op_ctx);
    if (reset_ops) {
      auto* old_write_op = down_cast<docdb::QLWriteOperation*>((*doc_ops)[i].get());
      RETURN_NOT_OK(write_op->Init(old_write_op->response()));
      (*doc_ops)[i] = std::move(write_op);
    } else {
      RETURN_NOT_OK(write_op->Init(resp->add_ql_response_batch()));
      doc_ops->emplace_back(std::move(write_op));
    }
  }
  return Status::OK();
}

} // namespace

enum class WriteQuery::ExecuteMode {
  kSimple,
  kRedis,
  kCql,
  kPgsql,
  kPgsqlLock,
};

WriteQuery::WriteQuery(
    int64_t term,
    CoarseTimePoint deadline,
    WriteQueryContext* context,
    TabletPtr tablet,
    rpc::RpcContext* rpc_context,
    tserver::WriteResponseMsg* response)
    : tablet_(tablet),
      operation_(std::make_unique<WriteOperation>(std::move(tablet))),
      term_(term),
      deadline_(deadline),
      context_(context),
      rpc_context_(rpc_context),
      response_(response),
      start_time_(MonoTime::Now()),
      execute_mode_(ExecuteMode::kSimple) {
  IncrementActiveWriteQueryObjectsBy(1);
  auto res = tablet_safe();
  if (res.ok()) {
    global_tablet_metrics_ = (*res)->metrics();
  }

  metrics_ = std::make_shared<TabletMetricsHolder>(
      GetAtomicFlag(&FLAGS_batch_tablet_metrics_update), global_tablet_metrics_,
      &scoped_tablet_metrics_);
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
  // If a schema mismatch error occured, populate the response accordingly and return.
  if (schema_version_mismatch_) {
    if (execute_mode_ == ExecuteMode::kCql) {
      CqlRespondSchemaVersionMismatch();
    } else if (execute_mode_ == ExecuteMode::kPgsql) {
      PgsqlRespondSchemaVersionMismatch();
    } else {
      auto s = STATUS_FORMAT(
          IllegalState,
          Format("Schema version mismatch error not expected for WriteQuery::ExecuteMode $0",
                 BOOST_PP_STRINGIZE(execute_mode_)));
      LOG(DFATAL) << s;
      Cancel(s);
    }
    return;
  }
  // If a restart read is required, then we return this fact to caller and don't perform the write
  // operation.
  if (status.ok() && read_restart_data_.is_valid()) {
    auto restart_time = response()->mutable_restart_read_time();
    restart_time->set_read_ht(read_restart_data_.restart_time.ToUint64());
    auto local_limit = context_->ReportReadRestart();
    if (!local_limit.ok()) {
      Cancel(local_limit.status());
      return;
    }
    restart_time->set_deprecated_max_of_read_time_and_local_limit_ht(local_limit->ToUint64());
    restart_time->set_local_limit_ht(local_limit->ToUint64());
    response()->set_restart_read_key(read_restart_data_.key);
    // Global limit is ignored by caller, so we don't set it.
    Cancel(Status::OK());
    return;
  }

  if (!status.ok()) {
    Cancel(status);
    return;
  }

  if (client_request_ && client_request_->use_async_write()) {
    VLOG(2) << "Performing Async write: " << client_request_->ShortDebugString();
    operation_->SetAsyncWrite(
        [query = this](Result<OpId> opid) -> void {
          // TODO: Add metrics for async writes.
          // Query is still pending, but we are ready to invoke the callback.

          Status status;
          if (opid.ok()) {
            query->context_->RegisterAsyncWrite(*opid);
            opid->ToPB(query->response_->mutable_async_write_op_id());
          } else {
            status = std::move(opid.status());
          }

          TEST_SYNC_POINT("WriteQuery::BeforeCallbackInvoke");
          TEST_SYNC_POINT_CALLBACK("WriteQuery::SetCallbackStatus", &status);
          query->InvokeCallback(status);
          TEST_SYNC_POINT("WriteQuery::AfterCallbackInvoke");
        });
  }

  TRACE_FUNC();
  ASH_ENABLE_CONCURRENT_UPDATES();
  SET_WAIT_STATUS(OnCpu_Passive);
  context_->Submit(self.release()->PrepareSubmit(), term_);
  // Any further update to the wait-state for this RPC should happen based on
  // the state/transition of the submitted WriteOperation.
  // We don't want to update this RPC's wait-state when this thread returns from
  // ServicePoolImpl::Handle call.
  //
  // Prevent any further modification to the wait-state on this thread.
  ash::WaitStateInfo::SetCurrentWaitState(nullptr);
}

void WriteQuery::Release() {
  // Free DocDB multi-level locks.
  docdb_locks_.Reset();
}

WriteQuery::~WriteQuery() {
  IncrementActiveWriteQueryObjectsBy(-1);

  // Any metrics updated after destroying the WriteQuery
  // object cannot be sent with the response PB. So, update
  // global tablet metrics directly from now.
  metrics_->Reset();

  if (global_tablet_metrics_) {
    scoped_tablet_metrics_.MergeAndClear(global_tablet_metrics_);
  }
  auto tablet_result = tablet_safe();
  if (tablet_result.ok()) {
    scoped_statistics_.MergeAndClear(
        (*tablet_result)->regulardb_statistics().get(),
        (*tablet_result)->intentsdb_statistics().get());
  }
}

void WriteQuery::set_client_request(std::reference_wrapper<const tserver::WriteRequestMsg> req) {
  client_request_ = &req.get();
  read_time_ = ReadHybridTime::FromReadTimePB(req.get());
  allow_immediate_read_restart_ = !read_time_;
}

void WriteQuery::set_client_request(std::unique_ptr<tserver::WriteRequestMsg> req) {
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
    if (metrics_) {
      auto op_duration_usec =
          make_unsigned(MonoDelta(MonoTime::Now() - start_time_).ToMicroseconds());
      metrics_->Increment(tablet::TabletEventStats::kQlWriteLatency, op_duration_usec);
    }
  }

  auto& metadata = *tablet->metadata();

  for (const auto& sv : operation->request()->write_batch().table_schema_version()) {
    if (!status.IsAborted()) {
      auto schema_version = 0;
      if (sv.table_id().empty()) {
        schema_version = metadata.primary_table_schema_version();
      } else {
        auto uuid = Uuid::FromSlice(sv.table_id());
        CHECK(uuid.ok());
        auto schema_version_result = metadata.schema_version(*uuid);
        if (!schema_version_result.ok()) {
          Complete(schema_version_result.status());
          return;
        }
        schema_version = *schema_version_result;
      }
      CHECK_LE(schema_version, sv.schema_version()) << ", status: " << status;
    }
  }

  Complete(status);
}

void WriteQuery::Cancel(const Status& status) {
  if (operation_) {
    operation_->ResetPreparingToken();
  } else {
    LOG(DFATAL) << "Cancelled submitted operation: " << status;
  }

  Complete(status);
}

void WriteQuery::Complete(const Status& status) {
  Release();
  InvokeCallback(status);
}

void WriteQuery::InvokeCallback(const Status& status) {
  if (callback_) {
    callback_(status);
    callback_ = nullptr;
  }
}

void WriteQuery::ExecuteDone(const Status& status) {
  VLOG_WITH_FUNC(4) << "status: " << status;

  docdb_locks_ = std::move(prepare_result_.lock_batch);
  scoped_read_operation_.Reset();
  // Reset request_scope_ using RAII here to prevent it from blocking transaction cleanup.
  auto request_scope = std::move(request_scope_);
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
    case ExecuteMode::kPgsqlLock:
      PgsqlLockExecuteDone(status);
      return;
  }
  FATAL_INVALID_ENUM_VALUE(ExecuteMode, execute_mode_);
}

Result<bool> WriteQuery::PrepareExecute() {
  if (client_request_) {
    auto* request = operation().AllocateRequest();
    SetupKeyValueBatch(*client_request_, request);
    if (client_request_->has_start_time_micros()) {
      SetRequestStartUs(client_request_->start_time_micros());
    }

    if (!client_request_->redis_write_batch().empty()) {
      return RedisPrepareExecute();
    }

    if (!client_request_->ql_write_batch().empty()) {
      return CqlPrepareExecute();
    }

    if (!client_request_->pgsql_write_batch().empty()) {
      return PgsqlPrepareExecute();
    }

    if (!client_request_->pgsql_lock_batch().empty()) {
      return PgsqlPrepareLock();
    }

    if (client_request_->has_write_batch() && client_request_->has_external_hybrid_time()) {
      return ExternalWritePrepareExecute();
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
  scoped_read_operation_ = tablet->CreateScopedRWOperationNotBlockingRocksDbShutdownStart();
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

Result<bool> WriteQuery::CqlRePrepareExecuteIfNecessary() {
  auto tablet = VERIFY_RESULT(tablet_safe());
  auto& metadata = *tablet->metadata();
  VLOG_WITH_FUNC(2) << "Schema version for " << metadata.table_name() << ": "
                    << metadata.primary_table_schema_version();
  // Check if the schema version set in client_request_->ql_write_batch() is compatible with
  // the current schema pointed to by the tablet's metadata.
  if (!VERIFY_RESULT(CqlCheckSchemaVersion())) {
    return false;
  }

  SCHECK_EQ(
      request().write_batch().table_schema_version_size(), 1,
      IllegalState, "Unexpected value encountered for write_batch().table_schema_version_size()");
  auto* write_batch = request().mutable_write_batch();
  const auto& schema_version = write_batch->table_schema_version().front().schema_version();
  if (schema_version == metadata.primary_table_schema_version()) {
    return true;
  }
  // It could still happen that the schema version set in request() is one behind the current
  // schema. In such a case, reset the version and re-form the doc operations poiting to the
  // latest schema.
  //
  // Note that 'CqlCheckSchemaVersion' doesn't catch this since it checks the compatibility
  // of requests in ql_write_batch with that of the current metadata and doesn't check the
  // schema compatibility for operations in 'request().write_batch()'.
  SCHECK_EQ(
      schema_version + 1, metadata.primary_table_schema_version(),
      IllegalState, "Expected current schema version to be ahead by at most 1");
  write_batch->mutable_table_schema_version()->Clear();
  docdb::AddTableSchemaVersion(
      Uuid::Nil(), metadata.primary_table_schema_version(), request().mutable_write_batch());
  RETURN_NOT_OK(
      CqlPopulateDocOps(tablet, client_request_, &doc_ops_, response_, true /* reset_ops */));
  return true;
}

Result<bool> WriteQuery::CqlPrepareExecute() {
  auto tablet = VERIFY_RESULT(tablet_safe());
  RETURN_NOT_OK(InitExecute(ExecuteMode::kCql));

  auto& metadata = *tablet->metadata();
  VLOG_WITH_FUNC(2) << "Schema version for " << metadata.table_name() << ": "
                    << metadata.primary_table_schema_version();

  if (!VERIFY_RESULT(CqlCheckSchemaVersion())) {
    return false;
  }

  docdb::AddTableSchemaVersion(
      Uuid::Nil(), metadata.primary_table_schema_version(), request().mutable_write_batch());
  RETURN_NOT_OK(CqlPopulateDocOps(tablet, client_request_, &doc_ops_, response_));
  return true;
}

Result<bool> WriteQuery::PgsqlPrepareExecute() {
  auto tablet = VERIFY_RESULT(tablet_safe());
  RETURN_NOT_OK(InitExecute(ExecuteMode::kPgsql));

  if (!VERIFY_RESULT(PgsqlCheckSchemaVersion())) {
    return false;
  }

  const auto& pgsql_write_batch = client_request_->pgsql_write_batch();

  doc_ops_.reserve(pgsql_write_batch.size());

  TransactionOperationContext txn_op_ctx;

  auto& metadata = *tablet->metadata();
  // Colocated via DB/tablegroup/syscatalog.
  bool colocated = metadata.colocated() || tablet->is_sys_catalog();

  for (const auto& req : pgsql_write_batch) {
    auto* resp = response_->add_pgsql_response_batch();
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

Result<bool> WriteQuery::PgsqlPrepareLock() {
  auto& write_batch = client_request_->write_batch();
  // Transaction should be specified in the write batch.
  // TODO(advisory-lock #24709) - virtual transaction if it's session level.
  SCHECK(write_batch.has_transaction(), InvalidArgument, "No transaction found in write batch");

  auto tablet = VERIFY_RESULT(tablet_safe());
  RETURN_NOT_OK(InitExecute(ExecuteMode::kPgsqlLock));

  const auto& pgsql_lock_batch = client_request_->pgsql_lock_batch();

  SCHECK(!pgsql_lock_batch.empty(), InvalidArgument, "lock batch is empty");
  auto& metadata = *tablet->metadata();
  const auto table_info = VERIFY_RESULT(metadata.GetTableInfo(""));

  doc_ops_.reserve(pgsql_lock_batch.size());

  TransactionOperationContext txn_op_ctx;
  VLOG_WITH_FUNC(2) << "transaction=" << write_batch.transaction().ShortDebugString();
  for (const auto& req : pgsql_lock_batch) {
    VLOG_WITH_FUNC(4) << req.ShortDebugString();
    if (doc_ops_.empty()) {
      txn_op_ctx = VERIFY_RESULT(tablet->CreateTransactionOperationContext(
          write_batch.transaction(),
          /* is_ysql_catalog_table */ false,
          &write_batch.subtransaction()));
    }
    auto* resp = response_->add_pgsql_response_batch();
    auto lock_op = std::make_unique<docdb::PgsqlLockOperation>(
        req,
        txn_op_ctx);
    RETURN_NOT_OK(lock_op->Init(resp, table_info->doc_read_context));
    doc_ops_.emplace_back(std::move(lock_op));
  }
  return client_request_->pgsql_lock_batch().begin()->is_lock();
}

Result<bool> WriteQuery::ExternalWritePrepareExecute() {
  auto& write_batch = client_request_->write_batch();
  if (write_batch.xcluster_used_schema_versions().empty()) {
    return false;
  }

  auto tablet = VERIFY_RESULT(tablet_safe());
  auto& metadata = *tablet->metadata();
  // Verify that the schema versions used in the write batch are present in the tablet metadata.
  for (const auto& used_schema_version : write_batch.xcluster_used_schema_versions()) {
    auto table_info_res = metadata.GetTableInfo(used_schema_version.colocation_id());
    if (!table_info_res.ok()) {
      if (used_schema_version.colocation_id() != kColocationIdNotSet) {
        // This is expected for colocated tables that are not yet created on the target.
        continue;
      }
      return table_info_res.status();
    }
    const auto table_info = *table_info_res;
    const auto& schema_packing_storage = table_info->doc_read_context->schema_packing_storage;
    for (const auto& schema_version : used_schema_version.schema_versions()) {
      SCHECK(
          schema_packing_storage.HasVersion(schema_version), IllegalState,
          Format(
              "Schema version $0 not found in schema packing storage: $1", schema_version,
              schema_packing_storage.VersionsToString()));
    }
  }
  return false;
}

void WriteQuery::Execute(std::unique_ptr<WriteQuery> query) {
  auto* query_ptr = query.get();
  query_ptr->self_ = std::move(query);

  auto prepare_result = query_ptr->PrepareExecute();
  VLOG_WITH_FUNC(4)
      << "Request: " << AsString(query_ptr->client_request_) << ", result: " << prepare_result;

  if (!prepare_result.ok()) {
    query_ptr->ExecuteDone(prepare_result.status());
    return;
  }

  if (!prepare_result.get()) {
    if (query_ptr->execute_mode_ == ExecuteMode::kPgsqlLock) {
      query_ptr->ExecuteDone(query_ptr->ExecuteUnlock());
      return;
    }
    StartSynchronization(std::move(query_ptr->self_), Status::OK());
    return;
  }

  auto status = query_ptr->DoExecute();
  if (!status.ok()) {
    query_ptr->ExecuteDone(status);
  }
}

// The conflict management policy (as defined in conflict_resolution.h) to be used is determined
// based on the following -
//   1. For explicit row level locking or advisory locks (is_advisory_lock_request == true):
//      YSQL sets the "wait_policy" field which maps to a corresponding ConflictManagementPolicy
//      as detailed in the WaitPolicy enum in common.proto.
//   2. For everything else, either the WAIT_ON_CONFLICT or the FAIL_ON_CONFLICT policy is used
//      based on whether wait queues are enabled or not.
docdb::ConflictManagementPolicy GetConflictManagementPolicy(
    const docdb::WaitQueue* wait_queue, const docdb::LWKeyValueWriteBatchPB& write_batch,
    bool is_advisory_lock_request) {
  // Either write_batch.read_pairs is not empty or doc_ops is non empty. Both can't be non empty
  // together. This is because read_pairs is filled only in case of a read operation that has a
  // row mark or is part of a serializable txn.
  // 1. In case doc_ops are present, we either use the WAIT_ON_CONFLICT or the FAIL_ON_CONFLICT
  //    policy based on whether wait queues are enabled or not.
  // 2. In case of a read rpc that has wait_policy, we use the corresponding conflict management
  //    policy.

  auto conflict_management_policy = wait_queue ? docdb::WAIT_ON_CONFLICT : docdb::FAIL_ON_CONFLICT;
  const auto& pairs = write_batch.read_pairs();
  if ((is_advisory_lock_request || !pairs.empty()) && write_batch.has_wait_policy()) {
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

Status WriteQuery::ExecuteUnlock() {
  bool unlock_all =
      client_request_->pgsql_lock_batch().begin()->lock_id().lock_range_column_values_size() == 0;
  if (unlock_all) {
    request().mutable_write_batch()->add_lock_pairs()->set_is_lock(false);
    return Status::OK();
  }
  auto tablet = VERIFY_RESULT(tablet_safe());
  docdb::DocWriteBatch doc_write_batch(
      tablet->doc_db(),
      docdb::InitMarkerBehavior::kOptional,
      scoped_read_operation_,
      tablet->monotonic_counter());
  docdb::DocOperationApplyData data;
  data.doc_write_batch = &doc_write_batch;
  for (const auto& doc_op : doc_ops_) {
    RETURN_NOT_OK(doc_op->Apply(data));
  }
  data.doc_write_batch->MoveLocksToWriteBatchPB(
      request().mutable_write_batch(), /* is_lock= */ false);
  return Status::OK();
}

Status WriteQuery::DoExecute() {
  auto tablet = VERIFY_RESULT(tablet_safe());
  auto& write_batch = *request().mutable_write_batch();
  // Lock request must have non-empty pgsql_lock_batch.
  bool is_advisory_lock_request = client_request_ && !client_request_->pgsql_lock_batch().empty();
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

  auto* transaction_participant = tablet->transaction_participant();

  // Doesn't support skip prefix lock for CQL, Redis, kPgsqlLock and sys catlog. For them, the locks
  // are unaffected, regardless of the skip prefix lock setting. Also skip prefix lock is disabled
  // when packed row is false or for fast-path transaction.
  const bool is_pgsql_transaction_with_isolation =
      tablet->table_type() == TableType::PGSQL_TABLE_TYPE &&
      (execute_mode_ == ExecuteMode::kPgsql || execute_mode_ == ExecuteMode::kSimple) &&
      transactional_table && !tablet->is_sys_catalog() &&
      isolation_level_ != IsolationLevel::NON_TRANSACTIONAL;
  dockv::SkipPrefixLocks skip_prefix_locks = dockv::SkipPrefixLocks::kFalse;
  if (is_pgsql_transaction_with_isolation) {
    auto transaction_id = VERIFY_RESULT(FullyDecodeTransactionId(
      write_batch.transaction().transaction_id()));

    bool should_skip_prefix_locks = FLAGS_skip_prefix_locks &&
        FLAGS_skip_prefix_locks_for_upgrade && FLAGS_ysql_enable_packed_row;
    fast_mode_txn_scope_ = VERIFY_RESULT(transaction_participant->ShouldUseFastMode(
        isolation_level_, should_skip_prefix_locks, transaction_id));
    should_skip_prefix_locks = SkipPrefixLocks(fast_mode_txn_scope_.active(), isolation_level_);
    write_batch.mutable_transaction()->set_skip_prefix_locks(should_skip_prefix_locks);
    skip_prefix_locks = should_skip_prefix_locks ?
        dockv::SkipPrefixLocks::kTrue : dockv::SkipPrefixLocks::kFalse;
    VLOG_WITH_FUNC(4) << "Choose skip_prefix_locks:" << skip_prefix_locks << ", isolation_level:"
        << isolation_level_ << ", transaction id:" << transaction_id;
    if (skip_prefix_locks && isolation_level_ == IsolationLevel::SERIALIZABLE_ISOLATION &&
        !FLAGS_skip_prefix_locks_suppress_warning_for_serializable_op) {
      LOG(WARNING) << "The skip_prefix_locks gflag is enabled while there are active serializable "
                   << "isolation transactions. Please consider turning it off since a serializable "
                   << "transaction in skip_prefix_locks mode can block all other transactions that "
                   << "access/ modify the same test. To suppress this warning, set the gflag "
                   << "skip_prefix_locks_suppress_warning_for_serializable_op to true. "
                   << "transaction id:" << transaction_id;
    }
  }

  dockv::PartialRangeKeyIntents partial_range_key_intents(metadata.UsePartialRangeKeyIntents());
  prepare_result_ = VERIFY_RESULT(docdb::PrepareDocWriteOperation(
      doc_ops_, write_batch.read_pairs(), metrics_, isolation_level_, row_mark_type,
      transactional_table, write_batch.has_transaction(), deadline(), partial_range_key_intents,
      tablet->shared_lock_manager(), skip_prefix_locks));

  DEBUG_ONLY_TEST_SYNC_POINT("WriteQuery::DoExecute::PreparedDocWriteOps");

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
    CompleteExecute(HybridTime::kInvalid);
    return Status::OK();
  }

  // The request_id field should be populated for all write requests, but any read requests which
  // trigger a write_query will not have it populated. In this case, we use -1 as the request_id,
  // and conflict_resolution passes the serial_no to the wait queue as the request_id.
  DCHECK(request().has_request_id() || request().write_batch().write_pairs_size() == 0);
  auto request_id = request().has_request_id() ? request().request_id() : -1;

  if (isolation_level_ == IsolationLevel::NON_TRANSACTIONAL) {
    SCHECK(!is_advisory_lock_request, InvalidArgument,
           "Unexpected advisory lock request with NON_TRANSACTIONAL isolation level");
    auto now = tablet->clock()->Now();
    auto conflict_management_policy = GetConflictManagementPolicy(
        wait_queue, write_batch, is_advisory_lock_request);

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
        doc_ops_, conflict_management_policy, write_batch, request_scope_, now, request_start_us(),
        request_id, tablet->doc_db(), partial_range_key_intents, transaction_participant, metrics_,
        &prepare_result_.lock_batch, wait_queue, deadline(),
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
    DocPaths paths;
    for (const auto& doc_op : doc_ops_) {
      IsolationLevel ignored_isolation_level;
      RETURN_NOT_OK(doc_op->GetDocPaths(
              docdb::GetDocPathsMode::kLock, &paths, &ignored_isolation_level));
      boost::tribool pk_is_known =
          doc_op->OpType() == docdb::DocOperation::Type::PGSQL_WRITE_OPERATION ?
          boost::tribool(down_cast<const docdb::PgsqlWriteOperation&>(*doc_op).pk_is_known()) :
          boost::indeterminate;
      AddReadPairs(paths, &write_batch, pk_is_known);
      paths.clear();
    }
  }

  auto conflict_management_policy = GetConflictManagementPolicy(
      wait_queue, write_batch, is_advisory_lock_request);

  SCHECK(!write_batch.has_background_transaction_id() || is_advisory_lock_request,
      IllegalState, "background_transaction_id should only be set for advisory lock requests.");

  // TODO(wait-queues): Ensure that wait_queue respects deadline() during conflict resolution.
  VLOG(5) << "Going to call ResolveTransactionConflicts with read_time: "
          << (read_time_ ? read_time_.read : HybridTime::kMax);
  return docdb::ResolveTransactionConflicts(
      doc_ops_, conflict_management_policy, write_batch, request_scope_, tablet->clock()->Now(),
      read_time_ ? read_time_.read : HybridTime::kMax, write_batch.transaction().pg_txn_start_us(),
      request_start_us(), request_id, tablet->doc_db(), partial_range_key_intents,
      transaction_participant, metrics_,
      &prepare_result_.lock_batch, wait_queue, is_advisory_lock_request, deadline(),
      [this](const Result<HybridTime>& result) {
        if (!result.ok()) {
          ExecuteDone(result.status());
          TRACE("ExecuteDone");
          return;
        }
        TransactionalConflictsResolved();
        TRACE("TransactionalConflictsResolved");
      },
      skip_prefix_locks);
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

  CompleteExecute(HybridTime::kInvalid);
}

void WriteQuery::TransactionalConflictsResolved() {
  auto status = DoTransactionalConflictsResolved();
  if (!status.ok()) {
    LOG(WARNING) << status;
    ExecuteDone(status);
  }
}

Status WriteQuery::DoTransactionalConflictsResolved() {
  HybridTime safe_time;
  if (!read_time_) {
    auto tablet = VERIFY_RESULT(tablet_safe());
    safe_time = VERIFY_RESULT(tablet->SafeTime(RequireLease::kTrue));
    read_time_ = ReadHybridTime::FromHybridTimeRange(
        {safe_time, tablet->clock()->NowRange().second});
    metrics_->Increment(tablet::TabletCounters::kPickReadTimeOnDocDB);
  } else if (prepare_result_.need_read_snapshot &&
             isolation_level_ == IsolationLevel::SERIALIZABLE_ISOLATION) {
    return STATUS_FORMAT(
        InvalidArgument,
        "Read time should NOT be specified for serializable isolation level: $0",
        read_time_);
  }

  CompleteExecute(safe_time);
  return Status::OK();
}

void WriteQuery::CompleteExecute(HybridTime safe_time) {
  ExecuteDone(DoCompleteExecute(safe_time));
}

Status WriteQuery::DoCompleteExecute(HybridTime safe_time) {
  auto tablet = VERIFY_RESULT(tablet_safe());
  if (prepare_result_.need_read_snapshot && !read_time_) {
    // A read_time will be picked by the below ScopedReadOperation::Create() call.
    metrics_->Increment(tablet::TabletCounters::kPickReadTimeOnDocDB);
  }
  // For WriteQuery requests with execution mode kCql and kPgsql, we perform schema version checks
  // in two places:
  // 1. pre conflict resolution - as part of CqlPrepareExecute/ PgsqlPrepareExecute
  // 2. post conflict resolution - as part of ExecuteSchemaVersionCheck
  //
  // We acquire the write permit here just before performing the checks in 2. so as to block alter
  // schema requests until the current request gets submitted to the preparer queue for replication.
  //
  // Note: Acquiring the write permit pre conflict resolution could lead to other issues.
  // Refer https://github.com/yugabyte/yugabyte-db/issues/20730 for details.
  if (PREDICT_TRUE(!GetAtomicFlag(&FLAGS_disable_alter_vs_write_mutual_exclusion))) {
    auto write_permit = tablet->GetPermitToWrite(deadline());
    RETURN_NOT_OK(write_permit);
    // Save the write permit to be released after the operation is submitted
    // to Raft queue.
    UseSubmitToken(std::move(write_permit));
  }

  if (!VERIFY_RESULT(ExecuteSchemaVersionCheck())) {
    DCHECK(schema_version_mismatch_) << "Expected schema_version_mismatch_ to be set";
    return Status::OK();
  }

  auto read_op = prepare_result_.need_read_snapshot
      ? VERIFY_RESULT(ScopedReadOperation::Create(tablet.get(),
                                                  RequireLease::kTrue,
                                                  read_time_))
      : ScopedReadOperation();

  docdb::ReadOperationData read_operation_data {
    .deadline = deadline(),
    .read_time = prepare_result_.need_read_snapshot
        ? read_op.read_time()
        // When need_read_snapshot is false, this time is used only to write TTL field of record.
        : ReadHybridTime::SingleTime(tablet->clock()->Now()),
    .statistics = &scoped_statistics_,
  };
  read_operation_data.read_time.serial_no = request_scope_.request_id();

  // We expect all read operations for this transaction to be done in AssembleDocWriteBatch. Once
  // read_txn goes out of scope, the read point is deregistered.
  bool local_limit_updated = false;

  // This loop may be executed multiple times only for serializable isolation or
  // when read_time was not yet picked for snapshot isolation.
  // In all other cases it is executed only once.
  auto init_marker_behavior = tablet->table_type() == TableType::REDIS_TABLE_TYPE
      ? docdb::InitMarkerBehavior::kRequired
      : docdb::InitMarkerBehavior::kOptional;
  auto& write_batch = *request().mutable_write_batch();
  for (;;) {
    RETURN_NOT_OK(docdb::AssembleDocWriteBatch(
        doc_ops_, read_operation_data, tablet->doc_db(), &tablet->GetSchemaPackingProvider(),
        scoped_read_operation_, &write_batch, init_marker_behavior,
        tablet->monotonic_counter(), &read_restart_data_, tablet->metadata()->table_name()));

    // For serializable isolation we don't fix read time, so could do read restart locally,
    // instead of failing whole transaction.
    if (!read_restart_data_.is_valid() || !allow_immediate_read_restart_) {
      break;
    }

    read_operation_data.read_time.read = read_restart_data_.restart_time;
    if (!local_limit_updated) {
      local_limit_updated = true;
      safe_time = VERIFY_RESULT(tablet->SafeTime(RequireLease::kTrue));
      read_operation_data.read_time.local_limit = std::min(
          read_operation_data.read_time.local_limit,
          safe_time);
    }

    read_restart_data_ = ReadRestartData();

    write_batch.mutable_write_pairs()->clear();

    for (auto& doc_op : doc_ops_) {
      doc_op->ClearResponse();
    }
  }
  VLOG_WITH_FUNC(4)
      << "Write batch: " << AsString(write_batch) << ", doc ops: " << AsString(doc_ops_);

  if (isolation_level_ == IsolationLevel::NON_TRANSACTIONAL) {
    return Status::OK();
  }

  if (allow_immediate_read_restart_ && response_) {
    read_operation_data.read_time.ToPB(response_->mutable_used_read_time());
  }

  // SERIALIZABLE operations already add the row to read_pairs for UPDATE operations
  // in DoExecute(), so we shouldn't be doing it again here.
  if (write_batch.write_pairs_size() &&
      isolation_level_ != IsolationLevel::SERIALIZABLE_ISOLATION) {
    DocPaths paths;
    for (const auto& doc_op : doc_ops_) {
      if (IsSkipped(*doc_op)) {
        continue;
      }
      IsolationLevel ignored_isolation_level;
      RETURN_NOT_OK(doc_op->GetDocPaths(
          docdb::GetDocPathsMode::kStrongReadIntents, &paths, &ignored_isolation_level));
      boost::tribool pk_is_known =
          doc_op->OpType() == docdb::DocOperation::Type::PGSQL_WRITE_OPERATION ?
          boost::tribool(down_cast<const docdb::PgsqlWriteOperation&>(*doc_op).pk_is_known()) :
          boost::indeterminate;
      AddReadPairs(paths, &write_batch, pk_is_known);
      paths.clear();
    }
  }
  return Status::OK();
}

Result<TabletPtr> WriteQuery::tablet_safe() const {
  // We cannot rely on using operation_->tablet_safe() as operation_ is moved to TabletPeer::Submit
  // at some point in the lifecycle of the WriteQuery, and wouldn't be a valid dereference/access.
  auto tablet = tablet_.lock();
  if (!tablet) {
    return STATUS_FORMAT(IllegalState, "Underlying tablet object might have been deallocated");
  }
  return tablet;
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

Result<bool> WriteQuery::CqlCheckSchemaVersion() {
  auto tablet_result = tablet_safe();
  if (!tablet_result.ok()) {
    YB_LOG_EVERY_N_SECS(WARNING, 1)
        << "Tablet has already been destroyed in WriteQuery::CqlCheckSchemaVersion.";
    return tablet_result.status();
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
      VLOG_WITH_FUNC(4) << "Schema version mismatch for: " << AsString(req);
      ++num_mismatches;
    }
    ++index;
  }

  if (num_mismatches != 0) {
    schema_version_mismatch_ = true;
    return false;
  }

  return true;
}

void WriteQuery::CqlRespondSchemaVersionMismatch() {
  constexpr auto error_code = QLResponsePB::YQL_STATUS_SCHEMA_VERSION_MISMATCH;
  const auto& req_batch = client_request_->ql_write_batch();
  auto& resp_batch = *response_->mutable_ql_response_batch();
  SchemaVersionMismatch(error_code, req_batch.size(), &resp_batch);
}

void WriteQuery::CqlExecuteDone(const Status& status) {
  if (restart_read_ht().is_valid() || schema_version_mismatch_) {
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
  Cancel(Status::OK());
}

Result<bool> WriteQuery::ExecuteSchemaVersionCheck() {
  switch (execute_mode_) {
    case ExecuteMode::kSimple: FALLTHROUGH_INTENDED;
    case ExecuteMode::kRedis:
    case ExecuteMode::kPgsqlLock:
      return true;
    case ExecuteMode::kCql:
      // For cql, the requests in client_request_->ql_write_batch() could have the field
      // 'is_compatible_with_previous_version' set, which allows processing of the Write when the
      // schema is at the previous version. It could have happened that an alter schema request got
      // processed between first schema version check done pre conflict resolution and acquring the
      // write permit. In such cases, we need to re-prepare the write operation considering the new
      // schema version. Else, the post replication check guarding the write query against schema
      // changes amidst its replication would fail. Hence a call to 'CqlCheckSchemaVersion' alone
      // doesn't gaurd us from failures/inconsistencies.
      return CqlRePrepareExecuteIfNecessary();
    case ExecuteMode::kPgsql:
      // Note: We don't have the above problem for pgsql as 'is_compatible_with_previous_version' is
      // not used for pgsql, and hence can go ahead with a plain schema version check. If an alter
      // schema was processed when this query underwent conflict resolution, the below check fails.
      return PgsqlCheckSchemaVersion();
  }
  FATAL_INVALID_ENUM_VALUE(ExecuteMode, execute_mode_);
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
  const ChildTransactionDataMsg* child_transaction_data = nullptr;
  client::YBMetaDataCache* metadata_cache;

  std::mutex mutex;
  WriteQuery::IndexOps index_ops GUARDED_BY(mutex);
  size_t counter GUARDED_BY(mutex) = 1;
  Status failure;

  explicit UpdateQLIndexesTask(WriteQuery* query_) : query(query_) {}

  Status Init(const TabletPtr& tablet, docdb::QLWriteOperation* write_op) {
    client = &tablet->client();
    session = client->NewSession(query->deadline());
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
    DCHECK_EQ(self.get(), this);

    // Apply the write ops to update the index
    {
      std::lock_guard lock(mutex);
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
      QLWriteRequestMsg* index_request, docdb::QLWriteOperation* write_op,
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

Result<bool> WriteQuery::PgsqlCheckSchemaVersion() {
  auto tablet_result = tablet_safe();
  if (!tablet_result.ok()) {
    YB_LOG_EVERY_N_SECS(WARNING, 1)
        << "Tablet has already been destroyed in WriteQuery::PgsqlCheckSchemaVersion.";
    return tablet_result.status();
  }
  auto tablet = *tablet_result;

  constexpr auto error_code = PgsqlResponsePB::PGSQL_STATUS_SCHEMA_VERSION_MISMATCH;
  auto& metadata = *tablet->metadata();
  const auto& req_batch = client_request_->pgsql_write_batch();
  auto& resp_batch = *response_->mutable_pgsql_response_batch();

  int index = 0;
  int num_mismatches = 0;
  for (const auto& req : req_batch) {
    auto table_info = VERIFY_RESULT(metadata.GetTableInfo(req.table_id()));
    if (!CheckSchemaVersion(
            table_info.get(), req.schema_version(),
            std::nullopt /* compatible_with_previous_version= */,
            error_code, index, &resp_batch)) {
      ++num_mismatches;
    }
    ++index;
  }

  if (num_mismatches != 0) {
    schema_version_mismatch_ = true;
    return false;
  }

  return true;
}

void WriteQuery::PgsqlRespondSchemaVersionMismatch() {
  constexpr auto error_code = PgsqlResponsePB::PGSQL_STATUS_SCHEMA_VERSION_MISMATCH;
  const auto& req_batch = client_request_->pgsql_write_batch();
  auto& resp_batch = *response_->mutable_pgsql_response_batch();
  SchemaVersionMismatch(error_code, req_batch.size(), &resp_batch);
}

void WriteQuery::PgsqlExecuteDone(const Status& status) {
  if (!status.ok() || read_restart_data_.is_valid() || schema_version_mismatch_) {
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

void WriteQuery::PgsqlLockExecuteDone(const Status& status) {
  StartSynchronization(std::move(self_), status);
}

void WriteQuery::IncrementActiveWriteQueryObjectsBy(int64_t value) {
  auto res = tablet_safe();
  if (res.ok() && (*res)->metrics()) {
    (*res)->metrics()->IncrementBy(tablet::TabletGauges::kActiveWriteQueryObjects, value);
    did_update_active_write_queries_metric_ = true;
  } else if (PREDICT_FALSE(did_update_active_write_queries_metric_)) {
    LOG(DFATAL) << "Unable to update kActiveWriteQueryObjects metric but had "
                << "previosuly contributed to it.";
  }
}

std::pair<PgsqlResponseMsg*, PgsqlMetricsCaptureType>
    WriteQuery::GetPgsqlResponseAndMetricsCapture() const {
  if (!pgsql_write_ops_.empty()) {
    auto& write_op = pgsql_write_ops_.at(0);
    auto metrics_capture = write_op->request().metrics_capture();
    if (GetAtomicFlag(&FLAGS_ysql_analyze_dump_metrics) &&
        metrics_capture != PgsqlMetricsCaptureType::PGSQL_METRICS_CAPTURE_NONE) {
      return {write_op->response(), metrics_capture};
    }
  }
  return {nullptr, PgsqlMetricsCaptureType::PGSQL_METRICS_CAPTURE_NONE};
}

}  // namespace tablet
}  // namespace yb
