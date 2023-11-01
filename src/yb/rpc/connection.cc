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

#include <atomic>
#include <sstream>
#include <thread>
#include <utility>

#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/rpc/connection_context.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/network_error.h"
#include "yb/rpc/reactor.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/rpc_introspection.pb.h"
#include "yb/rpc/rpc_metrics.h"
#include "yb/rpc/reactor_thread_role.h"

#include "yb/util/debug-util.h"
#include "yb/util/enums.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/string_util.h"
#include "yb/util/trace.h"
#include "yb/util/tsan_util.h"
#include "yb/util/unique_lock.h"

using namespace std::literals;
using namespace std::placeholders;
using std::shared_ptr;
using std::vector;
using strings::Substitute;

DEFINE_uint64(rpc_connection_timeout_ms, yb::NonTsanVsTsan(15000, 30000),
    "Timeout for RPC connection operations");

METRIC_DEFINE_histogram_with_percentiles(
    server, handler_latency_outbound_transfer, "Time taken to transfer the response ",
    yb::MetricUnit::kMicroseconds, "Microseconds spent to queue and write the response to the wire",
    60000000LU, 2);

DEFINE_test_flag(double, simulated_sent_stuck_call_probability, 0.0,
    "Probability of a simulated stuck call in SENT state. We set the state to SENT without "
    "actually sending the call.");

DEFINE_test_flag(double, simulated_failure_to_send_call_probability, 0.0,
    "Probability of a simulated failure to send a call's data, which should result in connection "
    "being closed.");


