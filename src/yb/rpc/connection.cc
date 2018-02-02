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

#include <iostream>
#include <thread>
#include <utility>

#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/human_readable.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/util/enums.h"

#include "yb/rpc/rpc_introspection.pb.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/reactor.h"
#include "yb/rpc/growable_buffer.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/util/trace.h"

using namespace std::literals;
using std::shared_ptr;
using std::vector;
using strings::Substitute;

DEFINE_uint64(rpc_initial_buffer_size, 4096, "Initial buffer size used for RPC calls");
DEFINE_uint64(rpc_connection_timeout_ms, 15000, "Timeout for RPC connection operations");

METRIC_DEFINE_histogram(
    server, handler_latency_outbound_transfer, "Time taken to transfer the response ",
    yb::MetricUnit::kMicroseconds, "Microseconds spent to queue and write the response to the wire",
    60000000LU, 2);

namespace yb {
namespace rpc {

///
/// Connection
///
Connection::Connection(Reactor* reactor,
                       const Endpoint& remote,
                       int socket,
                       Direction direction,
                       std::unique_ptr<ConnectionContext> context)
    : reactor_(reactor),
      socket_(socket),
      remote_(remote),
      direction_(direction),
      last_activity_time_(CoarseMonoClock::Now()),
      read_buffer_(FLAGS_rpc_initial_buffer_size, context->BufferLimit()),
      context_(std::move(context)) {
  const auto metric_entity = reactor->messenger()->metric_entity();
  handler_latency_outbound_transfer_ = metric_entity ?
      METRIC_handler_latency_outbound_transfer.Instantiate(metric_entity) : nullptr;
}

Connection::~Connection() {
  // Must clear the outbound_transfers_ list before deleting.
  CHECK(sending_.empty());

  // It's crucial that the connection is Shutdown first -- otherwise
  // our destructor will end up calling read_io_.stop() and write_io_.stop()
  // from a possibly non-reactor thread context. This can then make all
  // hell break loose with libev.
  CHECK(!is_epoll_registered_);
}

void Connection::EpollRegister(ev::loop_ref& loop) {  // NOLINT
  DCHECK(reactor_->IsCurrentThread());

  DVLOG(4) << "Registering connection for epoll: " << ToString();
  io_.set(loop);
  io_.set<Connection, &Connection::Handler>(this);
  int events;
  if (connected_) {
    events = ev::READ;
    if (direction_ == Direction::CLIENT) {
      events |= ev::WRITE;
    }
  } else {
    events = ev::WRITE;
  }
  io_.start(socket_.GetFd(), events);

  timer_.set(loop);
  timer_.set<Connection, &Connection::HandleTimeout>(this); // NOLINT

  is_epoll_registered_ = true;
}

bool Connection::Idle() const {
  DCHECK(reactor_->IsCurrentThread());
  // check if we're in the middle of receiving something
  if (!read_buffer_.empty()) {
    return false;
  }
  // check if we still need to send something
  if (!sending_.empty()) {
    return false;
  }
  // can't kill a connection if calls are waiting response
  if (!awaiting_response_.empty()) {
    return false;
  }

  // Check upstream logic (i.e. processing calls etc.)
  if (!context_->Idle()) {
    return false;
  }

  return true;
}

void Connection::ClearSending(const Status& status) {
  // Clear any outbound transfers.
  for (auto& call : sending_outbound_datas_) {
    if (call) {
      call->Transferred(status, this);
    }
  }
  sending_outbound_datas_.clear();
  sending_.clear();
}

void Connection::Shutdown(const Status& status) {
  DCHECK(reactor_->IsCurrentThread());

  {
    std::lock_guard<simple_spinlock> lock(outbound_data_queue_lock_);
    outbound_data_being_processed_.swap(outbound_data_to_process_);
    shutdown_status_ = status;
  }

  if (!read_buffer_.empty()) {
    double secs_since_active = ToSeconds(reactor_->cur_time() - last_activity_time_);
    LOG(WARNING) << "Shutting down connection " << ToString() << " with pending inbound data ("
                 << read_buffer_ << ", last active "
                 << HumanReadableElapsedTime::ToShortString(secs_since_active)
                 << " ago, status=" << status.ToString() << ")";
  }

  // Clear any calls which have been sent and were awaiting a response.
  for (auto& v : awaiting_response_) {
    if (v.second) {
      v.second->SetFailed(status);
    }
  }
  awaiting_response_.clear();

  ClearSending(status);

  for (auto& call : outbound_data_being_processed_) {
    call->Transferred(status, this);
  }
  outbound_data_being_processed_.clear();

  io_.stop();
  timer_.stop();
  is_epoll_registered_ = false;
  WARN_NOT_OK(socket_.Close(), "Error closing socket");
}

void Connection::OutboundQueued() {
  DCHECK(reactor_->IsCurrentThread());

  if (connected_ && !waiting_write_ready_) {
    // If we weren't waiting write to be ready, we could try to write data to socket.
    auto status = DoWrite();
    if (!status.ok()) {
      reactor_->ScheduleReactorTask(
        MakeFunctorReactorTask(std::bind(&Reactor::DestroyConnection,
                                         reactor_,
                                         this,
                                         status),
                               shared_from_this()));
    }
  }
}

void StartTimer(CoarseMonoClock::Duration left, ev::timer* timer) {
  timer->start(MonoDelta(left).ToSeconds(), 0);
}

void Connection::HandleTimeout(ev::timer& watcher, int revents) {  // NOLINT
  DCHECK(reactor_->IsCurrentThread());

  if (EV_ERROR & revents) {
    LOG(WARNING) << "Connection " << ToString() << " got an error in handle timeout";
    return;
  }

  auto now = CoarseMonoClock::Now();

  if (!connected_) {
    auto deadline = last_activity_time_ + FLAGS_rpc_connection_timeout_ms * 1ms;
    if (now > deadline) {
      auto passed = reactor_->cur_time() - last_activity_time_;
      reactor_->DestroyConnection(
          this, STATUS_FORMAT(NetworkError, "Connect timeout, passed: $0", passed));
      return;
    }

    StartTimer(deadline - now, &timer_);
  }

  while (!expiration_queue_.empty() && expiration_queue_.top().first <= now) {
    auto call = expiration_queue_.top().second.lock();
    expiration_queue_.pop();
    if (call && !call->IsFinished()) {
      call->SetTimedOut();
      auto i = awaiting_response_.find(call->call_id());
      if (i != awaiting_response_.end()) {
        i->second.reset();
      } else {
        LOG(ERROR) << "Timeout of non awaiting call: " << call->call_id();
        DCHECK(i != awaiting_response_.end());
      }
    }
  }

  if (!expiration_queue_.empty()) {
    StartTimer(expiration_queue_.top().first - now, &timer_);
  }
}

void Connection::QueueOutboundCall(const OutboundCallPtr& call) {
  DCHECK(call);
  DCHECK_EQ(direction_, Direction::CLIENT);
  DCHECK(reactor_->IsCurrentThread());

  if (PREDICT_FALSE(!shutdown_status_.ok())) {
    // Already shutdown
    call->SetFailed(shutdown_status_);
    return;
  }

  // Serialize the actual bytes to be put on the wire.
  call->Serialize(&sending_);

  sending_outbound_datas_.resize(sending_.size());
  sending_outbound_datas_.back() = call;
  call->SetQueued();
}

void Connection::Handler(ev::io& watcher, int revents) {  // NOLINT
  DCHECK(reactor_->IsCurrentThread());

  DVLOG(3) << ToString() << " Handler(revents=" << revents << ")";
  auto status = Status::OK();
  if (revents & EV_ERROR) {
    status = STATUS(NetworkError, ToString() + ": Handler encountered an error");
  }

  if (status.ok() && (revents & EV_READ)) {
    status = ReadHandler();
  }

  if (status.ok() && (revents & EV_WRITE)) {
    bool just_connected = !connected_;
    if (just_connected) {
      connected_ = true;
      context_->Connected(shared_from_this());
    }
    status = WriteHandler(just_connected);
  }

  if (status.ok()) {
    int events = ev::READ;
    waiting_write_ready_ = !sending_.empty();
    if (waiting_write_ready_) {
      events |= ev::WRITE;
    }
    io_.set(events);
  } else {
    reactor_->DestroyConnection(this, status);
  }
}

Status Connection::ReadHandler() {
  DCHECK(reactor_->IsCurrentThread());
  last_activity_time_ = reactor_->cur_time();

  for (;;) {
    auto received = Receive();
    if (PREDICT_FALSE(!received.ok())) {
      if (received.status().error_code() == ESHUTDOWN) {
        VLOG(1) << ToString() << " shut down by remote end.";
      } else {
        LOG(INFO) << ToString() << " recv error: " << received;
      }
      return received.status();
    }
    // Exit the loop if we did not receive anything.
    if (!received.get()) {
      return Status::OK();
    }
    // If we were not able to process next call exit loop.
    // If status is ok, it means that we just do not have enough data to process yet.
    auto continue_receiving = TryProcessCalls();
    if (!continue_receiving.ok()) {
      return continue_receiving.status();
    }
    if (!continue_receiving.get()) {
      return Status::OK();
    }
  }
}

Result<bool> Connection::Receive() {
  RETURN_NOT_OK(read_buffer_.PrepareRead());

  size_t max_receive = context_->MaxReceive(Slice(read_buffer_.begin(), read_buffer_.size()));
  DCHECK_GT(max_receive, read_buffer_.size());
  // This should not happen, but at least avoid crash if something went wrong.
  if (PREDICT_FALSE(max_receive <= read_buffer_.size())) {
    LOG(ERROR) << "Max receive: " << max_receive << ", less existing data: " << read_buffer_.size();
    max_receive = std::numeric_limits<size_t>::max();
  } else {
    max_receive -= read_buffer_.size();
  }
  max_receive = std::min(max_receive, static_cast<size_t>(std::numeric_limits<int32_t>::max()));
  max_receive = std::min(max_receive, read_buffer_.capacity_left());

  const int32_t remaining_buf_capacity = static_cast<int32_t>(max_receive);
  int32_t nread = 0;
  auto status = socket_.Recv(read_buffer_.write_position(), remaining_buf_capacity, &nread);
  if (!status.ok()) {
    if (Socket::IsTemporarySocketError(status)) {
      return false;
    }
    return status;
  }

  read_buffer_.DataAppended(nread);
  return nread != 0;
}

Result<bool> Connection::TryProcessCalls() {
  DCHECK(reactor_->IsCurrentThread());

  if (read_buffer_.empty()) {
    return false;
  }

  Slice bytes_to_process(read_buffer_.begin(), read_buffer_.size());
  size_t consumed = 0;
  auto result = context_->ProcessCalls(shared_from_this(), bytes_to_process, &consumed);
  if (PREDICT_FALSE(!result.ok())) {
    LOG(WARNING) << ToString() << " command sequence failure: " << result.ToString();
    return result;
  }
  read_buffer_.Consume(consumed);
  return true;
}

Status Connection::HandleCallResponse(Slice call_data) {
  DCHECK(reactor_->IsCurrentThread());
  CallResponse resp;
  RETURN_NOT_OK(resp.ParseFrom(call_data));

  ++responded_call_count_;
  auto awaiting = awaiting_response_.find(resp.call_id());
  if (awaiting == awaiting_response_.end()) {
    LOG(ERROR) << ToString() << ": Got a response for call id " << resp.call_id() << " which "
               << "was not pending! Ignoring.";
    DCHECK(awaiting != awaiting_response_.end());
    return Status::OK();
  }
  auto call = awaiting->second;
  awaiting_response_.erase(awaiting);

  if (PREDICT_FALSE(!call)) {
    // The call already failed due to a timeout.
    VLOG(1) << "Got response to call id " << resp.call_id() << " after client already timed out";
    return Status::OK();
  }

  call->SetResponse(std::move(resp));

  return Status::OK();
}

Status Connection::WriteHandler(bool just_connected) {
  DCHECK(reactor_->IsCurrentThread());

  if (sending_.empty()) {
    LOG_IF(WARNING, !just_connected) << ToString() << " got a ready-to-write callback, "
                                     << "but there is nothing to write.";
    return Status::OK();
  }

  return DoWrite();
}

Status Connection::DoWrite() {
  if (!is_epoll_registered_) {
    return Status::OK();
  }
  while (!sending_.empty()) {
    const size_t kMaxIov = 16;
    iovec iov[kMaxIov];
    const int iov_len = static_cast<int>(std::min(kMaxIov, sending_.size()));
    size_t offset = send_position_;
    for (auto i = 0; i != iov_len; ++i) {
      iov[i].iov_base = sending_[i].data() + offset;
      iov[i].iov_len = sending_[i].size() - offset;
      offset = 0;
    }

    last_activity_time_ = reactor_->cur_time();
    int32_t written = 0;

    auto status = socket_.Writev(iov, iov_len, &written);
    if (PREDICT_FALSE(!status.ok())) {
      if (!Socket::IsTemporarySocketError(status)) {
        LOG(WARNING) << ToString() << " send error: " << status.ToString();
        return status;
      } else {
        waiting_write_ready_ = true;
        io_.set(ev::READ|ev::WRITE);
        return Status::OK();
      }
    }

    send_position_ += written;
    while (!sending_.empty() && send_position_ >= sending_.front().size()) {
      auto call = sending_outbound_datas_.front();
      send_position_ -= sending_.front().size();
      sending_.pop_front();
      sending_outbound_datas_.pop_front();
      if (call) {
        call->Transferred(Status::OK(), this);
      }
    }
  }

  return Status::OK();
}

void Connection::CallSent(OutboundCallPtr call) {
  DCHECK(reactor_->IsCurrentThread());

  awaiting_response_.emplace(call->call_id(), call);

  // Set up the timeout timer.
  const MonoDelta& timeout = call->controller()->timeout();
  if (timeout.Initialized()) {
    auto expires_at = CoarseMonoClock::Now() + timeout.ToSteadyDuration();
    auto reschedule = expiration_queue_.empty() || expiration_queue_.top().first > expires_at;
    expiration_queue_.emplace(expires_at, call);
    if (reschedule) {
      StartTimer(timeout.ToSteadyDuration(), &timer_);
    }
  }
}

std::string Connection::ToString() const {
  // This may be called from other threads, so we cannot
  // include anything in the output about the current state,
  // which might concurrently change from another thread.
  static const char* format = "Connection ($0) $1 $2 => $3";
  if (direction_ == Direction::SERVER) {
    return strings::Substitute(format, this, "server", yb::ToString(remote_), yb::ToString(local_));
  } else {
    return strings::Substitute(format, this, "client", yb::ToString(local_), yb::ToString(remote_));
  }
}

Status Connection::DumpPB(const DumpRunningRpcsRequestPB& req,
                          RpcConnectionPB* resp) {
  DCHECK(reactor_->IsCurrentThread());
  resp->set_remote_ip(yb::ToString(remote_));
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
    for (auto& entry : awaiting_response_) {
      if (entry.second && entry.second->DumpPB(req, call_in_flight)) {
        call_in_flight = resp->add_calls_in_flight();
      }
    }
    for (auto& call : sending_outbound_datas_) {
      if (call && call->DumpPB(req, call_in_flight)) {
        call_in_flight = resp->add_calls_in_flight();
      }
    }
    resp->mutable_calls_in_flight()->DeleteSubrange(resp->calls_in_flight_size() - 1, 1);
  } else if (direction_ != Direction::SERVER) {
    LOG(FATAL) << "Invalid direction: " << util::to_underlying(direction_);
  }

