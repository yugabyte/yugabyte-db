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
#include "yb/client/client.h"
#include "yb/client/client-internal.h"
#include "yb/client/in_flight_op.h"
#include "yb/client/meta_cache.h"
#include "yb/client/yb_op-internal.h"

#include "yb/util/cast.h"
#include "yb/util/debug-util.h"
#include "yb/util/logging.h"
#include "yb/common/wire_protocol.h"
#include "yb/common/row_operations.h"

// TODO: do we need word Redis in following two metrics? ReadRpc and WriteRpc objects emitting
// these metrics are used not only in Redis service.
METRIC_DEFINE_histogram(
    server, handler_latency_yb_client_write_remote, "yb.client.Write remote call time",
    yb::MetricUnit::kMicroseconds, "Microseconds spent in the remote Write call ", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_client_read_remote, "yb.client.Read remote call time",
    yb::MetricUnit::kMicroseconds, "Microseconds spent in the remote Read call ", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_client_write_local, "yb.client.Write local call time",
    yb::MetricUnit::kMicroseconds, "Microseconds spent in the local Write call ", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_client_read_local, "yb.client.Read local call time",
    yb::MetricUnit::kMicroseconds, "Microseconds spent in the local Read call ", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_client_time_to_send,
    "Time taken for a Write/Read rpc to be sent to the server", yb::MetricUnit::kMicroseconds,
    "Microseconds spent before sending the request to the server", 60000000LU, 2);
DECLARE_bool(rpc_dump_all_traces);
DECLARE_bool(collect_end_to_end_traces);

using namespace std::placeholders;

