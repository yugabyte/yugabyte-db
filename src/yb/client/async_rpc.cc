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

#include "yb/client/async_rpc.h"

#include "yb/client/batcher.h"
#include "yb/client/client_error.h"
#include "yb/client/in_flight_op.h"
#include "yb/client/meta_cache.h"
#include "yb/client/table.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/pgsql_error.h"
#include "yb/common/schema.h"
#include "yb/common/transaction.h"
#include "yb/common/transaction_error.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/casts.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/cast.h"
#include "yb/util/debug-util.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/result.h"
#include "yb/util/status_log.h"
#include "yb/util/sync_point.h"
#include "yb/util/trace.h"
#include "yb/util/yb_pg_errcodes.h"

// TODO: do we need word Redis in following two metrics? ReadRpc and WriteRpc objects emitting
// these metrics are used not only in Redis service.
METRIC_DEFINE_event_stats(
    server, handler_latency_yb_client_write_remote, "yb.client.Write remote call time",
    yb::MetricUnit::kMicroseconds, "Microseconds spent in the remote Write call ");
METRIC_DEFINE_event_stats(
    server, handler_latency_yb_client_read_remote, "yb.client.Read remote call time",
    yb::MetricUnit::kMicroseconds, "Microseconds spent in the remote Read call ");
METRIC_DEFINE_event_stats(
    server, handler_latency_yb_client_write_local, "yb.client.Write local call time",
    yb::MetricUnit::kMicroseconds, "Microseconds spent in the local Write call ");
METRIC_DEFINE_event_stats(
    server, handler_latency_yb_client_read_local, "yb.client.Read local call time",
    yb::MetricUnit::kMicroseconds, "Microseconds spent in the local Read call ");
METRIC_DEFINE_event_stats(
    server, handler_latency_yb_client_time_to_send,
    "Time taken for a Write/Read rpc to be sent to the server", yb::MetricUnit::kMicroseconds,
    "Microseconds spent before sending the request to the server");

METRIC_DEFINE_counter(server, consistent_prefix_successful_reads,
    "Number of consistent prefix reads that were served by the closest replica.",
    yb::MetricUnit::kRequests,
    "Number of consistent prefix reads that were served by the closest replica.");

METRIC_DEFINE_counter(server, consistent_prefix_failed_reads,
    "Number of consistent prefix reads that failed to be served by the closest replica.",
    yb::MetricUnit::kRequests,
    "Number of consistent prefix reads that failed to be served by the closest replica.");

DEFINE_RUNTIME_int32(ybclient_print_trace_every_n, 0,
    "Controls the rate at which traces from ybclient are printed. Setting this to 0 "
    "disables printing the collected traces.");
TAG_FLAG(ybclient_print_trace_every_n, advanced);

DEFINE_UNKNOWN_bool(forward_redis_requests, true,
    "If false, the redis op will not be served if it's not "
    "a local request. The op response will be set to the redis error "
    "'-MOVED partition_key 0.0.0.0:0'. This works with jedis which only looks at the MOVED "
    "part of the reply and ignores the rest. For now, if this flag is true, we will only "
    "attempt to read from leaders, so redis_allow_reads_from_followers will be ignored.");

DEFINE_UNKNOWN_bool(detect_duplicates_for_retryable_requests, true,
    "Enable tracking of write requests that prevents the same write from being applied twice.");

DEFINE_UNKNOWN_bool(ysql_forward_rpcs_to_local_tserver, false,
    "DEPRECATED. Feature has been removed");

DEFINE_test_flag(bool, asyncrpc_finished_set_timedout, false,
    "Whether to reset asyncrpc response status to Timedout.");

DEFINE_test_flag(bool, asyncrpc_common_response_check_fail_once, false,
    "For testing only. When set to true triggers AsyncRpc::Failure() with RuntimeError status "
    "inside AsyncRpcBase::CommonResponseCheck() and returns false from this method.");

// DEPRECATED. It is assumed that all t-servers and masters in the cluster has this capability.
// Remove it completely when it won't be necessary to support upgrade from releases which checks
// the existence on this capability.
DEFINE_CAPABILITY(PickReadTimeAtTabletServer, 0x8284d67b);

DECLARE_bool(collect_end_to_end_traces);

using namespace std::placeholders;

