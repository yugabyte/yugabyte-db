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

#include "yb/rpc/connection.h"

#include <thread>
#include <utility>

#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/rpc/connection_context.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/network_error.h"
#include "yb/rpc/reactor.h"
#include "yb/rpc/reactor_task.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/rpc_introspection.pb.h"
#include "yb/rpc/rpc_metrics.h"

#include "yb/util/enums.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/string_util.h"
#include "yb/util/trace.h"
#include "yb/util/tsan_util.h"
#include "yb/util/flags.h"
#include "yb/util/unique_lock.h"

using namespace std::literals;
using namespace std::placeholders;
using std::vector;

DEFINE_UNKNOWN_uint64(rpc_connection_timeout_ms, yb::NonTsanVsTsan(15000, 30000),
    "Timeout for RPC connection operations");

METRIC_DEFINE_histogram(
    server, handler_latency_outbound_transfer, "Time taken to transfer the response ",
    yb::MetricUnit::kMicroseconds, "Microseconds spent to queue and write the response to the wire",
    60000000LU, 2);

namespace yb {
namespace rpc {

Connection::Connection(Reactor* reactor,
                       std::unique_ptr<Stream> stream,
                       Direction direction,
                       RpcMetrics* rpc_metrics,
                       std::unique_ptr<ConnectionContext> context)
    : reactor_(reactor),
      stream_(std::move(stream)),
      direction_(direction),
      rpc_metrics_(*rpc_metrics),
      context_(std::move(context)),
      last_activity_time_(CoarseMonoClock::Now()) {
  const auto metric_entity = reactor->messenger().metric_entity();
  handler_latency_outbound_transfer_ = metric_entity ?
      METRIC_handler_latency_outbound_transfer.Instantiate(metric_entity) : nullptr;
  IncrementCounter(rpc_metrics_.connections_created);
  IncrementGauge(rpc_metrics_.connections_alive);
}

Connection::~Connection() {
  DecrementGauge(rpc_metrics_.connections_alive);
}

void UpdateIdleReason(const char* message, bool* result, std::string* reason) {
  *result = false;
  if (reason) {
    AppendWithSeparator(message, reason);
  }
}

bool Connection::Idle(std::string* reason_not_idle) const {
  bool result = stream_->Idle(reason_not_idle);

  // Connection is not idle if calls are waiting for a response.
  if (!active_calls_.empty()) {
    UpdateIdleReason("awaiting response", &result, reason_not_idle);
  }

  // Check upstream logic (i.e. processing calls, etc.)
  return context_->Idle(reason_not_idle) && result;
}

std::string Connection::ReasonNotIdle() const {
  std::string reason;
  Idle(&reason);
  return reason;
}

void Connection::Shutdown(const Status& status) {
  {
    std::vector<OutboundDataPtr> outbound_data_being_processed;
    {
      std::lock_guard lock(outbound_data_queue_mtx_);

      outbound_data_being_processed.swap(outbound_data_to_process_);
      shutdown_status_ = status;
    }

    for (auto& call : outbound_data_being_processed) {
      call->Transferred(status, this);
    }
  }

  context_->Shutdown(status);
  stream_->Shutdown(status);

  // Clear any calls which have been sent and were awaiting a response.
  for (auto& v : active_calls_) {
    if (v.call && !v.call->IsFinished()) {
      v.call->SetFailed(status);
    }
  }
  active_calls_.clear();

  timer_.Shutdown();

  // TODO(bogdan): re-enable once we decide how to control verbose logs better...
  // LOG_WITH_PREFIX(INFO) << "Connection::Shutdown completed, status: " << status;
}

void Connection::OutboundQueued() {
  auto status = stream_->TryWrite();
  if (!status.ok()) {
    VLOG_WITH_PREFIX(1) << "Write failed: " << status;
    // Even though we are already on the reactor thread, try to schedule a task so that it would run
    // later than all other already scheduled tasks, to preserve historical behavior.
    auto scheduling_status = reactor_->ScheduleReactorTask(
        MakeFunctorReactorTask(
            std::bind(&Reactor::DestroyConnection, reactor_, this, status),
            shared_from_this(), SOURCE_LOCATION()));
    if (!scheduling_status.ok()) {
      LOG(WARNING) << "Failed to schedule DestroyConnection: " << scheduling_status
                   << "on reactor, destroying connection immediately";
      reactor_->DestroyConnection(this, status);
    }
  }
}

void Connection::HandleTimeout(ev::timer& watcher, int revents) {  // NOLINT
  DVLOG_WITH_PREFIX(5) << "Connection::HandleTimeout revents: " << revents
                       << " connected: " << stream_->IsConnected();

  if (EV_ERROR & revents) {
    LOG_WITH_PREFIX(WARNING) << "Got an error in handle timeout";
    return;
  }

  auto now = CoarseMonoClock::Now();

  CoarseTimePoint deadline = CoarseTimePoint::max();
  if (!stream_->IsConnected()) {
    const MonoDelta timeout = FLAGS_rpc_connection_timeout_ms * 1ms;
    auto current_last_activity_time = last_activity_time();
    deadline = current_last_activity_time + timeout;
    DVLOG_WITH_PREFIX(5) << Format("now: $0, deadline: $1, timeout: $2", now, deadline, timeout);
    if (now > deadline) {
      auto passed = reactor_->cur_time() - current_last_activity_time;
      reactor_->DestroyConnection(
          this,
          STATUS_EC_FORMAT(NetworkError, NetworkError(NetworkErrorCode::kConnectFailed),
                           "Connect timeout $0, passed: $1, timeout: $2",
                           ToString(), passed, timeout));
      return;
    }
  }

  CleanupExpirationQueue(now);

  if (!active_calls_.empty()) {
    deadline = std::min(deadline, active_calls_.get<ExpirationTag>().begin()->expires_at);
  }

  if (deadline != CoarseTimePoint::max()) {
    timer_.Start(deadline - now);
  }
}

void Connection::CleanupExpirationQueue(CoarseTimePoint now) {
  auto& index = active_calls_.get<ExpirationTag>();
  while (!index.empty() && index.begin()->expires_at <= now) {
    auto& top = *index.begin();
    auto call = top.call;
    auto handle = top.handle;
    auto erase = false;
    if (!call->IsFinished()) {
      call->SetTimedOut();
      if (handle != std::numeric_limits<size_t>::max()) {
        erase = stream_->Cancelled(handle);
      }
    }
    if (erase) {
      index.erase(index.begin());
    } else {
      index.modify(index.begin(), [](auto& active_call) {
        active_call.call.reset();
        active_call.expires_at = CoarseTimePoint::max();
      });
    }
  }
}

void Connection::QueueOutboundCall(const OutboundCallPtr& call) {
  CHECK_NOTNULL(call);
  CHECK_EQ(direction_, Direction::CLIENT);

  auto handle = DoQueueOutboundData(call, /* batch= */ true);

  // Set up the timeout timer.
  MonoDelta timeout = call->controller()->timeout();
  CoarseTimePoint expires_at;
  if (timeout.Initialized()) {
    auto now = CoarseMonoClock::Now();
    expires_at = now + timeout.ToSteadyDuration();
    auto reschedule = active_calls_.empty() ||
                      active_calls_.get<ExpirationTag>().begin()->expires_at > expires_at;
    CleanupExpirationQueue(now);
    if (reschedule && (stream_->IsConnected() ||
                       expires_at < last_activity_time() + FLAGS_rpc_connection_timeout_ms * 1ms)) {
      timer_.Start(timeout.ToSteadyDuration());
    }
  } else {
    // Call never expires.
    expires_at = CoarseTimePoint::max();
  }
  active_calls_.insert(ActiveCall {
    .id = call->call_id(),
    .call = call,
    .expires_at = expires_at,
    .handle = handle,
  });

  call->SetQueued();
}

size_t Connection::DoQueueOutboundData(OutboundDataPtr outbound_data, bool batch) {
  DVLOG_WITH_PREFIX(4) << "Connection::DoQueueOutboundData: " << AsString(outbound_data);

  Status shutdown_status;
  {
    std::lock_guard lock(outbound_data_queue_mtx_);
    shutdown_status = shutdown_status_;
  }

  if (!shutdown_status.ok()) {
    YB_LOG_EVERY_N_SECS(INFO, 5) << "Connection::DoQueueOutboundData data: "
                                 << AsString(outbound_data) << " shutdown_status_: "
                                 << shutdown_status;
    outbound_data->Transferred(shutdown_status, this);
    return std::numeric_limits<size_t>::max();
  }

  // Check before and after calling Send. Before to reset state, if we were over the limit, but are
  // now back in good standing. After, to check if we are now over the limit.
  Status s = context_->ReportPendingWriteBytes(stream_->GetPendingWriteBytes());
  if (!s.ok()) {
    Shutdown(s);
    return std::numeric_limits<size_t>::max();
  }
  auto result = stream_->Send(std::move(outbound_data));
  if (!result.ok()) {
    Shutdown(result.status());
    return std::numeric_limits<size_t>::max();
  }
  s = context_->ReportPendingWriteBytes(stream_->GetPendingWriteBytes());
  if (!s.ok()) {
    Shutdown(s);
    return std::numeric_limits<size_t>::max();
  }

  if (!batch) {
    OutboundQueued();
  }

  return *result;
}

void Connection::ParseReceived() {
  stream_->ParseReceived();
}

Result<size_t> Connection::ProcessReceived(ReadBufferFull read_buffer_full) {
  auto result = context_->ProcessCalls(
      shared_from_this(), ReadBuffer().AppendedVecs(), read_buffer_full);
  VLOG_WITH_PREFIX(4) << "context_->ProcessCalls result: " << AsString(result);
  if (PREDICT_FALSE(!result.ok())) {
    LOG_WITH_PREFIX(WARNING) << "Command sequence failure: " << result.status();
    return result.status();
  }

  if (!result->consumed && ReadBuffer().Full() && context_->Idle()) {
    return STATUS_FORMAT(
        InvalidArgument, "Command is greater than read buffer, exist data: $0",
        IoVecsFullSize(ReadBuffer().AppendedVecs()));
  }

  ReadBuffer().Consume(result->consumed, result->buffer);

  return result->bytes_to_skip;
}

Status Connection::HandleCallResponse(CallData* call_data) {
  CallResponse resp;
  RETURN_NOT_OK(resp.ParseFrom(call_data));

  ++responded_call_count_;
  auto awaiting = active_calls_.find(resp.call_id());
  if (awaiting == active_calls_.end()) {
    LOG_WITH_PREFIX(DFATAL) << "Got a response for call id " << resp.call_id() << " which "
                            << "was not pending! Ignoring.";
    return Status::OK();
  }
  auto call = awaiting->call;
  active_calls_.erase(awaiting);

  if (PREDICT_FALSE(!call)) {
    // The call already failed due to a timeout.
    VLOG_WITH_PREFIX(1) << "Got response to call id " << resp.call_id()
                        << " after client already timed out";
    return Status::OK();
  }

  call->SetResponse(std::move(resp));

  return Status::OK();
}

std::string Connection::ToString() const {
  // This may be called from other threads, so we only include immutable fields.
  constexpr auto* format = "Connection ($0) $1 $2 => $3";
  const void* self = this;
  switch (direction_) {
    case Direction::SERVER:
      return Format(format, self, "server", remote(), local());
    case Direction::CLIENT:
      return Format(format, self, "client", local(), remote());
  }
  FATAL_INVALID_ENUM_VALUE(Direction, direction_);
}

Status Connection::DumpPB(const DumpRunningRpcsRequestPB& req,
                          RpcConnectionPB* resp) {
  resp->set_remote_ip(yb::ToString(remote()));
  resp->set_state(context_->State());

  const uint64_t processed_call_count =
      direction_ == Direction::CLIENT ? responded_call_count_.load(std::memory_order_acquire)
                                      : context_->ProcessedCallCount();
  if (processed_call_count > 0) {
    resp->set_processed_call_count(processed_call_count);
  }

  context_->DumpPB(req, resp);

  switch (direction_) {
    case Direction::CLIENT: {
      auto call_in_flight = resp->add_calls_in_flight();
      for (auto& entry : active_calls_) {
        if (entry.call && entry.call->DumpPB(req, call_in_flight)) {
          call_in_flight = resp->add_calls_in_flight();
        }
      }
      resp->mutable_calls_in_flight()->DeleteSubrange(resp->calls_in_flight_size() - 1, 1);
      stream_->DumpPB(req, resp);
      return Status::OK();
    }
    case Direction::SERVER:
      return Status::OK();
  }

  FATAL_INVALID_ENUM_VALUE(Direction, direction_);
}

void Connection::QueueOutboundDataBatch(const OutboundDataBatch& batch) {
  for (const auto& call : batch) {
    DoQueueOutboundData(call, /* batch */ true);
  }

  OutboundQueued();
}

Status Connection::QueueOutboundData(OutboundDataPtr outbound_data) {
  if (reactor_->IsCurrentThread()) {
    ReactorThreadRoleGuard guard;
    DoQueueOutboundData(std::move(outbound_data), /* batch */ false);
    return Status::OK();
  }

  bool was_empty;
  std::shared_ptr<ReactorTask> process_response_queue_task;
  {
    UniqueLock outbound_data_queue_lock(outbound_data_queue_mtx_);
    if (!shutdown_status_.ok()) {
      auto task = MakeFunctorReactorTaskWithAbort(
          std::bind(&OutboundData::Transferred, outbound_data, _2, /* conn */ nullptr),
          SOURCE_LOCATION());
      outbound_data_queue_lock.unlock();
      auto scheduling_status = reactor_->ScheduleReactorTask(task, true /* even_if_not_running */);
      LOG_IF_WITH_PREFIX(DFATAL, !scheduling_status.ok())
          << "Failed to schedule OutboundData::Transferred: " << scheduling_status;
      return scheduling_status;
    }
    was_empty = outbound_data_to_process_.empty();
    outbound_data_to_process_.push_back(std::move(outbound_data));
    if (was_empty && !process_response_queue_task_) {
      process_response_queue_task_ =
          MakeFunctorReactorTask(std::bind(&Connection::ProcessResponseQueue, this),
                                 shared_from_this(), SOURCE_LOCATION());
    }
    process_response_queue_task = process_response_queue_task_;
  }

  if (was_empty) {
    return reactor_->ScheduleReactorTask(process_response_queue_task);
  }
  return Status::OK();
}

void Connection::ProcessResponseQueue() {
  {
    std::lock_guard lock(outbound_data_queue_mtx_);
    outbound_data_to_process_.swap(outbound_data_being_processed_);
  }

  if (!outbound_data_being_processed_.empty()) {
    for (auto& call : outbound_data_being_processed_) {
      DoQueueOutboundData(std::move(call), /* batch */ true);
    }
    outbound_data_being_processed_.clear();
    OutboundQueued();
  }
}

Status Connection::Start(ev::loop_ref* loop) {
  context_->SetEventLoop(loop);

  RETURN_NOT_OK(stream_->Start(direction_ == Direction::CLIENT, loop, this));

  timer_.Init(*loop);
  timer_.SetCallback<Connection, &Connection::HandleTimeout>(this); // NOLINT

  if (!stream_->IsConnected()) {
    timer_.Start(FLAGS_rpc_connection_timeout_ms * 1ms);
  }

  auto self = shared_from_this();
  return context_->AssignConnection(self);
}

void Connection::Connected() {
  context_->Connected(shared_from_this());
}

StreamReadBuffer& Connection::ReadBuffer() {
  return context_->ReadBuffer();
}

const Endpoint& Connection::remote() const {
  return stream_->Remote();
}

const Protocol* Connection::protocol() const {
  return stream_->GetProtocol();
}

const Endpoint& Connection::local() const {
  return stream_->Local();
}

void Connection::Close() {
  stream_->Close();
}

void Connection::UpdateLastActivity() {
  auto new_last_activity_time = reactor_->cur_time();
  VLOG_WITH_PREFIX(4) << "Updated last_activity_time_=" << AsString(new_last_activity_time);
  last_activity_time_.store(new_last_activity_time, std::memory_order_release);
}

void Connection::UpdateLastRead() {
  context_->UpdateLastRead(shared_from_this());
}

void Connection::UpdateLastWrite() {
  context_->UpdateLastWrite(shared_from_this());
}

void Connection::Transferred(const OutboundDataPtr& data, const Status& status) {
  data->Transferred(status, this);
}

void Connection::Destroy(const Status& status) {
  reactor_->DestroyConnection(this, status);
}

std::string Connection::LogPrefix() const {
  return ToString() + ": ";
}

}  // namespace rpc
}  // namespace yb
