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

#include "yb/rpc/inbound_call.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/rpc/connection.h"
#include "yb/rpc/connection_context.h"
#include "yb/rpc/rpc_introspection.pb.h"
#include "yb/rpc/rpc_metrics.h"
#include "yb/rpc/service_if.h"

#include "yb/util/debug/trace_event.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/trace.h"

DEFINE_RUNTIME_bool(rpc_dump_all_traces, false, "If true, dump all RPC traces at INFO level");
TAG_FLAG(rpc_dump_all_traces, advanced);

DEFINE_RUNTIME_bool(collect_end_to_end_traces, false,
    "If true, collected traces includes information for sub-components "
    "potentially running on a different server. ");
TAG_FLAG(collect_end_to_end_traces, advanced);

DEFINE_RUNTIME_int32(print_trace_every, 0,
    "Controls the rate at which traces are printed. Setting this to 0 "
    "disables printing the collected traces.");
TAG_FLAG(print_trace_every, advanced);

DEFINE_RUNTIME_int32(rpc_slow_query_threshold_ms, 10000,
    "Traces for calls that take longer than this threshold (in ms) are logged");
TAG_FLAG(rpc_slow_query_threshold_ms, advanced);

namespace yb {
namespace rpc {

InboundCall::InboundCall(ConnectionPtr conn, RpcMetrics* rpc_metrics,
                         CallProcessedListener* call_processed_listener)
    : trace_holder_(Trace::MaybeGetNewTrace()),
      trace_(trace_holder_.get()),
      // TBD: Initialise this in the Parse function instead.
      wait_state_(std::make_shared<util::WaitStateInfo>(
          util::AUHMetadata{.component = yb::util::WaitStateComponent::TServer})),
      conn_(std::move(conn)),
      rpc_metrics_(rpc_metrics ? rpc_metrics : &conn_->rpc_metrics()),
      call_processed_listener_(call_processed_listener) {
  TRACE_TO(trace(), "Created InboundCall");
  IncrementCounter(rpc_metrics_->inbound_calls_created);
  IncrementGauge(rpc_metrics_->inbound_calls_alive);
  // wait_state_->set_state(util::WaitStateCode::Created);
  wait_state_->set_state(util::WaitStateCode::PassiveOnCPU);
}

InboundCall::~InboundCall() {
  Trace *my_trace = trace();
  TRACE_TO(my_trace, "Destroying InboundCall");
  if (my_trace) {
    YB_LOG_IF_EVERY_N(INFO, FLAGS_print_trace_every > 0, FLAGS_print_trace_every)
        << "Tracing op: \n " << my_trace->DumpToString(true);
  }
  DecrementGauge(rpc_metrics_->inbound_calls_alive);
}

void InboundCall::NotifyTransferred(const Status& status, const ConnectionPtr& ignored) {
  // wait_state_->set_state(util::WaitStateCode::ActiveOnCPU);
  if (status.ok()) {
    TRACE_TO(trace(), "Transfer finished");
  } else {
    YB_LOG_EVERY_N_SECS(WARNING, 10) << LogPrefix() << "Connection torn down before " << ToString()
                                     << " could send its response: " << status.ToString();
  }
  if (call_processed_listener_) {
    call_processed_listener_->CallProcessed(this);
  }
}

void InboundCall::EnsureTraceCreated() {
  scoped_refptr<Trace> trace = nullptr;
  {
    std::lock_guard lock(mutex_);
    if (trace_holder_) {
      return;
    }
    trace = new Trace;
    trace_holder_ = trace;
    trace_.store(trace.get(), std::memory_order_relaxed);
  }

  if (timing_.time_received.Initialized()) {
    TRACE_TO_WITH_TIME(trace, ToCoarse(timing_.time_received), "Created InboundCall");
  }
  if (timing_.time_handled.Initialized()) {
    TRACE_TO_WITH_TIME(trace, ToCoarse(timing_.time_handled), "Handling the call");
  }
  DCHECK(!timing_.time_completed.Initialized());
  TRACE_TO(trace, "Trace Created");
}

const Endpoint& InboundCall::remote_address() const {
  CHECK_NOTNULL(conn_.get());
  return conn_->remote();
}

const Endpoint& InboundCall::local_address() const {
  CHECK_NOTNULL(conn_.get());
  return conn_->local();
}

ConnectionPtr InboundCall::connection() const {
  return conn_;
}

ConnectionContext& InboundCall::connection_context() const {
  return conn_->context();
}

void InboundCall::RecordCallReceived() {
  TRACE_EVENT_ASYNC_BEGIN0("rpc", "InboundCall", this);
  // Protect against multiple calls.
  LOG_IF_WITH_PREFIX(DFATAL, timing_.time_received.Initialized()) << "Already marked as received";
  VLOG_WITH_PREFIX(4) << "Received";
  timing_.time_received = MonoTime::Now();
}

void InboundCall::RecordHandlingStarted(scoped_refptr<EventStats> incoming_queue_time) {
  DCHECK(incoming_queue_time != nullptr);
  // Protect against multiple calls.
  LOG_IF_WITH_PREFIX(DFATAL, timing_.time_handled.Initialized()) << "Already marked as started";
  timing_.time_handled = MonoTime::Now();
  VLOG_WITH_PREFIX(4) << "Handling";
  incoming_queue_time->Increment(
      timing_.time_handled.GetDeltaSince(timing_.time_received).ToMicroseconds());
}

MonoDelta InboundCall::GetTimeInQueue() const {
  return timing_.time_handled.GetDeltaSince(timing_.time_received);
}

ThreadPoolTask* InboundCall::BindTask(InboundCallHandler* handler, int64_t rpc_queue_limit) {
  auto shared_this = shared_from(this);
  boost::optional<int64_t> rpc_queue_position = handler->CallQueued(rpc_queue_limit);
  if (!rpc_queue_position) {
    return nullptr;
  }
  rpc_queue_position_ = *rpc_queue_position;
  tracker_ = handler;
  task_.Bind(handler, shared_this);
  return &task_;
}

void InboundCall::RecordHandlingCompleted() {
  // Protect against multiple calls.
  LOG_IF_WITH_PREFIX(DFATAL, timing_.time_completed.Initialized()) << "Already marked as completed";
  timing_.time_completed = MonoTime::Now();
  VLOG_WITH_PREFIX(4) << "Completed handling";
  if (rpc_method_handler_latency_) {
    rpc_method_handler_latency_->Increment(
        (timing_.time_completed - timing_.time_handled).ToMicroseconds());
  }
}

bool InboundCall::ClientTimedOut() const {
  auto deadline = GetClientDeadline();
  if (deadline == CoarseTimePoint::max()) {
    return false;
  }

  return deadline < CoarseMonoClock::now();
}

void InboundCall::QueueResponse(bool is_success) {
  // wait_state_->set_state(util::WaitStateCode::ActiveOnCPU);
  TRACE_TO(trace(), is_success ? "Queueing success response" : "Queueing failure response");
  LogTrace();
  bool expected = false;
  if (responded_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
    auto queuing_status =
        connection()->context().QueueResponse(connection(), shared_from(this));
    // Do not DFATAL here because it is a normal situation during reactor shutdown. The client
    // should detect and handle the error.
    LOG_IF_WITH_PREFIX(WARNING, !queuing_status.ok())
        << "Could not queue response to an inbound call: " << queuing_status;
  } else {
    LOG_WITH_PREFIX(DFATAL) << "Response already queued";
  }
  wait_state_->set_state(util::WaitStateCode::ResponseQueued);
}

std::string InboundCall::LogPrefix() const {
  return Format("$0: ", this);
}

bool InboundCall::RespondTimedOutIfPending(const char* message) {
  if (!TryStartProcessing()) {
    return false;
  }

  RespondFailure(ErrorStatusPB::ERROR_SERVER_TOO_BUSY, STATUS(TimedOut, message));
  Clear();

  return true;
}

void InboundCall::SetCallProcessedListener(CallProcessedListener* call_processed_listener) {
  DCHECK(call_processed_listener_ == nullptr) << this << " Trying to overwrite non-null call processed listner "
      << " existing : " << call_processed_listener_ << " trying to set " << call_processed_listener;
  call_processed_listener_ = call_processed_listener;
}

void InboundCall::Clear() {
  {
    std::lock_guard lock(mutex_);
    cleared_ = true;
  }
  serialized_request_.clear();
  request_data_.Reset();
}

// Overrides OutboundData::DynamicMemoryUsage() to track response buffer memory.
// TODO: remove the trace() usage from OutboundData/Sending mem-tracker to call tracker.
size_t InboundCall::DynamicMemoryUsage() const {
  return DynamicMemoryUsageOf(trace());
}

void InboundCall::InboundCallTask::Run() {
  handler_->Handle(call_);
}

void InboundCall::InboundCallTask::Done(const Status& status) {
  // We should reset call_ after this function. So it is easiest way to do it.
  auto call = std::move(call_);
  if (!status.ok()) {
    handler_->Failure(call, status);
  }
}

void InboundCall::SetRpcMethodMetrics(std::reference_wrapper<const RpcMethodMetrics> value) {
  const auto& metrics = value.get();
  rpc_method_response_bytes_ = metrics.response_bytes;
  rpc_method_handler_latency_ = metrics.handler_latency;
  if (metrics.request_bytes) {
    auto request_size = request_data_.size();
    if (request_size) {
      metrics.request_bytes->IncrementBy(request_size);
    }
  }
}

void InboundCall::Serialize(ByteBlocks* output) {
  size_t old_size = output->size();
  DoSerialize(output);
  if (rpc_method_response_bytes_) {
    auto response_size = 0;
    for (size_t i = old_size; i != output->size(); ++i) {
      response_size += (*output)[i].size();
    }
    if (response_size) {
      rpc_method_response_bytes_->IncrementBy(response_size);
    }
  }
}

}  // namespace rpc
}  // namespace yb