namespace yb::client::internal {

bool IsTracingEnabled() {
  auto *trace = Trace::CurrentTrace();
  return trace && (FLAGS_collect_end_to_end_traces || trace->end_to_end_traces_requested());
}

namespace {

const char* const kRead = "Read";
const char* const kWrite = "Write";
const char* const kRedis = "Redis";
const char* const kYCQL = "YCQL";
const char* const kYSQL = "YSQL";

bool LocalTabletServerOnly(const InFlightOps& ops) {
  const auto op_type = ops.front().yb_op->type();
  return ((op_type == YBOperation::Type::REDIS_READ || op_type == YBOperation::Type::REDIS_WRITE) &&
          !FLAGS_forward_redis_requests);
}

void FillRequestIds(const RetryableRequestId request_id, InFlightOps* ops) {
  for (auto& op : *ops) {
    op.yb_op->set_request_id(request_id);
  }
}

void DoCheckResponseCount(
    const char* op, const char* name, int found, int expected, Status* status) {
  if (found == expected) {
    return;
  }
  auto msg = Format(". $0 $1 requests sent, $2 responses received", expected, name, found);
  if (status->ok()) {
    *status = STATUS_FORMAT(IllegalState, "$0 response count mismatch$1", op, msg);
  } else {
    *status = status->CloneAndAppend(msg);
  }
}

} // namespace

AsyncRpcMetrics::AsyncRpcMetrics(const scoped_refptr<yb::MetricEntity>& entity)
    : remote_write_rpc_time(METRIC_handler_latency_yb_client_write_remote.Instantiate(entity)),
      remote_read_rpc_time(METRIC_handler_latency_yb_client_read_remote.Instantiate(entity)),
      local_write_rpc_time(METRIC_handler_latency_yb_client_write_local.Instantiate(entity)),
      local_read_rpc_time(METRIC_handler_latency_yb_client_read_local.Instantiate(entity)),
      time_to_send(METRIC_handler_latency_yb_client_time_to_send.Instantiate(entity)),
      consistent_prefix_successful_reads(
          METRIC_consistent_prefix_successful_reads.Instantiate(entity)),
      consistent_prefix_failed_reads(METRIC_consistent_prefix_failed_reads.Instantiate(entity)) {
}

AsyncRpc::AsyncRpc(
    const AsyncRpcData& data, YBConsistencyLevel yb_consistency_level)
    : Rpc(data.batcher->deadline(), data.batcher->messenger(), &data.batcher->proxy_cache()),
      batcher_(data.batcher),
      ops_(data.ops),
      tablet_invoker_(LocalTabletServerOnly(ops_),
                      yb_consistency_level == YBConsistencyLevel::CONSISTENT_PREFIX,
                      data.batcher->client_,
                      this,
                      this,
                      data.tablet,
                      table(),
                      mutable_retrier(),
                      trace_.get()),
      start_(CoarseMonoClock::Now()),
      async_rpc_metrics_(data.batcher->async_rpc_metrics()) {
  mutable_retrier()->mutable_controller()->set_allow_local_calls_in_curr_thread(
      data.allow_local_calls_in_curr_thread);
}

AsyncRpc::~AsyncRpc() {
  if (trace_) {
    if (trace_->must_print()) {
      LOG(INFO) << ToString() << " took " << ToMicroseconds(CoarseMonoClock::Now() - start_)
                << "us. Trace:\n"
                << trace_->DumpToString(true);
    } else {
      const auto print_trace_every_n = GetAtomicFlag(&FLAGS_ybclient_print_trace_every_n);
      if (print_trace_every_n > 0) {
        YB_LOG_EVERY_N(INFO, print_trace_every_n)
            << ToString() << " took " << ToMicroseconds(CoarseMonoClock::Now() - start_)
            << "us. Trace:\n"
            << trace_->DumpToString(true);
      }
    }
  }
}

void AsyncRpc::SendRpc() {
  TRACE_TO(trace_, "SendRpc() called.");

  retained_self_ = shared_from_this();
  // For now, if this is a retry, execute this rpc on the leader even if
  // the consistency level is YBConsistencyLevel::CONSISTENT_PREFIX or
  // FLAGS_redis_allow_reads_from_followers is set to true.
  // TODO(hector): Temporarily blacklist the follower that couldn't serve the read so we can retry
  // on another follower.
  if (async_rpc_metrics_ && num_attempts() > 1 && tablet_invoker_.is_consistent_prefix()) {
    IncrementCounter(async_rpc_metrics_->consistent_prefix_failed_reads);
  }
  tablet_invoker_.Execute(std::string(), num_attempts() > 1);
}

std::string AsyncRpc::ToString() const {
  const auto& transaction = batcher_->in_flight_ops().metadata.transaction;
  const auto subtransaction_opt = batcher_->in_flight_ops().metadata.subtransaction;
  return Format("$0(tablet: $1, num_ops: $2, num_attempts: $3, txn: $4, subtxn: $5)",
                ops_.front().yb_op->read_only() ? "Read" : "Write",
                tablet().tablet_id(), ops_.size(), num_attempts(),
                transaction.transaction_id,
                subtransaction_opt
                    ? Format("$0", subtransaction_opt->subtransaction_id)
                    : "[none]");
}

std::shared_ptr<const YBTable> AsyncRpc::table() const {
  // All of the ops for a given tablet obviously correspond to the same table,
  // so we'll just grab the table from the first.
  return ops_[0].yb_op->table();
}

void AsyncRpc::Finished(const Status& status) {
  VLOG_WITH_FUNC(4) << "status: " << status << ", error: " << AsString(response_error());
  Status new_status = status;
  if (status.ok()) {
    if (PREDICT_FALSE(ANNOTATE_UNPROTECTED_READ(FLAGS_TEST_asyncrpc_finished_set_timedout))) {
      new_status = STATUS(
          TimedOut, "Fake TimedOut for testing due to FLAGS_TEST_asyncrpc_finished_set_timedout");
      DEBUG_ONLY_TEST_SYNC_POINT("AsyncRpc::Finished:SetTimedOut:1");
      DEBUG_ONLY_TEST_SYNC_POINT("AsyncRpc::Finished:SetTimedOut:2");
    }

  }
  if (tablet_invoker_.Done(&new_status)) {
    if (tablet().is_split() ||
        ClientError(new_status) == ClientErrorCode::kTablePartitionListIsStale) {
      ops_[0].yb_op->MarkTablePartitionListAsStale();
    }
    if (async_rpc_metrics_ && status.ok() && tablet_invoker_.is_consistent_prefix()) {
      IncrementCounter(async_rpc_metrics_->consistent_prefix_successful_reads);
    }
    ProcessResponseFromTserver(new_status);
    batcher_->Flushed(ops_, new_status, MakeFlushExtraResult());
    retained_self_.reset();
  }
}

void AsyncRpc::Failed(const Status& status) {
  VLOG_WITH_FUNC(4) << "status: " << status.ToString();
  std::string error_message = status.message().ToBuffer();
  auto redis_error_code = status.IsInvalidCommand() || status.IsInvalidArgument() ?
      RedisResponsePB_RedisStatusCode_PARSING_ERROR : RedisResponsePB_RedisStatusCode_SERVER_ERROR;
  for (auto& op : ops_) {
    YBOperation* yb_op = op.yb_op.get();
    switch (yb_op->type()) {
      case YBOperation::Type::REDIS_READ: FALLTHROUGH_INTENDED;
      case YBOperation::Type::REDIS_WRITE: {
        RedisResponsePB* resp;
        if (yb_op->type() == YBOperation::Type::REDIS_READ) {
          resp = down_cast<YBRedisReadOp*>(yb_op)->mutable_response();
        } else {
          resp = down_cast<YBRedisWriteOp*>(yb_op)->mutable_response();
        }
        resp->Clear();
        // If the tserver replied it is not the leader, respond that the key has moved. We do not
        // need to return the address of the new leader because the Redis client will refresh the
        // cluster map instead.
        if (status.IsIllegalState()) {
          resp->set_code(RedisResponsePB_RedisStatusCode_SERVER_ERROR);
          resp->set_error_message(Format("MOVED $0 0.0.0.0:0",
                                         down_cast<YBRedisOp*>(yb_op)->hash_code()));
        } else {
          resp->set_code(redis_error_code);
          resp->set_error_message(error_message);
        }
        break;
      }
      case YBOperation::Type::QL_READ: FALLTHROUGH_INTENDED;
      case YBOperation::Type::QL_WRITE: {
        QLResponsePB* resp = down_cast<YBqlOp*>(yb_op)->mutable_response();
        resp->Clear();
        resp->set_status(status.IsTryAgain() ? QLResponsePB::YQL_STATUS_RESTART_REQUIRED_ERROR
                                             : QLResponsePB::YQL_STATUS_RUNTIME_ERROR);
        resp->set_error_message(error_message);
        break;
      }
      case YBOperation::Type::PGSQL_READ: FALLTHROUGH_INTENDED;
      case YBOperation::Type::PGSQL_WRITE: {
        PgsqlResponsePB* resp = down_cast<YBPgsqlOp*>(yb_op)->mutable_response();
        resp->set_status(status.IsTryAgain() ? PgsqlResponsePB::PGSQL_STATUS_RESTART_REQUIRED_ERROR
                                             : PgsqlResponsePB::PGSQL_STATUS_RUNTIME_ERROR);
        // TODO(14814, 18387): At the moment only one error status is supported.
        resp->mutable_error_status()->Clear();
        StatusToPB(status, resp->add_error_status());
        // For backward compatibility set also deprecated fields
        resp->set_error_message(error_message);
        const uint8_t* pg_err_ptr = status.ErrorData(PgsqlErrorTag::kCategory);
        if (pg_err_ptr != nullptr) {
          resp->set_pg_error_code(static_cast<uint32_t>(PgsqlErrorTag::Decode(pg_err_ptr)));
        } else {
          resp->set_pg_error_code(static_cast<uint32_t>(YBPgErrorCode::YB_PG_INTERNAL_ERROR));
        }
        const uint8_t* txn_err_ptr = status.ErrorData(TransactionErrorTag::kCategory);
        if (txn_err_ptr != nullptr) {
          resp->set_txn_error_code(static_cast<uint16_t>(TransactionErrorTag::Decode(txn_err_ptr)));
        } else {
          resp->set_txn_error_code(static_cast<uint16_t>(TransactionErrorCode::kNone));
        }
        break;
      }
      default:
        LOG(FATAL) << "Unsupported operation " << yb_op->type();
        break;
    }
  }
}

bool AsyncRpc::IsLocalCall() const {
  return tablet_invoker_.IsLocalCall();
}

namespace {

template<class T>
void SetMetadata(const InFlightOpsTransactionMetadata& metadata,
                 bool need_full_metadata,
                 T* dest) {
  auto* transaction = dest->mutable_transaction();
  if (need_full_metadata) {
    metadata.transaction.ToPB(transaction);
  } else {
    metadata.transaction.TransactionIdToPB(transaction);
  }
  transaction->set_pg_txn_start_us(metadata.transaction.pg_txn_start_us);
  dest->set_deprecated_may_have_metadata(true);

  if (metadata.subtransaction && !metadata.subtransaction->IsDefaultState()) {
    metadata.subtransaction->ToPB(dest->mutable_subtransaction());
  }
}

void SetMetadata(const InFlightOpsTransactionMetadata& metadata,
                 bool need_full_metadata,
                 tserver::WriteRequestPB* req) {
  SetMetadata(metadata, need_full_metadata, req->mutable_write_batch());
}

} // namespace

void AsyncRpc::SendRpcToTserver(int attempt_num) {
  if (async_rpc_metrics_) {
    async_rpc_metrics_->time_to_send->Increment(ToMicroseconds(CoarseMonoClock::Now() - start_));
  }
  CallRemoteMethod();
}

template <class Req, class Resp>
AsyncRpcBase<Req, Resp>::AsyncRpcBase(
    const AsyncRpcData& data, YBConsistencyLevel consistency_level)
    : AsyncRpc(data, consistency_level) {
  req_.set_allocated_tablet_id(const_cast<std::string*>(&tablet_invoker_.tablet()->tablet_id()));
  req_.set_include_trace(IsTracingEnabled());
  const ConsistentReadPoint* read_point = batcher_->read_point();
  bool has_read_time = false;
  if (read_point) {
    req_.set_propagated_hybrid_time(read_point->Now().ToUint64());
    // Set read time for consistent read only if the table is transaction-enabled and
    // consistent read is required.
    if (data.need_consistent_read &&
        table()->InternalSchema().table_properties().is_transactional()) {
      auto read_time = read_point->GetReadTime(tablet_invoker_.tablet()->tablet_id());
      if (read_time) {
        has_read_time = true;
        read_time.AddToPB(&req_);
      }
    }
  }
  if (!ops_.empty()) {
    req_.set_batch_idx(ops_.front().batch_idx);
  }
  const auto& metadata = batcher_->in_flight_ops().metadata;
  if (!metadata.transaction.transaction_id.IsNil()) {
    SetMetadata(metadata, data.need_metadata, &req_);
    bool serializable = metadata.transaction.isolation == IsolationLevel::SERIALIZABLE_ISOLATION;
    LOG_IF(DFATAL, has_read_time && serializable)
        << "Read time should NOT be specified for serializable isolation: "
        << read_point->GetReadTime().ToString();
  }
}

template <class Req, class Resp>
AsyncRpcBase<Req, Resp>::~AsyncRpcBase() {
  req_.release_tablet_id();
}

template <class Req, class Resp>
bool AsyncRpcBase<Req, Resp>::CommonResponseCheck(const Status& status) {
  if (PREDICT_FALSE(ANNOTATE_UNPROTECTED_READ(
      FLAGS_TEST_asyncrpc_common_response_check_fail_once))) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_asyncrpc_common_response_check_fail_once) = false;
    const auto status = STATUS(RuntimeError, "CommonResponseCheck test runtime error");
    LOG_WITH_FUNC(INFO) << "Generating failure: " << status;
    Failed(status);
    return false;
  }
  if (!status.ok()) {
    return false;
  }
  if (resp_.has_error()) {
    LOG(WARNING) << ToString() << " has error:" << resp_.error().DebugString()
                 << ". Requests not processed.";
    // If there is an error at the Rpc itself, there should be no individual responses.
    // All of them need to be marked as failed.
    Failed(StatusFromPB(resp_.error().status()));
    return false;
  }
  auto restart_read_time = ReadHybridTime::FromRestartReadTimePB(resp_);
  if (restart_read_time) {
    auto read_point = batcher_->read_point();
    if (read_point) {
      read_point->RestartRequired(req_.tablet_id(), restart_read_time);
    }
    Failed(STATUS(TryAgain, Format("Restart read required at: $0", restart_read_time), Slice(),
                  TransactionError(TransactionErrorCode::kReadRestartRequired)));
    return false;
  }
  auto local_limit_ht = resp_.local_limit_ht();
  if (local_limit_ht) {
    auto read_point = batcher_->read_point();
    if (read_point) {
      read_point->UpdateLocalLimit(req_.tablet_id(), HybridTime(local_limit_ht));
    }
  }
  return true;
}