namespace yb {
namespace rpc {

namespace {

template<typename ContainerIndex>
void ActiveCallExpired(
    ContainerIndex& index,
    typename ContainerIndex::iterator iter,
    Reactor* reactor,
    Stream* stream) ON_REACTOR_THREAD {
  auto call = iter->call;
  if (!call) {
    LOG(DFATAL) << __func__ << ": call is null in " << iter->ToString();
    return;
  }
  auto handle = iter->handle;
  auto erase = false;
  if (!call->IsFinished()) {
    call->SetTimedOut();
    if (handle != kUnknownCallHandle) {
      erase = stream->Cancelled(handle);
    }
  }
  if (erase) {
    index.erase(iter);
    call->SetActiveCallState(ActiveCallState::kErasedOnExpiration);
  } else {
    index.modify(iter, [](auto& active_call) {
      active_call.call.reset();
      active_call.expires_at = CoarseTimePoint::max();
    });
    call->SetActiveCallState(ActiveCallState::kResetOnExpiration);
  }
}

}  // anonymous namespace

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
  DCHECK(reactor_->IsCurrentThread());

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

void Connection::Shutdown(const Status& provided_status) {
  DCHECK(reactor_->IsCurrentThread());

  if (provided_status.ok()) {
    LOG_WITH_PREFIX(DFATAL)
        << "Connection shutdown called with an OK status, replacing with an error:\n"
        << GetStackTrace();
  }
  const Status status =
      provided_status.ok()
          ? STATUS_FORMAT(RuntimeError, "Connection shutdown called with OK status")
          : provided_status;

  {
    std::vector<OutboundDataPtr> outbound_data_being_processed;
    {
      std::lock_guard<simple_spinlock> lock(outbound_data_queue_mtx_);

      // Perform this compare-and-set when holding outbound_data_queue_mtx_ so that
      // ShutdownStatus() would retrieve the correct status.
      if (shutdown_initiated_.exchange(true, std::memory_order_release)) {
        LOG_WITH_PREFIX(WARNING)
            << "Connection shutdown invoked multiple times. Previously with status "
            << shutdown_status_ << " and now with status " << provided_status
            << ", completed=" << shutdown_completed() << ". Skipping repeated shutdown.";
        return;
      }

      outbound_data_being_processed = std::move(outbound_data_to_process_);
      shutdown_status_ = status;
    }

    shutdown_time_.store(reactor_->cur_time(), std::memory_order_release);

    auto self = shared_from_this();
    for (auto& call : outbound_data_being_processed) {
      call->Transferred(status, self);
    }
  }

  context_->Shutdown(status);
  stream_->Shutdown(status);

  // Clear any calls which have been sent and were awaiting a response.
  active_calls_during_shutdown_.store(active_calls_.size(), std::memory_order_release);
  for (auto& v : active_calls_) {
    if (v.call) {
      if (!v.call->IsFinished()) {
        v.call->SetFailed(status);
      }
      v.call->SetActiveCallState(ActiveCallState::kErasedOnConnectionShutdown);
    }
  }
  active_calls_.clear();

  timer_.Shutdown();

  // TODO(bogdan): re-enable once we decide how to control verbose logs better...
  // LOG_WITH_PREFIX(INFO) << "Connection::Shutdown completed, status: " << status;
  shutdown_completed_.store(true, std::memory_order_release);
}

Status Connection::OutboundQueued() {
  DCHECK(reactor_->IsCurrentThread());
  RETURN_NOT_OK(ShutdownStatus());
  auto status = stream_->TryWrite();
  if (!status.ok()) {
    VLOG_WITH_PREFIX(1) << "Write failed: " << status;
    ScheduleDestroyConnection(status);
  }
  return status;
}

void Connection::HandleTimeout(ev::timer& watcher, int revents) {  // NOLINT
  DCHECK(reactor_->IsCurrentThread());
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
    DVLOG_WITH_PREFIX(5) << Format(
        "now: $0, deadline: $1, timeout: $2", now, ToStringRelativeToNow(deadline, now), timeout);
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
    ActiveCallExpired(index, index.begin(), reactor_, stream_.get());
  }
}

void Connection::QueueOutboundCall(const OutboundCallPtr& call) {
  DCHECK(call);
  DCHECK_EQ(direction_, Direction::CLIENT);

  size_t handle;

  const bool simulate_stuck_sent_call =
      RandomActWithProbability(FLAGS_TEST_simulated_sent_stuck_call_probability);
  if (simulate_stuck_sent_call) {
    handle = kUnknownCallHandle;
    call->SetQueued();
    auto _ [[maybe_unused]] = call->SetSent();  // NOLINT
    LOG_WITH_PREFIX(WARNING) << "Simulating a call stuck in SENT state: " << call->DebugString();
  } else {
    auto queue_result = DoQueueOutboundData(call, /* batch= */ true);
    if (!queue_result.ok()) {
      // The connection has been shut down by this point, and the callback has been called.
      return;
    }
    handle = *queue_result;
  }

  // Set up the timeout timer.
  MonoDelta timeout = call->controller()->timeout();
  CoarseTimePoint expires_at;
  if (timeout.Initialized()) {
    auto now = CoarseMonoClock::Now();
    expires_at = now + timeout.ToSteadyDuration();
    call->SetExpiration(expires_at);
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
  if (!PREDICT_FALSE(simulate_stuck_sent_call)) {
    call->SetActiveCallState(ActiveCallState::kAdded);
    active_calls_.insert(ActiveCall {
      .id = call->call_id(),
      .call = call,
      .expires_at = expires_at,
      .handle = handle,
    });

    call->SetQueued();
  }
}

void Connection::FailCallAndDestroyConnection(
    const OutboundDataPtr& outbound_data, const Status& status) {
  outbound_data->Transferred(status, shared_from_this());
  ScheduleDestroyConnection(status);
}

void Connection::ScheduleDestroyConnection(const Status& status) {
  if (!queued_destroy_connection_.exchange(true, std::memory_order_acq_rel)) {
    // Even though we are already on the reactor thread, try to schedule a task so that it
    // would run later than all other already scheduled tasks, to preserve historical
    // behavior.
    auto scheduling_status = reactor_->ScheduleReactorTask(MakeFunctorReactorTask(
        std::bind(&Reactor::DestroyConnection, reactor_, this, status), shared_from_this(),
        SOURCE_LOCATION()));
    if (!scheduling_status.ok()) {
      LOG_WITH_PREFIX(WARNING) << "Failed to schedule DestroyConnection: " << scheduling_status
                               << "on reactor, destroying connection immediately";
      reactor_->DestroyConnection(this, status);
    }
  }
}

Result<size_t> Connection::DoQueueOutboundData(OutboundDataPtr outbound_data, bool batch) {
  DCHECK(reactor_->IsCurrentThread());
  DVLOG_WITH_PREFIX(4) << "Connection::DoQueueOutboundData: " << AsString(outbound_data);
  RSTATUS_DCHECK_NOTNULL(outbound_data);

  Status shutdown_status = ShutdownStatus();
  if (!shutdown_status.ok()) {
    YB_LOG_EVERY_N_SECS(INFO, 5) << "Connection::DoQueueOutboundData data: "
                                 << AsString(outbound_data) << " shutdown_status: "
                                 << shutdown_status;
    outbound_data->Transferred(shutdown_status, shared_from_this());
    calls_queued_after_shutdown_.fetch_add(1, std::memory_order_acq_rel);
    return shutdown_status;;
  }

  // If the connection is torn down, then the QueueOutbound() call that
  // eventually runs in the reactor thread will take care of calling
  // ResponseTransferCallbacks::NotifyTransferAborted.

  // Check before and after calling Send. Before to reset state, if we
  // were over the limit; but are now back in good standing. After, to
  // check if we are now over the limit.
  Status s = context_->ReportPendingWriteBytes(stream_->GetPendingWriteBytes());
  if (!s.ok()) {
    FailCallAndDestroyConnection(outbound_data, s);
    return s;
  }

  if (PREDICT_FALSE(
      RandomActWithProbability(FLAGS_TEST_simulated_failure_to_send_call_probability))) {
    s = STATUS_FORMAT(
          NetworkError, "Simulated failure to send outbound data for $0", outbound_data);
    LOG(WARNING) << "Simulated network failure: " << s.ToString();
    FailCallAndDestroyConnection(outbound_data, s);
    return s;
  }

  auto result = stream_->Send(outbound_data);
  if (!result.ok()) {
    FailCallAndDestroyConnection(outbound_data, result.status());
    return result.status();
  }

  s = context_->ReportPendingWriteBytes(stream_->GetPendingWriteBytes());
  if (!s.ok()) {
    FailCallAndDestroyConnection(outbound_data, s);
    return s;
  }

  if (!batch) {
    s = OutboundQueued();
    if (!s.ok()) {
      outbound_data->Transferred(s, shared_from_this());
      // The connection shutdown has already been triggered by OutboundQueued.
      return s;
    }
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
  DCHECK(reactor_->IsCurrentThread());
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
  if (PREDICT_TRUE(call)) {
    call->SetActiveCallState(ActiveCallState::kErasedOnResponse);
  } else {
    // The call already failed due to a timeout.
    VLOG_WITH_PREFIX(1) << "Got response to call id " << resp.call_id()
                        << " after client already timed out";
    return Status::OK();
  }

  call->SetResponse(std::move(resp));

  return Status::OK();
}

std::string Connection::ToString() const {
  // This may be called from other threads, so we cannot
  // include anything in the output about the current state,
  // which might concurrently change from another thread.
  static const char* format = "Connection ($0) $1 $2 => $3";
  const void* self = this;
  if (direction_ == Direction::SERVER) {
    return Format(format, self, "server", remote(), local());
  } else {
    return Format(format, self, "client", local(), remote());
  }
}

void Connection::QueueDumpConnectionState(int32_t call_id, const void* call_ptr) const {
  auto task = MakeFunctorReactorTask(
      std::bind(&Connection::DumpConnectionState, this, call_id, call_ptr), shared_from_this(),
      SOURCE_LOCATION());
  auto scheduling_status = reactor_->ScheduleReactorTask(task);
  LOG_IF_WITH_PREFIX(DFATAL, !scheduling_status.ok())
      << "Failed to schedule call to dump connection state: " << scheduling_status;
}

void Connection::DumpConnectionState(int32_t call_id, const void* call_ptr) const {
  auto earliest_expiry = active_calls_.empty()
                             ? CoarseTimePoint()
                             : active_calls_.get<ExpirationTag>().begin()->expires_at;
  auto found_call_id = active_calls_.find(call_id) != active_calls_.end();
  auto now = CoarseMonoClock::Now();
  LOG_WITH_PREFIX(INFO) << Format(
      "LastActivityTime: $0, "
      "ActiveCalls stats: { "
      "during shutdown: $1, "
      "current size: $2, "
      "earliest expiry: $3 "
      "}, "
      "OutboundCall: { "
      "ptr: $4, "
      "call id: $5, "
      "is active: $6 "
      "}, "
      "Shutdown status: $7, "
      "Shutdown time: $8, "
      "Queue attempts after shutdown: { "
      "calls: $9, "
      "responses: $10 "
      "}",
      /* $0 */ ToStringRelativeToNow(last_activity_time(), now),
      /* $1 */ active_calls_during_shutdown_.load(std::memory_order_acquire),
      /* $2 */ active_calls_.size(),
      /* $3 */ ToStringRelativeToNow(earliest_expiry, now),
      /* $4 */ call_ptr,
      /* $5 */ call_id,
      /* $6 */ found_call_id,
      /* $7 */ shutdown_status_,
      /* $8 */ ToStringRelativeToNow(shutdown_time_, now),
      /* $9 */ calls_queued_after_shutdown_.load(std::memory_order_acquire),
      /* $10 */ responses_queued_after_shutdown_.load(std::memory_order_acquire));
}

Status Connection::DumpPB(const DumpRunningRpcsRequestPB& req,
                          RpcConnectionPB* resp) {
  DCHECK(reactor_->IsCurrentThread());
  resp->set_remote_ip(yb::ToString(remote()));
  resp->set_state(context_->State());

  const uint64_t processed_call_count =
      direction_ == Direction::CLIENT ? responded_call_count_.load(std::memory_order_acquire)
                                      : context_->ProcessedCallCount();
  if (processed_call_count > 0) {
    resp->set_processed_call_count(processed_call_count);
  }

  context_->DumpPB(req, resp);

  if (direction_ == Direction::CLIENT) {
    auto call_in_flight = resp->add_calls_in_flight();
    for (auto& entry : active_calls_) {
      if (entry.call && entry.call->DumpPB(req, call_in_flight)) {
        call_in_flight = resp->add_calls_in_flight();
      }
    }
    resp->mutable_calls_in_flight()->DeleteSubrange(resp->calls_in_flight_size() - 1, 1);
    stream_->DumpPB(req, resp);
  } else if (direction_ != Direction::SERVER) {
    LOG(FATAL) << "Invalid direction: " << to_underlying(direction_);
  }

  return Status::OK();
}

void Connection::QueueOutboundDataBatch(const OutboundDataBatch& batch) {
  DCHECK(reactor_->IsCurrentThread());

  for (const auto& call : batch) {
    // If one of these calls fails and shuts down the connection, all calls after that will fail
    // as well.
    auto ignored_status = DoQueueOutboundData(call, /* batch */ true);
  }

  auto ignored_status = OutboundQueued();
}

Status Connection::QueueOutboundData(OutboundDataPtr outbound_data) {
  CHECK_NOTNULL(outbound_data);
  if (reactor_->IsCurrentThread()) {
    ReactorThreadRoleGuard guard;
    auto result = DoQueueOutboundData(std::move(outbound_data), /* batch */ false);
    if (result.ok()) {
      return Status::OK();
    }
    return result.status();
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
      responses_queued_after_shutdown_.fetch_add(1, std::memory_order_acq_rel);
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
  DCHECK(reactor_->IsCurrentThread());

  {
    std::lock_guard lock(outbound_data_queue_mtx_);
    outbound_data_being_processed_ = std::move(outbound_data_to_process_);
  }

  if (!outbound_data_being_processed_.empty()) {
    for (auto& call : outbound_data_being_processed_) {
      // If one of these calls fails and shuts down the connection, all calls after that will fail
      // as well.
      auto ignored_status = DoQueueOutboundData(std::move(call), /* batch= */ true);
    }
    outbound_data_being_processed_.clear();
    auto ignored_status = OutboundQueued();
  }
}

Status Connection::Start(ev::loop_ref* loop) {
  DCHECK(reactor_->IsCurrentThread());

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

Status Connection::Connected() {
  return context_->Connected(shared_from_this());
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

Status Connection::ShutdownStatus() const {
  if (!shutdown_initiated_.load(std::memory_order_acquire)) {
    return Status::OK();
  }
  std::lock_guard lock(outbound_data_queue_mtx_);
  return shutdown_status_;
}

void Connection::ForceCallExpiration(const OutboundCallPtr& call) {
  auto call_id = call->call_id();
  auto it = active_calls_.find(call_id);
  if (it != active_calls_.end()) {
    // This will call SetTimedOut in addition to updating active_calls_.
    ActiveCallExpired(active_calls_, it, reactor_, stream_.get());
  } else if (!call->IsFinished()) {
    call->SetTimedOut();
  }
}

void Connection::UpdateLastActivity() {
  last_activity_time_ = reactor_->cur_time();
  VLOG_WITH_PREFIX(4) << "Updated last_activity_time_=" << AsString(last_activity_time_);
}

void Connection::UpdateLastRead() {
  context_->UpdateLastRead(shared_from_this());
}

void Connection::UpdateLastWrite() {
  context_->UpdateLastWrite(shared_from_this());
}

void Connection::Transferred(const OutboundDataPtr& data, const Status& status) {
  data->Transferred(status, shared_from_this());
}

void Connection::Destroy(const Status& status) {
  if (!queued_destroy_connection_.exchange(true, std::memory_order_acq_rel)) {
    reactor_->DestroyConnection(this, status);
  }
}

std::string Connection::LogPrefix() const {
  return ToString() + ": ";
}

}  // namespace rpc
}  // namespace yb