namespace yb {

using std::shared_ptr;
using rpc::ErrorStatusPB;
using rpc::Messenger;
using rpc::Rpc;
using rpc::RpcController;
using tserver::WriteRequestPB;
using tserver::WriteResponsePB;
using tserver::WriteResponsePB_PerRowErrorPB;
using strings::Substitute;

namespace client {

namespace internal {

bool IsTracingEnabled() {
  return FLAGS_collect_end_to_end_traces;
}

AsyncRpcMetrics::AsyncRpcMetrics(const scoped_refptr<yb::MetricEntity>& entity)
    : remote_write_rpc_time(METRIC_handler_latency_yb_client_write_remote.Instantiate(entity)),
      remote_read_rpc_time(METRIC_handler_latency_yb_client_read_remote.Instantiate(entity)),
      local_write_rpc_time(METRIC_handler_latency_yb_client_write_local.Instantiate(entity)),
      local_read_rpc_time(METRIC_handler_latency_yb_client_read_local.Instantiate(entity)),
      time_to_send(METRIC_handler_latency_yb_client_time_to_send.Instantiate(entity)) {
}

AsyncRpc::AsyncRpc(
    const scoped_refptr<Batcher>& batcher, RemoteTablet* const tablet, InFlightOps ops,
    YBConsistencyLevel yb_consistency_level)
    : Rpc(batcher->deadline(), batcher->messenger()),
      batcher_(batcher),
      trace_(new Trace),
      tablet_invoker_(yb_consistency_level == YBConsistencyLevel::CONSISTENT_PREFIX,
                      batcher->client_,
                      this,
                      this,
                      tablet,
                      mutable_retrier(),
                      trace_.get()),
      ops_(std::move(ops)),
      start_(MonoTime::Now(MonoTime::FINE)),
      async_rpc_metrics_(batcher->async_rpc_metrics()) {
  if (Trace::CurrentTrace()) {
    Trace::CurrentTrace()->AddChildTrace(trace_.get());
  }
}

AsyncRpc::~AsyncRpc() {
  if (PREDICT_FALSE(FLAGS_rpc_dump_all_traces)) {
    LOG(INFO) << ToString() << " took "
              << MonoTime::Now(MonoTime::FINE).GetDeltaSince(start_).ToMicroseconds()
              << "us. Trace:";
    trace_->Dump(&LOG(INFO), true);
  }
}

void AsyncRpc::SendRpc() {
  TRACE_TO(trace_, "SendRpc() called.");

  retained_self_ = shared_from_this();
  tablet_invoker_.Execute(std::string());
}

std::string AsyncRpc::ToString() const {
  return Substitute("$0(tablet: $1, num_ops: $2, num_attempts: $3)",
                    batcher_->read_only_ ? "Read" : "Write",
                    tablet().tablet_id(), ops_.size(), num_attempts());
}

const YBTable* AsyncRpc::table() const {
  // All of the ops for a given tablet obviously correspond to the same table,
  // so we'll just grab the table from the first.
  return ops_[0]->yb_op->table();
}

void AsyncRpc::SendRpcCb(const Status& status) {
  Status new_status = status;
  if (tablet_invoker_.Done(&new_status)) {
    ProcessResponseFromTserver(new_status);
    batcher_->RemoveInFlightOpsAfterFlushing(ops_, new_status);
    batcher_->CheckForFinishedFlush();
    retained_self_.reset();
  }
}

void AsyncRpc::Failed(const Status& status) {
  std::string error_message = status.message().ToBuffer();
  for (auto op : ops_) {
    YBOperation* yb_op = op->yb_op.get();
    switch (yb_op->type()) {
      case YBOperation::Type::REDIS_READ: {
        RedisResponsePB* resp = down_cast<YBRedisReadOp*>(yb_op)->mutable_response();
        resp->Clear();
        resp->set_code(RedisResponsePB_RedisStatusCode_SERVER_ERROR);
        resp->set_error_message(error_message);
        break;
      }
      case YBOperation::Type::REDIS_WRITE: {
        RedisResponsePB* resp = down_cast<YBRedisWriteOp *>(yb_op)->mutable_response();
        resp->Clear();
        resp->set_code(RedisResponsePB_RedisStatusCode_SERVER_ERROR);
        resp->set_error_message(error_message);
        break;
      }
      case YBOperation::Type::QL_READ: FALLTHROUGH_INTENDED;
      case YBOperation::Type::QL_WRITE: {
        QLResponsePB* resp = down_cast<YBqlOp*>(yb_op)->mutable_response();
        resp->Clear();
        resp->set_status(QLResponsePB::YQL_STATUS_RUNTIME_ERROR);
        resp->set_error_message(error_message);
        break;
      }
      case YBOperation::Type::INSERT: FALLTHROUGH_INTENDED;
      case YBOperation::Type::UPDATE: FALLTHROUGH_INTENDED;
      case YBOperation::Type::DELETE:
        break;
      default:
        LOG(FATAL) << "Unsupported operation " << yb_op->type();
        break;
    }
  }
}

bool AsyncRpc::IsLocalCall() const {
  return tablet_invoker_.IsLocalCall();
}

void AsyncRpc::SendRpcToTserver() {
  MonoTime end_time = MonoTime::Now(MonoTime::FINE);
  if (async_rpc_metrics_)
    async_rpc_metrics_->time_to_send->Increment(end_time.GetDeltaSince(start_).ToMicroseconds());
  CallRemoteMethod();
}

WriteRpc::WriteRpc(const scoped_refptr<Batcher>& batcher,
                   RemoteTablet* const tablet,
                   InFlightOps ops)
    : AsyncRpc(batcher, tablet, ops) {
  TRACE_TO(trace_, "WriteRpc initiated to $0", tablet->tablet_id());
  const Schema& schema = GetSchema(table()->schema());

  req_.set_tablet_id(tablet->tablet_id());
  req_.set_include_trace(IsTracingEnabled());
  const auto& transaction = batcher->transaction_metadata();
  if (transaction.has_transaction_id()) {
    *req_.mutable_write_batch()->mutable_transaction() = transaction;
  }

  switch (batcher->external_consistency_mode()) {
    case yb::client::YBSession::CLIENT_PROPAGATED:
      req_.set_external_consistency_mode(yb::CLIENT_PROPAGATED);
      break;
    case yb::client::YBSession::COMMIT_WAIT:
      req_.set_external_consistency_mode(yb::COMMIT_WAIT);
      break;
    default:
      LOG(FATAL) << "Unsupported consistency mode: " << batcher->external_consistency_mode();
  }

  RowOperationsPB* requested = req_.mutable_row_operations();

  // Add the rows
  int ctr = 0;
  RowOperationsPBEncoder enc(requested);
  for (auto& op : ops_) {
#ifndef NDEBUG
    const Partition& partition = op->tablet->partition();
    const PartitionSchema& partition_schema = table()->partition_schema();
    const YBPartialRow& row = op->yb_op->row();

    bool partition_contains_row;
    CHECK(partition_schema.PartitionContainsRow(partition, row, &partition_contains_row).ok());
    CHECK(partition_contains_row)
    << "Row " << partition_schema.RowDebugString(row)
    << "not in partition " << partition_schema.PartitionDebugString(partition, schema);
#endif
    switch (op->yb_op->type()) {
      case YBOperation::Type::REDIS_WRITE: {
        CHECK_EQ(table()->table_type(), YBTableType::REDIS_TABLE_TYPE);
        // Move Redis write request PB into tserver write request PB for performance. Will restore
        // in ProcessResponseFromTserver.
        auto* redis_op = down_cast<YBRedisWriteOp*>(op->yb_op.get());
        req_.add_redis_write_batch()->Swap(redis_op->mutable_request());
        break;
      }
      case YBOperation::Type::QL_WRITE: {
        CHECK_EQ(table()->table_type(), YBTableType::YQL_TABLE_TYPE);
        // Move QL write request PB into tserver write request PB for performance. Will restore
        // in ProcessResponseFromTserver.
        auto* ql_op = down_cast<YBqlWriteOp*>(op->yb_op.get());
        req_.add_ql_write_batch()->Swap(ql_op->mutable_request());
        break;
      }
      case YBOperation::Type::INSERT: FALLTHROUGH_INTENDED;
      case YBOperation::Type::UPDATE: FALLTHROUGH_INTENDED;
      case YBOperation::Type::DELETE: {
        CHECK_NE(table()->table_type(), YBTableType::REDIS_TABLE_TYPE)
            << "unsupported table type " << table()->table_type() << " for insert/update/delete";
        enc.Add(ToInternalWriteType(op->yb_op->type()), op->yb_op->row());
        if (!req_.has_schema()) {
          // Only in the Kudu case, we still need the schema as part of every WriteRequest.
          CHECK_OK(SchemaToPB(schema, req_.mutable_schema(),
              SCHEMA_PB_WITHOUT_STORAGE_ATTRIBUTES | SCHEMA_PB_WITHOUT_IDS));
        }
        break;
      }
      case YBOperation::Type::REDIS_READ: FALLTHROUGH_INTENDED;
      case YBOperation::Type::QL_READ:
        LOG(FATAL) << "Not a write operation " << op->yb_op->type();
        break;
      default:
        LOG(FATAL) << "Unsupported write operation " << op->yb_op->type();
        break;
    }

    // Set the state now, even though we haven't yet sent it -- at this point
    // there is no return, and we're definitely going to send it. If we waited
    // until after we sent it, the RPC callback could fire before we got a chance
    // to change its state to 'sent'.
    op->state = InFlightOpState::kRequestSent;
    VLOG(4) << ++ctr << ". Encoded row " << op->yb_op->ToString();
  }

  if (VLOG_IS_ON(3)) {
    VLOG(3) << "Created batch for " << tablet->tablet_id() << ":\n"
            << req_.ShortDebugString();
  }
}

WriteRpc::~WriteRpc() {
  MonoTime end_time = MonoTime::Now(MonoTime::FINE);
  if (async_rpc_metrics_) {
    scoped_refptr<Histogram> write_rpc_time = IsLocalCall() ?
                                              async_rpc_metrics_->local_write_rpc_time :
                                              async_rpc_metrics_->remote_write_rpc_time;
    write_rpc_time->Increment(end_time.GetDeltaSince(start_).ToMicroseconds());
  }
}

std::string WriteRpc::ToString() const {
  return Substitute("Write(tablet: $0, num_ops: $1, num_attempts: $2)",
                    tablet().tablet_id(), ops_.size(), num_attempts());
}

void WriteRpc::CallRemoteMethod() {
  auto trace = trace_; // It is possible that we receive reply before returning from WriteAsync.
                       // Since send happens before we return from WriteAsync.
                       // So under heavy load it is possible that our request is handled and
                       // reply is received before WriteAsync returned.
  TRACE_TO(trace, "SendRpcToTserver");
  ADOPT_TRACE(trace.get());

  tablet_invoker_.proxy()->WriteAsync(
      req_, &resp_, mutable_retrier()->mutable_controller(),
      std::bind(&WriteRpc::SendRpcCb, this, Status::OK()));
  TRACE_TO(trace, "RpcDispatched Asynchronously");
}

void WriteRpc::ProcessResponseFromTserver(Status status) {
  TRACE_TO(trace_, "ProcessResponseFromTserver($0)", status.ToString(false));
  if (resp_.has_trace_buffer()) {
    TRACE_TO(trace_, "Received from server: $0", resp_.trace_buffer());
  }
  batcher_->ProcessWriteResponse(*this, status);
  if (!status.ok()) return;
  if (resp_.has_error()) {
    LOG(WARNING) << "Write Rpc to tablet server has error:"
                 << resp_.error().DebugString()
                 << ". Requests not processed.";
    // If there is an error at the Rpc itself,
    // there should be no individual responses. All of them need to be
    // marked as failed.
    Failed(StatusFromPB(resp_.error().status()));
    return;
  }
  size_t redis_idx = 0;
  size_t ql_idx = 0;
  // Retrieve Redis and QL responses and make sure we received all the responses back.
  for (auto& op : ops_) {
    YBOperation* yb_op = op->yb_op.get();
    switch (yb_op->type()) {
      case YBOperation::Type::REDIS_WRITE: {
        if (redis_idx >= resp_.redis_response_batch().size()) {
          batcher_->AddOpCountMismatchError();
          return;
        }
        auto* redis_op = down_cast<YBRedisWriteOp*>(yb_op);
        // Restore Redis write request PB and extract response.
        redis_op->mutable_request()->Swap(req_.mutable_redis_write_batch(redis_idx));
        redis_op->mutable_response()->Swap(resp_.mutable_redis_response_batch(redis_idx));
        redis_idx++;
        break;
      }
      case YBOperation::Type::QL_WRITE: {
        if (ql_idx >= resp_.ql_response_batch().size()) {
          batcher_->AddOpCountMismatchError();
          return;
        }
        // Restore QL write request PB and extract response.
        auto* ql_op = down_cast<YBqlWriteOp*>(yb_op);
        ql_op->mutable_request()->Swap(req_.mutable_ql_write_batch(ql_idx));
        ql_op->mutable_response()->Swap(resp_.mutable_ql_response_batch(ql_idx));
        const auto& ql_response = ql_op->response();
        if (ql_response.has_rows_data_sidecar()) {
          Slice rows_data;
          CHECK_OK(retrier().controller().GetSidecar(
              ql_response.rows_data_sidecar(), &rows_data));
          down_cast<YBqlWriteOp*>(yb_op)->mutable_rows_data()->assign(
              util::to_char_ptr(rows_data.data()), rows_data.size());
        }
        ql_idx++;
        break;
      }

      case YBOperation::Type::INSERT: FALLTHROUGH_INTENDED;
      case YBOperation::Type::UPDATE: FALLTHROUGH_INTENDED;
      case YBOperation::Type::DELETE:
        break; // these writes have no separate responses

      case YBOperation::Type::REDIS_READ: FALLTHROUGH_INTENDED;
      case YBOperation::Type::QL_READ:
        LOG(FATAL) << "Not a write operation " << op->yb_op->type();
        break;
    }
  }

  if (redis_idx != resp_.redis_response_batch().size() ||
      ql_idx != resp_.ql_response_batch().size()) {
    LOG(ERROR) << Substitute("Write response count mismatch: "
                             "$0 Redis requests sent, $1 responses received. "
                             "$2 QL requests sent, $3 responses received.",
                             redis_idx, resp_.redis_response_batch().size(),
                             ql_idx, resp_.ql_response_batch().size());
    batcher_->AddOpCountMismatchError();
    Failed(STATUS(IllegalState, "Write response count mismatch"));
  }
}

ReadRpc::ReadRpc(
    const scoped_refptr<Batcher>& batcher, RemoteTablet* const tablet, InFlightOps ops,
    YBConsistencyLevel yb_consistency_level)
    : AsyncRpc(batcher, tablet, ops, yb_consistency_level) {
  TRACE_TO(trace_, "ReadRpc initiated to $0", tablet->tablet_id());
  req_.set_consistency_level(yb_consistency_level);
  req_.set_tablet_id(tablet->tablet_id());
  req_.set_include_trace(IsTracingEnabled());
  int ctr = 0;
  for (auto& op : ops_) {
    switch (op->yb_op->type()) {
      case YBOperation::Type::REDIS_READ: {
        CHECK_EQ(table()->table_type(), YBTableType::REDIS_TABLE_TYPE);
        // Move Redis read request PB into tserver read request PB for performance. Will restore
        // in ProcessResponseFromTserver.
        auto* redis_op = down_cast<YBRedisReadOp*>(op->yb_op.get());
        req_.add_redis_batch()->Swap(redis_op->mutable_request());
        break;
      }
      case YBOperation::Type::QL_READ: {
        CHECK_EQ(table()->table_type(), YBTableType::YQL_TABLE_TYPE);
        // Move QL read request PB into tserver read request PB for performance. Will restore
        // in ProcessResponseFromTserver.
        auto* ql_op = down_cast<YBqlReadOp*>(op->yb_op.get());
        req_.add_ql_batch()->Swap(ql_op->mutable_request());
        break;
      }
      case YBOperation::Type::INSERT: FALLTHROUGH_INTENDED;
      case YBOperation::Type::UPDATE: FALLTHROUGH_INTENDED;
      case YBOperation::Type::DELETE: FALLTHROUGH_INTENDED;
      case YBOperation::Type::REDIS_WRITE: FALLTHROUGH_INTENDED;
      case YBOperation::Type::QL_WRITE:
        LOG(FATAL) << "Not a read operation " << op->yb_op->type();
        break;
      default:
        LOG(FATAL) << "Unsupported read operation " << op->yb_op->type();
        break;
    }
    op->state = InFlightOpState::kRequestSent;
    VLOG(4) << ++ctr << ". Encoded row " << op->yb_op->ToString();
  }

  if (VLOG_IS_ON(3)) {
    VLOG(3) << "Created batch for " << tablet->tablet_id() << ":\n" << req_.ShortDebugString();
  }
}

ReadRpc::~ReadRpc() {
  MonoTime end_time = MonoTime::Now(MonoTime::FINE);
  if (async_rpc_metrics_) {
    scoped_refptr<Histogram> read_rpc_time = IsLocalCall() ?
                                             async_rpc_metrics_->local_read_rpc_time :
                                             async_rpc_metrics_->remote_read_rpc_time;
    read_rpc_time->Increment(end_time.GetDeltaSince(start_).ToMicroseconds());
  }
}

std::string ReadRpc::ToString() const {
  return Substitute("Read(tablet: $0, num_ops: $1, num_attempts: $2)",
                    tablet().tablet_id(), ops_.size(), num_attempts());
}

void ReadRpc::CallRemoteMethod() {
  auto trace = trace_; // It is possible that we receive reply before returning from ReadAsync.
                       // Detailed explanation in WriteRpc::SendRpcToTserver.
  TRACE_TO(trace, "SendRpcToTserver");
  ADOPT_TRACE(trace.get());
  tablet_invoker_.proxy()->ReadAsync(
      req_, &resp_, mutable_retrier()->mutable_controller(),
      std::bind(&ReadRpc::SendRpcCb, this, Status::OK()));
  TRACE_TO(trace, "RpcDispatched Asynchronously");
}

void ReadRpc::ProcessResponseFromTserver(Status status) {
  TRACE_TO(trace_, "ProcessResponseFromTserver($0)", status.ToString(false));
  if (resp_.has_trace_buffer()) {
    TRACE_TO(trace_, "Received from server: $0", resp_.trace_buffer());
  }
  batcher_->ProcessReadResponse(*this, status);
  if (!status.ok()) return;
  if (resp_.has_error()) {
    LOG(WARNING) << "Read Rpc to tablet server has error:"
                 << resp_.error().DebugString()
                 << ". Requests not processed.";
    // If there is an error at the Rpc itself,
    // there should be no individual responses. All of them need to be
    // marked as failed.
    Failed(StatusFromPB(resp_.error().status()));
    return;
  }
  // Retrieve Redis and QL responses and make sure we received all the responses back.
  size_t redis_idx = 0;
  size_t ql_idx = 0;
  for (auto& op : ops_) {
    YBOperation* yb_op = op->yb_op.get();
    switch (yb_op->type()) {
      case YBOperation::Type::REDIS_READ: {
        if (redis_idx >= resp_.redis_batch().size()) {
          batcher_->AddOpCountMismatchError();
          return;
        }
        // Restore Redis read request PB and extract response.
        auto* redis_op = down_cast<YBRedisReadOp*>(yb_op);
        redis_op->mutable_request()->Swap(req_.mutable_redis_batch(redis_idx));
        redis_op->mutable_response()->Swap(resp_.mutable_redis_batch(redis_idx));
        redis_idx++;
        break;
      }
      case YBOperation::Type::QL_READ: {
        if (ql_idx >= resp_.ql_batch().size()) {
          batcher_->AddOpCountMismatchError();
          return;
        }
        // Restore QL read request PB and extract response.
        auto* ql_op = down_cast<YBqlReadOp*>(yb_op);
        ql_op->mutable_request()->Swap(req_.mutable_ql_batch(ql_idx));
        ql_op->mutable_response()->Swap(resp_.mutable_ql_batch(ql_idx));
        const auto& ql_response = ql_op->response();
        if (ql_response.has_rows_data_sidecar()) {
          Slice rows_data;
          CHECK_OK(retrier().controller().GetSidecar(
              ql_response.rows_data_sidecar(), &rows_data));
          down_cast<YBqlReadOp*>(yb_op)->mutable_rows_data()->assign(
              util::to_char_ptr(rows_data.data()), rows_data.size());
        }
        ql_idx++;
        break;
      }
      case YBOperation::Type::INSERT: FALLTHROUGH_INTENDED;
      case YBOperation::Type::UPDATE: FALLTHROUGH_INTENDED;
      case YBOperation::Type::DELETE: FALLTHROUGH_INTENDED;
      case YBOperation::Type::REDIS_WRITE: FALLTHROUGH_INTENDED;
      case YBOperation::Type::QL_WRITE:
        LOG(FATAL) << "Not a read operation " << op->yb_op->type();
        break;
    }
  }

  if (redis_idx != resp_.redis_batch().size() ||
      ql_idx != resp_.ql_batch().size()) {
    LOG(ERROR) << Substitute("Read response count mismatch: "
                             "$0 Redis requests sent, $1 responses received. "
                             "$2 QL requests sent, $3 responses received.",
                             redis_idx, resp_.redis_batch().size(),
                             ql_idx, resp_.ql_batch().size());
    batcher_->AddOpCountMismatchError();
    Failed(STATUS(IllegalState, "Read response count mismatch"));
  }
}

}  // namespace internal
}  // namespace client
}  // namespace yb