template <class Req, class Resp>
void AsyncRpcBase<Req, Resp>::SendRpcToTserver(int attempt_num) {
  req_.set_rejection_score(batcher_->RejectionScore(attempt_num));
  AsyncRpc::SendRpcToTserver(attempt_num);
}

template <class Req, class Resp>
void AsyncRpcBase<Req, Resp>::ProcessResponseFromTserver(const Status& status) {
  TRACE_TO(trace_, "ProcessResponseFromTserver($0)", status.ToString(false));
  if (resp_.has_trace_buffer()) {
    TRACE_TO(
        trace_, "Received from server: \n BEGIN AsyncRpc\n$0 END AsyncRpc", resp_.trace_buffer());
  }
  NotifyBatcher(status);
  if (!CommonResponseCheck(status)) {
    VLOG_WITH_FUNC(4) << "CommonResponseCheck failed, status: " << status;
    return;
  }
  auto swap_status = SwapResponses();
  if (!swap_status.ok()) {
    Failed(swap_status);
  }
}


template <class Req, class Resp>
FlushExtraResult AsyncRpcBase<Req, Resp>::MakeFlushExtraResult() {
  return {GetPropagatedHybridTime(resp_),
          resp_.has_used_read_time() ? ReadHybridTime::FromPB(resp_.used_read_time())
                                     : ReadHybridTime()};
}

