// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/rpc/outbound_call.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include <boost/functional/hash.hpp>
#include <boost/range/adaptor/transformed.hpp>

#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/walltime.h"

#include "yb/rpc/connection.h"
#include "yb/rpc/constants.h"
#include "yb/rpc/proxy_base.h"
#include "yb/rpc/rpc_context.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/rpc_introspection.pb.h"
#include "yb/rpc/rpc_metrics.h"
#include "yb/rpc/serialization.h"
#include "yb/rpc/sidecars.h"

#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/memory/memory.h"
#include "yb/util/metrics.h"
#include "yb/util/pb_util.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/thread_restrictions.h"
#include "yb/util/trace.h"
#include "yb/util/tsan_util.h"

using std::string;

METRIC_DEFINE_event_stats(
    server, handler_latency_outbound_call_queue_time, "Time taken to queue the request ",
    yb::MetricUnit::kMicroseconds, "Microseconds spent to queue the request to the reactor");
METRIC_DEFINE_event_stats(
    server, handler_latency_outbound_call_send_time, "Time taken to send the request ",
    yb::MetricUnit::kMicroseconds, "Microseconds spent to queue and write the request to the wire");
METRIC_DEFINE_event_stats(
    server, handler_latency_outbound_call_time_to_response, "Time taken to get the response ",
    yb::MetricUnit::kMicroseconds,
    "Microseconds spent to send the request and get a response on the wire");

// 100M cycles should be about 50ms on a 2Ghz box. This should be high
// enough that involuntary context switches don't trigger it, but low enough
// that any serious blocking behavior on the reactor would.
DEFINE_RUNTIME_int64(rpc_callback_max_cycles, 100 * 1000 * 1000 * yb::kTimeMultiplier,
    "The maximum number of cycles for which an RPC callback "
    "should be allowed to run without emitting a warning."
    " (Advanced debugging option)");
TAG_FLAG(rpc_callback_max_cycles, advanced);
DECLARE_bool(rpc_dump_all_traces);