  return Status::OK();
}

void Connection::QueueOutboundDataBatch(const OutboundDataBatch& batch) {
  DCHECK(reactor_->IsCurrentThread());

  for (const auto& call : batch) {
    DoQueueOutboundData(call, /* batch */ true);
  }

  OutboundQueued();
}

void Connection::QueueOutboundData(OutboundDataPtr outbound_data) {
  // This is usually called by the IPC worker thread when the response is set, but in some
  // circumstances may also be called by the reactor thread (e.g. if the service has shut down).
  // In addition to this, its also called for processing events generated by the server.

  if (reactor_->IsCurrentThread()) {
    DoQueueOutboundData(std::move(outbound_data), /* batch */ false);
    return;
  }

  bool was_empty = false;
  {
    std::unique_lock<simple_spinlock> lock(outbound_data_queue_lock_);
    if (!shutdown_status_.ok()) {
      auto task = MakeFunctorReactorTask(std::bind(&OutboundData::Transferred,
                                                   outbound_data,
                                                   shutdown_status_,
                                                   nullptr));
      lock.unlock();
      reactor_->ScheduleReactorTask(task);
      return;
    }
    was_empty = outbound_data_to_process_.empty();
    outbound_data_to_process_.push_back(std::move(outbound_data));
    if (!process_response_queue_task_) {
      process_response_queue_task_ =
          MakeFunctorReactorTask(std::bind(&Connection::ProcessResponseQueue, this),
                                 shared_from_this());
    }
  }

  if (was_empty) {
    reactor_->ScheduleReactorTask(process_response_queue_task_);
  }
}

void Connection::ProcessResponseQueue() {
  DCHECK(reactor_->IsCurrentThread());

  {
    std::lock_guard<simple_spinlock> lock(outbound_data_queue_lock_);
    outbound_data_to_process_.swap(outbound_data_being_processed_);
  }

  if (!outbound_data_being_processed_.empty()) {
    for (auto &call : outbound_data_being_processed_) {
      DoQueueOutboundData(std::move(call), /* batch */ true);
    }
    outbound_data_being_processed_.clear();
    OutboundQueued();
  }
}

void Connection::DoQueueOutboundData(OutboundDataPtr outbound_data, bool batch) {
  DCHECK(reactor_->IsCurrentThread());

  if (!shutdown_status_.ok()) {
    outbound_data->Transferred(shutdown_status_, this);
    return;
  }

  // If the connection is torn down, then the QueueOutbound() call that
  // eventually runs in the reactor thread will take care of calling
  // ResponseTransferCallbacks::NotifyTransferAborted.

  outbound_data->Serialize(&sending_);

  sending_outbound_datas_.resize(sending_.size());
  sending_outbound_datas_.back() = outbound_data;

  if (!batch) {
    OutboundQueued();
  }
}

Status Connection::Start(ev::loop_ref* loop) {
  DCHECK(reactor_->IsCurrentThread());

  RETURN_NOT_OK(socket_.SetNoDelay(true));
  RETURN_NOT_OK(socket_.SetSendTimeout(FLAGS_rpc_connection_timeout_ms * 1ms));
  RETURN_NOT_OK(socket_.SetRecvTimeout(FLAGS_rpc_connection_timeout_ms * 1ms));

  if (direction_ == Direction::CLIENT) {
    auto status = socket_.Connect(remote_);
    if (!status.ok() && !Socket::IsTemporarySocketError(status)) {
      LOG(WARNING) << "Failed to create connection " << ToString()
                   << " because connect failed: " << status;
      return status;
    }
  }
  RETURN_NOT_OK(socket_.GetSocketAddress(&local_));

  EpollRegister(*loop);

  if (!connected_) {
    StartTimer(FLAGS_rpc_connection_timeout_ms * 1ms, &timer_);
  }

  auto self = shared_from_this();
  context_->AssignConnection(self);
  if (direction_ == Direction::SERVER) {
    context_->Connected(self);
  }

  return Status::OK();
}

}  // namespace rpc
}  // namespace yb