template <class Op, class Req>
void HandleExtraFields(Op* op, Req* req) {
}

void HandleExtraFields(YBqlWriteOp* op, tserver::WriteRequestPB* req) {
  if (op->write_time_for_backfill()) {
    req->set_external_hybrid_time(op->write_time_for_backfill().ToUint64());
    ReadHybridTime::SingleTime(op->write_time_for_backfill()).ToPB(req->mutable_read_time());
  }
}

void HandleExtraFields(YBPgsqlWriteOp* op, tserver::WriteRequestPB* req) {
  if (op->write_time()) {
    req->set_external_hybrid_time(op->write_time().ToUint64());
  }
}

void HandleExtraFields(YBqlReadOp* op, tserver::ReadRequestPB* req) {
  if (op->read_time()) {
    op->read_time().AddToPB(req);
  }
}

template <class OpType, class Req, class Out>
void FillOps(
    const InFlightOps& ops, YBOperation::Type expected_type, Req* req, Out* out) {
  out->Reserve(narrow_cast<int>(ops.size()));
  size_t idx = 0;
  for (auto& op : ops) {
    CHECK_EQ(op.yb_op->type(), expected_type);
    auto* concrete_op = down_cast<OpType*>(op.yb_op.get());
    out->AddAllocated(concrete_op->mutable_request());
    HandleExtraFields(concrete_op, req);
    VLOG(5) << ++idx << ") encoded row: " << op.yb_op->ToString();
  }
}