namespace yb {
namespace rpc {

using google::protobuf::io::CodedOutputStream;

OutboundCallMetrics::OutboundCallMetrics(const scoped_refptr<MetricEntity>& entity)
    : queue_time(METRIC_handler_latency_outbound_call_queue_time.Instantiate(entity)),
      send_time(METRIC_handler_latency_outbound_call_send_time.Instantiate(entity)),
      time_to_response(METRIC_handler_latency_outbound_call_time_to_response.Instantiate(entity)) {
}

namespace {

std::atomic<int32_t> call_id_ = {0};

int32_t NextCallId() {
  for (;;) {
    auto result = call_id_.fetch_add(1, std::memory_order_acquire);
    ++result;
    if (result > 0) {
      return result;
    }
    // When call id overflows, we reset it to zero.
    call_id_.compare_exchange_weak(result, 0);
  }
}

const std::string kEmptyString;

} // namespace

void InvokeCallbackTask::Run() {
  CHECK_NOTNULL(call_.get());
  call_->InvokeCallbackSync();
}

void InvokeCallbackTask::Done(const Status& status) {
  CHECK_NOTNULL(call_.get());
  if (!status.ok()) {
    LOG(WARNING) << Format(
        "Failed to schedule invoking callback on response for request $0 to $1: $2",
        call_->remote_method(), call_->hostname(), status);
    call_->SetThreadPoolFailure(status);
    // We are in the shutdown path, with the threadpool closing, so allow IO and wait.
    ThreadRestrictions::SetWaitAllowed(true);
    ThreadRestrictions::SetIOAllowed(true);
    call_->InvokeCallbackSync();
  }
  // Clear the call, since it holds OutboundCall object.
  call_ = nullptr;
}

///
/// OutboundCall
///

OutboundCall::OutboundCall(const RemoteMethod& remote_method,
                           const std::shared_ptr<OutboundCallMetrics>& outbound_call_metrics,
                           std::shared_ptr<const OutboundMethodMetrics> method_metrics,
                           AnyMessagePtr response_storage,
                           RpcController* controller,
                           std::shared_ptr<RpcMetrics> rpc_metrics,
                           ResponseCallback callback,
                           ThreadPool* callback_thread_pool)
    : hostname_(&kEmptyString),
      start_(CoarseMonoClock::Now()),
      controller_(DCHECK_NOTNULL(controller)),
      response_(DCHECK_NOTNULL(response_storage)),
      trace_(Trace::MaybeGetNewTraceForParent(Trace::CurrentTrace())),
      call_id_(NextCallId()),
      remote_method_(remote_method),
      callback_(std::move(callback)),
      callback_thread_pool_(callback_thread_pool),
      outbound_call_metrics_(outbound_call_metrics),
      rpc_metrics_(std::move(rpc_metrics)),
      method_metrics_(std::move(method_metrics)) {
  TRACE_TO_WITH_TIME(trace_, start_, "$0.", remote_method_.ToString());

  DVLOG(4) << "OutboundCall " << this << " constructed with state_: " << StateName(state_)
           << " and RPC timeout: "
           << (controller_->timeout().Initialized() ? controller_->timeout().ToString() : "none");

  IncrementCounter(rpc_metrics_->outbound_calls_created);
  IncrementGauge(rpc_metrics_->outbound_calls_alive);
}

OutboundCall::~OutboundCall() {
  DCHECK(IsFinished());
  DVLOG(4) << "OutboundCall " << this << " destroyed with state_: " << StateName(state_);

  if (PREDICT_FALSE(FLAGS_rpc_dump_all_traces)) {
    LOG(INFO) << ToString() << " took "
              << MonoDelta(CoarseMonoClock::Now() - start_).ToMicroseconds() << "us."
              << (trace_ ? " Trace:" : "");
    if (trace_) {
      trace_->Dump(&LOG(INFO), true);
    }
  }

  DecrementGauge(rpc_metrics_->outbound_calls_alive);
}

void OutboundCall::NotifyTransferred(const Status& status, Connection* conn) {
  if (IsFinished()) {
    auto current_state = state();
    LOG_IF_WITH_PREFIX(DFATAL, !IsTimedOut())
        << "Transferred call is in wrong state: "
        << current_state
        << "(" << StateName(current_state) << ")"
        << ", status: " << this->status();
  } else if (status.ok()) {
    SetSent();
  } else {
    VLOG_WITH_PREFIX(1) << "Connection torn down: " << status;
    SetFailed(status);
  }
}

void OutboundCall::Serialize(ByteBlocks* output) {
  output->emplace_back(std::move(buffer_));
  if (sidecars_) {
    sidecars_->Flush(output);
  }
  buffer_consumption_ = ScopedTrackedConsumption();
}

Status OutboundCall::SetRequestParam(
    AnyMessageConstPtr req, std::unique_ptr<Sidecars> sidecars, const MemTrackerPtr& mem_tracker) {
  auto req_size = req.SerializedSize();
  sidecars_ = std::move(sidecars);
  auto sidecars_size = sidecars_ ? sidecars_->size() : 0;
  size_t message_size = SerializedMessageSize(req_size, sidecars_size);

  using Output = google::protobuf::io::CodedOutputStream;
  auto timeout_ms = VERIFY_RESULT(TimeoutMs());
  size_t call_id_size = Output::VarintSize32(call_id_);
  size_t timeout_ms_size = Output::VarintSize32(timeout_ms);
  auto serialized_remote_method = remote_method_.serialized();

  // We use manual encoding for header in protobuf format. So should add 1 byte for tag before
  // each field.
  // serialized_remote_method already contains tag byte, so don't add extra byte for it.
  size_t header_pb_len = 1 + call_id_size + serialized_remote_method.size() + 1 + timeout_ms_size;
  const google::protobuf::RepeatedField<uint32_t>* sidecar_offsets = nullptr;
  size_t encoded_sidecars_len = 0;
  if (sidecars_size) {
    sidecar_offsets = &sidecars_->offsets();
    encoded_sidecars_len = sidecar_offsets->size() * sizeof(uint32_t);
    header_pb_len += 1 + Output::VarintSize64(encoded_sidecars_len) + encoded_sidecars_len;
  }
  size_t header_size =
      kMsgLengthPrefixLength                            // Int prefix for the total length.
      + CodedOutputStream::VarintSize32(
            narrow_cast<uint32_t>(header_pb_len))       // Varint delimiter for header PB.
      + header_pb_len;                                  // Length for the header PB itself.
  size_t buffer_size = header_size + message_size;

  buffer_ = RefCntBuffer(buffer_size);
  uint8_t* dst = buffer_.udata();

  // 1. The length for the whole request, not including the 4-byte
  // length prefix.
  NetworkByteOrder::Store32(
      dst, narrow_cast<uint32_t>(buffer_size + sidecars_size - kMsgLengthPrefixLength));
  dst += sizeof(uint32_t);

  // 2. The varint-prefixed RequestHeader PB
  dst = CodedOutputStream::WriteVarint32ToArray(narrow_cast<uint32_t>(header_pb_len), dst);
  dst = Output::WriteTagToArray(RequestHeader::kCallIdFieldNumber << 3, dst);
  dst = Output::WriteVarint32ToArray(call_id_, dst);
  memcpy(dst, serialized_remote_method.data(), serialized_remote_method.size());
  dst += serialized_remote_method.size();
  dst = CodedOutputStream::WriteTagToArray(RequestHeader::kTimeoutMillisFieldNumber << 3, dst);
  dst = Output::WriteVarint32ToArray(timeout_ms, dst);
  if (sidecars_size) {
    using google::protobuf::internal::WireFormatLite;
    constexpr auto kTag = (RequestHeader::kSidecarOffsetsFieldNumber << 3) |
                          WireFormatLite::WIRETYPE_LENGTH_DELIMITED;
    dst = PackedWrite<LightweightSerialization<WireFormatLite::TYPE_FIXED32, uint32_t>, kTag>(
        *sidecar_offsets | boost::adaptors::transformed(
            [req_size](auto offset) { return narrow_cast<uint32_t>(offset + req_size); }),
        encoded_sidecars_len, dst);
  }

  DCHECK_EQ(dst - buffer_.udata(), header_size);

  if (mem_tracker) {
    buffer_consumption_ = ScopedTrackedConsumption(mem_tracker, buffer_.size());
  }
  RETURN_NOT_OK(SerializeMessage(req, req_size, buffer_, sidecars_size, header_size));
  if (method_metrics_) {
    IncrementCounterBy(method_metrics_->request_bytes, buffer_.size());
  }
  return Status::OK();
}

Status OutboundCall::status() const {
  std::lock_guard l(mtx_);
  return status_;
}

const ErrorStatusPB* OutboundCall::error_pb() const {
  std::lock_guard l(mtx_);
  return error_pb_.get();
}

string OutboundCall::StateName(State state) {
  return RpcCallState_Name(state);
}

OutboundCall::State OutboundCall::state() const {
  return state_.load(std::memory_order_acquire);
}

bool FinishedState(RpcCallState state) {
  switch (state) {
    case READY:
    case ON_OUTBOUND_QUEUE:
    case SENT:
      return false;
    case TIMED_OUT:
    case FINISHED_ERROR:
    case FINISHED_SUCCESS:
      return true;
  }
  LOG(FATAL) << "Unknown call state: " << state;
  return false;
}

bool ValidStateTransition(RpcCallState old_state, RpcCallState new_state) {
  switch (new_state) {
    case ON_OUTBOUND_QUEUE:
      return old_state == READY;
    case SENT:
      return old_state == ON_OUTBOUND_QUEUE;
    case TIMED_OUT:
      return old_state == SENT || old_state == ON_OUTBOUND_QUEUE;
    case FINISHED_SUCCESS:
      return old_state == SENT;
    case FINISHED_ERROR:
      return old_state == SENT || old_state == ON_OUTBOUND_QUEUE || old_state == READY;
    default:
      // No sanity checks for others.
      return true;
  }
}

bool OutboundCall::SetState(State new_state) {
  auto old_state = state();
  // Sanity check state transitions.
  DVLOG(3) << "OutboundCall " << this << " (" << ToString() << ") switching from "
           << StateName(old_state) << " to " << StateName(new_state);
  for (;;) {
    if (FinishedState(old_state)) {
      VLOG(1) << "Call already finished: " << StateName(old_state) << ", new state: "
              << StateName(new_state);
      return false;
    }
    if (!ValidStateTransition(old_state, new_state)) {
      LOG(DFATAL)
          << "Invalid call state transition: " << StateName(old_state) << " => "
          << StateName(new_state);
      return false;
    }
    if (state_.compare_exchange_weak(old_state, new_state, std::memory_order_acq_rel)) {
      return true;
    }
  }
}

void OutboundCall::InvokeCallback() {
  if (callback_thread_pool_) {
    callback_task_.SetOutboundCall(shared_from(this));
    callback_thread_pool_->Enqueue(&callback_task_);
    TRACE_TO(trace_, "Callback will be called asynchronously.");
  } else {
    InvokeCallbackSync();
    TRACE_TO(trace_, "Callback called synchronously.");
  }
}

void OutboundCall::InvokeCallbackSync() {
  if (!callback_) {
    LOG(DFATAL) << "Callback has been already invoked.";
    return;
  }

  int64_t start_cycles = CycleClock::Now();
  callback_();
  // Clear the callback, since it may be holding onto reference counts
  // via bound parameters. We do this inside the timer because it's possible
  // the user has naughty destructors that block, and we want to account for that
  // time here if they happen to run on this thread.
  callback_ = nullptr;
  int64_t end_cycles = CycleClock::Now();
  int64_t wait_cycles = end_cycles - start_cycles;
  if (PREDICT_FALSE(wait_cycles > FLAGS_rpc_callback_max_cycles)) {
    auto time_spent = MonoDelta::FromSeconds(
        static_cast<double>(wait_cycles) / base::CyclesPerSecond());

    LOG(WARNING) << "RPC callback for " << ToString() << " took " << time_spent;
  }

  // Could be destroyed during callback. So reset it.
  controller_ = nullptr;
  response_ = nullptr;
}

void OutboundCall::SetResponse(CallResponse&& resp) {
  DCHECK(!IsFinished());

  auto now = CoarseMonoClock::Now();
  TRACE_TO_WITH_TIME(trace_, now, "Response received.");
  // Avoid expensive conn_id.ToString() in production.
  VTRACE_TO(1, trace_, "from $0", conn_id_.ToString());
  // Track time taken to be responded.

  if (outbound_call_metrics_) {
    outbound_call_metrics_->time_to_response->Increment(MonoDelta(now - start_).ToMicroseconds());
  }
  call_response_ = std::move(resp);
  Slice r(call_response_.serialized_response());

  if (method_metrics_) {
    IncrementCounterBy(method_metrics_->response_bytes, r.size());
  }

  if (call_response_.is_success()) {
    // TODO: here we're deserializing the call response within the reactor thread,
    // which isn't great, since it would block processing of other RPCs in parallel.
    // Should look into a way to avoid this.
    auto status = response_.ParseFromSlice(r);
    if (!status.ok()) {
      SetFailed(status);
      return;
    }
    if (SetState(RpcCallState::FINISHED_SUCCESS)) {
      InvokeCallback();
    } else {
      LOG(DFATAL) << "Success of already finished call: "
                  << StateName(state());
    }
  } else {
    // Error
    auto err = std::make_unique<ErrorStatusPB>();
    if (!pb_util::ParseFromArray(err.get(), r.data(), r.size()).IsOk()) {
      SetFailed(STATUS(IOError, "Was an RPC error but could not parse error response",
                                err->InitializationErrorString()));
      return;
    }
    auto status = STATUS(RemoteError, err->message());
    SetFailed(status, std::move(err));
  }
}

void OutboundCall::SetQueued() {
  auto end_time = CoarseMonoClock::Now();
  // Track time taken to be queued.
  if (outbound_call_metrics_) {
    outbound_call_metrics_->queue_time->Increment(MonoDelta(end_time - start_).ToMicroseconds());
  }
  SetState(RpcCallState::ON_OUTBOUND_QUEUE);
  TRACE_TO_WITH_TIME(trace_, end_time, "Queued.");
}

void OutboundCall::SetSent() {
  auto end_time = CoarseMonoClock::Now();
  // Track time taken to be sent
  if (outbound_call_metrics_) {
    outbound_call_metrics_->send_time->Increment(MonoDelta(end_time - start_).ToMicroseconds());
  }
  SetState(RpcCallState::SENT);
  TRACE_TO_WITH_TIME(trace_, end_time, "Call Sent.");
}

void OutboundCall::SetFinished() {
  DCHECK(!IsFinished());

  // Track time taken to be responded.
  if (outbound_call_metrics_) {
    outbound_call_metrics_->time_to_response->Increment(
        MonoDelta(CoarseMonoClock::Now() - start_).ToMicroseconds());
  }
  if (SetState(RpcCallState::FINISHED_SUCCESS)) {
    InvokeCallback();
  }
}

void OutboundCall::SetFailed(const Status &status, std::unique_ptr<ErrorStatusPB> err_pb) {
  DCHECK(!IsFinished());

  TRACE_TO(trace_, "Call Failed.");
  bool invoke_callback;
  {
    std::lock_guard l(mtx_);
    status_ = status;
    if (status_.IsRemoteError()) {
      CHECK(err_pb);
      error_pb_ = std::move(err_pb);
      if (error_pb_->has_code()) {
        status_ = status_.CloneAndAddErrorCode(RpcError(error_pb_->code()));
      }
    } else {
      CHECK(!err_pb);
    }
    invoke_callback = SetState(RpcCallState::FINISHED_ERROR);
  }
  if (invoke_callback) {
    InvokeCallback();
  }
}

void OutboundCall::SetTimedOut() {
  DCHECK(!IsFinished());

  TRACE_TO(trace_, "Call TimedOut.");
  bool invoke_callback;
  {
    auto status = STATUS_FORMAT(
        TimedOut,
        "$0 RPC (request call id $3) to $1 timed out after $2",
        remote_method_.method_name(),
        conn_id_.remote(),
        controller_->timeout(),
        call_id_);
    std::lock_guard l(mtx_);
    status_ = std::move(status);
    invoke_callback = SetState(RpcCallState::TIMED_OUT);
  }
  if (invoke_callback) {
    InvokeCallback();
  }
}

bool OutboundCall::IsTimedOut() const {
  return state() == RpcCallState::TIMED_OUT;
}

bool OutboundCall::IsFinished() const {
  return FinishedState(state());
}

// The following two functions are only invoked when the call has already finished.
Result<RefCntSlice> OutboundCall::ExtractSidecar(size_t idx) const NO_THREAD_SAFETY_ANALYSIS {
  return call_response_.ExtractSidecar(idx);
}

size_t OutboundCall::TransferSidecars(Sidecars* dest) NO_THREAD_SAFETY_ANALYSIS  {
  return call_response_.TransferSidecars(dest);
}

string OutboundCall::ToString() const {
  return Format("RPC call $0 -> $1 , state=$2.",
                remote_method_, conn_id_, StateName(state()));
}

bool OutboundCall::DumpPB(const DumpRunningRpcsRequestPB& req,
                          RpcCallInProgressPB* resp) {
  std::lock_guard l(mtx_);
  auto state_value = state();
  if (!req.dump_timed_out() && state_value == RpcCallState::TIMED_OUT) {
    return false;
  }
  if (!InitHeader(resp->mutable_header()).ok() && !req.dump_timed_out()) {
    // Note that if we proceed here due to req.dump_timed_out() being true, then the
    // header.timeout_millis() will be inaccurate/not-set. This is ok because DumpPB
    // is only used for dumping the PB and not to send the RPC over the wire.
    return false;
  }
  resp->set_elapsed_millis(MonoDelta(CoarseMonoClock::Now() - start_).ToMilliseconds());
  resp->set_state(state_value);
  if (req.include_traces() && trace_) {
    resp->set_trace_buffer(trace_->DumpToString(true));
  }
  return true;
}

std::string OutboundCall::LogPrefix() const {
  return Format("{ OutboundCall@$0 } ", this);
}

Result<uint32_t> OutboundCall::TimeoutMs() const {
  MonoDelta timeout = controller_->timeout();
  if (timeout.Initialized()) {
    auto timeout_millis = timeout.ToMilliseconds();
    if (timeout_millis <= 0) {
      return STATUS(TimedOut, "Call timed out before sending");
    }
    return narrow_cast<uint32_t>(timeout_millis);
  } else {
    return 0;
  }
}

Status OutboundCall::InitHeader(RequestHeader* header) {
  header->set_call_id(call_id_);
  remote_method_.ToPB(header->mutable_remote_method());

  if (!IsFinished()) {
    header->set_timeout_millis(VERIFY_RESULT(TimeoutMs()));
  }
  return Status::OK();
}

///
/// ConnectionId
///

string ConnectionId::ToString() const {
  return Format("{ remote: $0 idx: $1 protocol: $2 }", remote_, idx_, protocol_);
}

size_t ConnectionId::HashCode() const {
  size_t seed = 0;
  boost::hash_combine(seed, hash_value(remote_));
  boost::hash_combine(seed, idx_);
  boost::hash_combine(seed, protocol_);
  return seed;
}

size_t ConnectionIdHash::operator() (const ConnectionId& conn_id) const {
  return conn_id.HashCode();
}

///
/// CallResponse
///

CallResponse::CallResponse()
    : parsed_(false) {
}

Result<RefCntSlice> CallResponse::ExtractSidecar(size_t idx) const {
  SCHECK(parsed_, IllegalState, "Calling $0 on non parsed response", __func__);
  return sidecars_.Extract(response_data_.buffer(), idx);
}

Result<SidecarHolder> CallResponse::GetSidecarHolder(size_t idx) const {
  return sidecars_.GetHolder(response_data_.buffer(), idx);
}

size_t CallResponse::TransferSidecars(Sidecars* dest) {
  return sidecars_.Transfer(response_data_.buffer(), dest);
}

Status CallResponse::ParseFrom(CallData* call_data) {
  CHECK(!parsed_);

  RETURN_NOT_OK(ParseYBMessage(*call_data, &header_, &serialized_response_, &sidecars_));
  response_data_ = std::move(*call_data);

  parsed_ = true;
  return Status::OK();
}

const std::string kRpcErrorCategoryName = "rpc error";

StatusCategoryRegisterer rpc_error_category_registerer(
    StatusCategoryDescription::Make<RpcErrorTag>(&kRpcErrorCategoryName));

}  // namespace rpc
}  // namespace yb