Status AsyncRpc::CheckResponseCount(const char* op, const char* name, int found, int expected) {
  if (found >= expected) {
    batcher_->AddOpCountMismatchError();
    return STATUS_FORMAT(
        IllegalState, "Too many $0 responses: $1", expected);
  }

  return Status::OK();
}

Status AsyncRpc::CheckResponseCount(
    const char* op, int redis_found, int redis_expected, int ql_found, int ql_expected,
    int pgsql_found, int pgsql_expected) {
  Status result;
  DoCheckResponseCount(op, kRedis, redis_found, redis_expected, &result);
  DoCheckResponseCount(op, kYCQL, ql_found, ql_expected, &result);
  DoCheckResponseCount(op, kYSQL, pgsql_found, pgsql_expected, &result);
  if (!result.ok()) {
    LOG(DFATAL) << result;
    batcher_->AddOpCountMismatchError();
  }
  return result;
}

template <class Repeated>
void ReleaseOps(Repeated* repeated) {
  auto size = repeated->size();
  if (size) {
    repeated->ExtractSubrange(0, size, nullptr);
  }
}

WriteRpc::WriteRpc(const AsyncRpcData& data)
    : AsyncRpcBase(data, YBConsistencyLevel::STRONG) {
  TRACE_TO(trace_, "WriteRpc initiated");
  VTRACE_TO(1, trace_, "Tablet $0 table $1", data.tablet->tablet_id(), table()->name().ToString());

  // Add the rows
  switch (table()->table_type()) {
    case YBTableType::REDIS_TABLE_TYPE:
      FillOps<YBRedisWriteOp>(
          ops_, YBOperation::Type::REDIS_WRITE, &req_, req_.mutable_redis_write_batch());
      break;
    case YBTableType::YQL_TABLE_TYPE:
      FillOps<YBqlWriteOp>(
          ops_, YBOperation::Type::QL_WRITE, &req_, req_.mutable_ql_write_batch());
      break;
    case YBTableType::PGSQL_TABLE_TYPE:
      FillOps<YBPgsqlWriteOp>(
          ops_, YBOperation::Type::PGSQL_WRITE, &req_, req_.mutable_pgsql_write_batch());
      break;
    case YBTableType::UNKNOWN_TABLE_TYPE:
    case YBTableType::TRANSACTION_STATUS_TABLE_TYPE:
      LOG(DFATAL) << "Unsupported table type: " << table()->ToString();
      break;
  }

  VLOG(3) << "Created batch for " << data.tablet->tablet_id() << ":\n"
          << req_.ShortDebugString();

  if (batcher_->GetLeaderTerm() != OpId::kUnknownTerm) {
    req_.set_leader_term(batcher_->GetLeaderTerm());
  }

  const auto& client_id = batcher_->client_id();
  if (!client_id.IsNil() && FLAGS_detect_duplicates_for_retryable_requests) {
    auto temp = client_id.ToUInt64Pair();
    req_.set_client_id1(temp.first);
    req_.set_client_id2(temp.second);
    const auto& first_yb_op = ops_.begin()->yb_op;
    // That means we are trying to resend all ops from this RPC and need to reuse retryable
    // request ID and details (see https://github.com/yugabyte/yugabyte-db/issues/14005).
    if (first_yb_op->request_id()) {
      const auto& request_detail = batcher_->GetRequestDetails(*first_yb_op->request_id());
      req_.set_request_id(*first_yb_op->request_id());
      req_.set_min_running_request_id(request_detail.min_running_request_id);
      if (request_detail.start_time_micros > 0) {
        req_.set_start_time_micros(request_detail.start_time_micros);
      }
    } else {
      const auto request_pair = batcher_->NextRequestIdAndMinRunningRequestId();
      if (batcher_->Clock()) {
        req_.set_start_time_micros(batcher_->Clock()->Now().GetPhysicalValueMicros());
      }
      req_.set_request_id(request_pair.first);
      req_.set_min_running_request_id(request_pair.second);
      batcher_->RegisterRequest(request_pair.first, request_pair.second, req_.start_time_micros());
    }
    FillRequestIds(req_.request_id(), &ops_);
  }
}

WriteRpc::~WriteRpc() {
  if (async_rpc_metrics_) {
    scoped_refptr<EventStats> write_rpc_time = IsLocalCall() ?
                                                    async_rpc_metrics_->local_write_rpc_time :
                                                    async_rpc_metrics_->remote_write_rpc_time;
    write_rpc_time->Increment(ToMicroseconds(CoarseMonoClock::Now() - start_));
  }

  ReleaseOps(req_.mutable_redis_write_batch());
  ReleaseOps(req_.mutable_ql_write_batch());
  ReleaseOps(req_.mutable_pgsql_write_batch());
}

void WriteRpc::CallRemoteMethod() {
  auto trace = trace_; // It is possible that we receive reply before returning from WriteAsync.
                       // Since send happens before we return from WriteAsync.
                       // So under heavy load it is possible that our request is handled and
                       // reply is received before WriteAsync returned.
  TRACE_TO(trace, "SendRpcToTserver");
  ADOPT_TRACE(trace.get());

  tablet_invoker_.proxy()->WriteAsync(
      req_, &resp_, PrepareController(), std::bind(&WriteRpc::Finished, this, Status::OK()));
  TRACE_TO(trace, "RpcDispatched Asynchronously");
}

Status WriteRpc::SwapResponses() {
  int redis_idx = 0;
  int ql_idx = 0;
  int pgsql_idx = 0;

  int64_t pgsql_upcall_sidecar_offset = -1;

  // Retrieve Redis and QL responses and make sure we received all the responses back.
  for (auto& op : ops_) {
    YBOperation* yb_op = op.yb_op.get();
    switch (yb_op->type()) {
      case YBOperation::Type::REDIS_WRITE: {
        if (redis_idx >= resp_.redis_response_batch().size()) {
          ++redis_idx;
          continue;
        }
        // Restore Redis write request PB and extract response.
        auto* redis_op = down_cast<YBRedisWriteOp*>(yb_op);
        redis_op->mutable_response()->Swap(resp_.mutable_redis_response_batch(redis_idx));
        redis_idx++;
        break;
      }
      case YBOperation::Type::QL_WRITE: {
        if (ql_idx >= resp_.ql_response_batch().size()) {
          ++ql_idx;
          continue;
        }
        // Restore QL write request PB and extract response.
        auto* ql_op = down_cast<YBqlWriteOp*>(yb_op);
        ql_op->mutable_response()->Swap(resp_.mutable_ql_response_batch(ql_idx));
        const auto& ql_response = ql_op->response();
        if (ql_response.has_rows_data_sidecar()) {
          // TODO avoid copying sidecar here.
          ql_op->set_rows_data(VERIFY_RESULT(
              retrier().controller().ExtractSidecar(ql_response.rows_data_sidecar())));
        }
        ql_idx++;
        break;
      }
      case YBOperation::Type::PGSQL_WRITE: {
        if (pgsql_idx >= resp_.pgsql_response_batch().size()) {
          ++pgsql_idx;
          continue;
        }
        // Restore QL write request PB and extract response.
        auto* pgsql_op = down_cast<YBPgsqlWriteOp*>(yb_op);
        pgsql_op->mutable_response()->Swap(resp_.mutable_pgsql_response_batch(pgsql_idx));
        const auto& pgsql_response = pgsql_op->response();
        if (pgsql_response.has_rows_data_sidecar()) {
          if (pgsql_upcall_sidecar_offset == -1) {
            // Transfer all sidecars from downcall to upcall. Remembering index of the first
            // sidecar in upcall. So we could convert downcall sidecar index to upcall index
            // using simple addition.
            pgsql_upcall_sidecar_offset = mutable_retrier()->mutable_controller()->TransferSidecars(
                &pgsql_op->sidecars());
          }
          pgsql_op->SetSidecarIndex(
              pgsql_upcall_sidecar_offset + pgsql_response.rows_data_sidecar());
        }
        pgsql_idx++;
        break;
      }
      case YBOperation::Type::PGSQL_READ: FALLTHROUGH_INTENDED;
      case YBOperation::Type::REDIS_READ: FALLTHROUGH_INTENDED;
      case YBOperation::Type::QL_READ:
        LOG(FATAL) << "Not a write operation " << op.yb_op->type();
        break;
    }
  }

  return CheckResponseCount(
      kWrite, redis_idx, resp_.redis_response_batch().size(), ql_idx,
      resp_.ql_response_batch().size(), pgsql_idx, resp_.pgsql_response_batch().size());
}

void WriteRpc::NotifyBatcher(const Status& status) {
  batcher_->ProcessWriteResponse(*this, status);
}

ReadRpc::ReadRpc(const AsyncRpcData& data, YBConsistencyLevel yb_consistency_level)
    : AsyncRpcBase(data, yb_consistency_level) {
  TRACE_TO(trace_, "ReadRpc initiated");
  VTRACE_TO(1, trace_, "Tablet $0 table $1", data.tablet->tablet_id(), table()->name().ToString());
  req_.set_consistency_level(yb_consistency_level);
  req_.set_proxy_uuid(data.batcher->proxy_uuid());

  switch (table()->table_type()) {
    case YBTableType::REDIS_TABLE_TYPE:
      FillOps<YBRedisReadOp>(
          ops_, YBOperation::Type::REDIS_READ, &req_, req_.mutable_redis_batch());
      break;
    case YBTableType::YQL_TABLE_TYPE:
      FillOps<YBqlReadOp>(
          ops_, YBOperation::Type::QL_READ, &req_, req_.mutable_ql_batch());
      break;
    case YBTableType::PGSQL_TABLE_TYPE:
      FillOps<YBPgsqlReadOp>(
          ops_, YBOperation::Type::PGSQL_READ, &req_, req_.mutable_pgsql_batch());
      break;
    case YBTableType::UNKNOWN_TABLE_TYPE:
    case YBTableType::TRANSACTION_STATUS_TABLE_TYPE:
      LOG(DFATAL) << "Unsupported table type: " << table()->ToString();
      break;
  }

  VLOG(3) << "Created batch for " << data.tablet->tablet_id() << ":\n"
          << req_.ShortDebugString();
}

ReadRpc::~ReadRpc() {
  // Get locality metrics if enabled, but skip for system tables as those go to the master.
  if (async_rpc_metrics_ && !table()->name().is_system()) {
    scoped_refptr<EventStats> read_rpc_time = IsLocalCall() ?
                                                   async_rpc_metrics_->local_read_rpc_time :
                                                   async_rpc_metrics_->remote_read_rpc_time;

    read_rpc_time->Increment(ToMicroseconds(CoarseMonoClock::Now() - start_));
  }

  ReleaseOps(req_.mutable_redis_batch());
  ReleaseOps(req_.mutable_ql_batch());
  ReleaseOps(req_.mutable_pgsql_batch());
}

void ReadRpc::CallRemoteMethod() {
  auto trace = trace_; // It is possible that we receive reply before returning from ReadAsync.
                       // Detailed explanation in WriteRpc::SendRpcToTserver.
  TRACE_TO(trace, "SendRpcToTserver");
  ADOPT_TRACE(trace.get());

  tablet_invoker_.proxy()->ReadAsync(
    req_, &resp_, PrepareController(), std::bind(&ReadRpc::Finished, this, Status::OK()));
  TRACE_TO(trace, "RpcDispatched Asynchronously");
}

Status ReadRpc::SwapResponses() {
  int redis_idx = 0;
  int ql_idx = 0;
  int pgsql_idx = 0;
  bool used_read_time_set = false;

  int64_t pgsql_upcall_sidecar_offset = -1;

  // Retrieve Redis and QL responses and make sure we received all the responses back.
  for (auto& op : ops_) {
    YBOperation* yb_op = op.yb_op.get();
    switch (yb_op->type()) {
      case YBOperation::Type::REDIS_READ: {
        RETURN_NOT_OK(CheckResponseCount(kRead, kRedis, redis_idx, resp_.redis_batch().size()));
        // Restore Redis read request PB and extract response.
        auto* redis_op = down_cast<YBRedisReadOp*>(yb_op);
        redis_op->mutable_response()->Swap(resp_.mutable_redis_batch(redis_idx));
        redis_idx++;
        break;
      }
      case YBOperation::Type::QL_READ: {
        RETURN_NOT_OK(CheckResponseCount(kRead, kYCQL, ql_idx, resp_.ql_batch().size()));
        // Restore QL read request PB and extract response.
        auto* ql_op = down_cast<YBqlReadOp*>(yb_op);
        ql_op->mutable_response()->Swap(resp_.mutable_ql_batch(ql_idx));
        const auto& ql_response = ql_op->response();
        if (ql_response.has_rows_data_sidecar()) {
          // TODO avoid copying sidecar here.
          ql_op->set_rows_data(VERIFY_RESULT(
              retrier().controller().ExtractSidecar(ql_response.rows_data_sidecar())));
        }
        ql_idx++;
        break;
      }
      case YBOperation::Type::PGSQL_READ: {
        RETURN_NOT_OK(CheckResponseCount(kRead, kYSQL, pgsql_idx, resp_.pgsql_batch().size()));
        // Restore PGSQL read request PB and extract response.
        auto* pgsql_op = down_cast<YBPgsqlReadOp*>(yb_op);
        if (!used_read_time_set && resp_.has_used_read_time()) {
          // Single operation in a group required used read time.
          used_read_time_set = true;
          pgsql_op->SetUsedReadTime(
              ReadHybridTime::FromPB(resp_.used_read_time()), tablet().tablet_id());
        }
        pgsql_op->mutable_response()->Swap(resp_.mutable_pgsql_batch(pgsql_idx));
        const auto& pgsql_response = pgsql_op->response();
        if (pgsql_response.has_rows_data_sidecar()) {
          if (pgsql_upcall_sidecar_offset == -1) {
            pgsql_upcall_sidecar_offset = mutable_retrier()->mutable_controller()->TransferSidecars(
                &pgsql_op->sidecars());
          }
          pgsql_op->SetSidecarIndex(
              pgsql_upcall_sidecar_offset + pgsql_response.rows_data_sidecar());
        }
        pgsql_idx++;
        break;
      }
      case YBOperation::Type::PGSQL_WRITE: FALLTHROUGH_INTENDED;
      case YBOperation::Type::REDIS_WRITE: FALLTHROUGH_INTENDED;
      case YBOperation::Type::QL_WRITE:
        LOG(FATAL) << "Not a read operation " << op.yb_op->type();
        break;
    }
  }

  return CheckResponseCount(
      kRead, redis_idx, resp_.redis_batch().size(), ql_idx,
      resp_.ql_batch().size(), pgsql_idx, resp_.pgsql_batch().size());
}

void ReadRpc::NotifyBatcher(const Status& status) {
  batcher_->ProcessReadResponse(*this, status);
}

}  // namespace yb::client::internal
