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

#include "yb/rpc/connection.h"

#include <iostream>
#include <thread>
#include <utility>

#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/human_readable.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/util/enums.h"

#include "yb/rpc/auth_store.h"
#include "yb/rpc/rpc_introspection.pb.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/negotiation.h"
#include "yb/rpc/reactor.h"
#include "yb/rpc/growable_buffer.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/sasl_client.h"
#include "yb/rpc/sasl_server.h"

#include "yb/util/trace.h"

using std::shared_ptr;
using std::vector;
using strings::Substitute;

namespace yb {
namespace rpc {

DEFINE_uint64(rpc_initial_buffer_size, 4096, "Initial buffer size used for RPC calls");

METRIC_DEFINE_histogram(
    server, handler_latency_outbound_transfer, "Time taken to transfer the response ",
    yb::MetricUnit::kMicroseconds, "Microseconds spent to queue and write the response to the wire",
    60000000LU, 2);

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
      last_activity_time_(MonoTime::Now(MonoTime::FINE)),
      read_buffer_(FLAGS_rpc_initial_buffer_size, context->BufferLimit()),
      context_(std::move(context)) {
  auto status = socket_.GetSocketAddress(&local_);
  if (!status.ok()) {
    LOG(WARNING) << "Failed to get local address for: " << socket;
  }
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

Status Connection::SetNonBlocking(bool enabled) {
  return socket_.SetNonBlocking(enabled);
}

void Connection::EpollRegister(ev::loop_ref& loop) {  // NOLINT
  DCHECK(reactor_->IsCurrentThread());
  DVLOG(4) << "Registering connection for epoll: " << ToString();
  io_.set(loop);
  io_.set<Connection, &Connection::Handler>(this);
  int events = ev::READ;
  if (direction_ == Direction::CLIENT && negotiation_complete_) {
    events |= ev::WRITE;
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

  // We are not idle if we are in the middle of connection negotiation.
  if (!negotiation_complete_) {
    return false;
  }

  return true;
}

void Connection::ClearSending(const Status& status) {
  // Clear any outbound transfers.
  for (auto& call : sending_outbound_datas_) {
    if (call) {
      call->Transferred(status);
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
    double secs_since_active = reactor_->cur_time()
        .GetDeltaSince(last_activity_time_).ToSeconds();
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
    call->Transferred(status);
  }
  outbound_data_being_processed_.clear();

  io_.stop();
  is_epoll_registered_ = false;
  WARN_NOT_OK(socket_.Close(), "Error closing socket");
}

void Connection::OutboundQueued() {
  DCHECK(reactor_->IsCurrentThread());

  if (negotiation_complete_ && !waiting_write_ready_) {
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

void Connection::HandleTimeout(ev::timer& watcher, int revents) {  // NOLINT
  DCHECK(reactor_->IsCurrentThread());

  if (EV_ERROR & revents) {
    LOG(WARNING) << "Connection " << ToString() << " got an error in handle timeout";
    return;
  }

  auto now = MonoTime::FineNow();
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
    auto left = expiration_queue_.top().first - now;
    timer_.start(left.ToSeconds(), 0);
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

void Connection::set_user_credentials(const UserCredentials& user_credentials) {
  user_credentials_.CopyFrom(user_credentials);
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
    status = WriteHandler();
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
    bool received = false;
    auto status = Receive(&received);
    if (PREDICT_FALSE(!status.ok())) {
      if (status.posix_code() == ESHUTDOWN) {
        VLOG(1) << ToString() << " shut down by remote end.";
      } else {
        LOG(WARNING) << ToString() << " recv error: " << status.ToString();
      }
      return status;
    }
    // Exit the loop if we did not receive anything.
    if (!received) {
      return Status::OK();
    }
    // If we were not able to process next call exit loop.
    // If status is ok, it means that we just do not have enough data to process yet.
    bool continue_receiving = false;
    status = TryProcessCalls(&continue_receiving);
    if (!continue_receiving) {
      return status;
    }
  }
}

Status Connection::Receive(bool* received) {
  auto status = read_buffer_.PrepareRead();
  if (!status.ok()) {
    return status;
  }

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
  status = socket_.Recv(read_buffer_.write_position(), remaining_buf_capacity, &nread);
  if (!status.ok()) {
    if (Socket::IsTemporarySocketError(status)) {
      *received = false;
      return Status::OK();
    }
    return status;
  }

  read_buffer_.DataAppended(nread);
  *received = nread != 0;
  return Status::OK();
}

Status Connection::TryProcessCalls(bool* continue_receiving) {
  CHECK_NOTNULL(continue_receiving);
  DCHECK(reactor_->IsCurrentThread());

  if (read_buffer_.empty()) {
    *continue_receiving = false;
    return Status::OK();
  }

  size_t consumed = 0;
  const Slice bytes_to_process(read_buffer_.begin(), read_buffer_.size());

  auto result = context_->ProcessCalls(shared_from_this(), bytes_to_process, &consumed);
  if (PREDICT_FALSE(!result.ok())) {
    LOG(WARNING) << ToString() << " command sequence failure: " << result.ToString();
    *continue_receiving = false;
    return result;
  }
  *continue_receiving = true;
  read_buffer_.Consume(consumed);
  return Status::OK();
}

Status Connection::HandleCallResponse(Slice call_data) {
  DCHECK(reactor_->IsCurrentThread());
  gscoped_ptr<CallResponse> resp(new CallResponse);
  RETURN_NOT_OK(resp->ParseFrom(call_data));

  auto awaiting = awaiting_response_.find(resp->call_id());
  if (awaiting == awaiting_response_.end()) {
    LOG(ERROR) << ToString() << ": Got a response for call id " << resp->call_id() << " which "
               << "was not pending! Ignoring.";
    DCHECK(awaiting != awaiting_response_.end());
    return Status::OK();
  }
  auto call = awaiting->second;
  awaiting_response_.erase(awaiting);

  if (PREDICT_FALSE(!call)) {
    // The call already failed due to a timeout.
    VLOG(1) << "Got response to call id " << resp->call_id() << " after client already timed out";
    return Status::OK();
  }

  call->SetResponse(resp.Pass());

  return Status::OK();
}

Status Connection::WriteHandler() {
  DCHECK(reactor_->IsCurrentThread());

  if (sending_.empty()) {
    LOG(WARNING) << ToString() << " got a ready-to-write callback, but there is "
        "nothing to write.";
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
        if (direction_ == Direction::CLIENT) {
          OutboundCallPtr outbound_call = std::static_pointer_cast<OutboundCall>(call);
          CallSent(std::move(outbound_call));
        }
        call->Transferred(Status::OK());
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
    auto expires_at = MonoTime::FineNow() + timeout;
    auto reschedule = expiration_queue_.empty() || expiration_queue_.top().first > expires_at;
    expiration_queue_.emplace(expires_at, call);
    if (reschedule) {
      timer_.set(timeout.ToSeconds(), 0);
      timer_.start();
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

// Reactor task that transitions this Connection from connection negotiation to
// regular RPC handling. Destroys Connection on negotiation error.
class NegotiationCompletedTask : public ReactorTask {
 public:
  NegotiationCompletedTask(ConnectionPtr conn,
                           const Status& negotiation_status)
      : conn_(std::move(conn)),
        negotiation_status_(negotiation_status) {
  }

  virtual void Run(Reactor* reactor) override {
    reactor->CompleteConnectionNegotiation(conn_, negotiation_status_);
  }

  virtual void Abort(const Status& status) override {
    DCHECK(conn_->reactor()->closing());
    VLOG(1) << "Failed connection negotiation due to shut down reactor thread: "
            << status.ToString();
  }

 private:
  ConnectionPtr conn_;
  Status negotiation_status_;
};

// Disable / reset socket timeouts.
Status DisableSocketTimeouts(Connection *conn) {
  RETURN_NOT_OK(conn->socket()->SetSendTimeout(MonoDelta::FromNanoseconds(0L)));
  RETURN_NOT_OK(conn->socket()->SetRecvTimeout(MonoDelta::FromNanoseconds(0L)));
  return Status::OK();
}

void Connection::CompleteNegotiation(Status negotiation_status) {
  if (negotiation_status.ok()) {
    negotiation_status = DisableSocketTimeouts(this);
  }
  auto task = std::make_shared<NegotiationCompletedTask>(shared_from_this(), negotiation_status);
  reactor_->ScheduleReactorTask(task);
}

void Connection::MarkNegotiationComplete() {
  DCHECK(reactor_->IsCurrentThread());
  negotiation_complete_ = true;
}

void Connection::RunNegotiation(const MonoTime& deadline) {
  context_->RunNegotiation(shared_from_this(), deadline);
}

Status Connection::DumpPB(const DumpRunningRpcsRequestPB& req,
                          RpcConnectionPB* resp) {
  DCHECK(reactor_->IsCurrentThread());
  resp->set_remote_ip(yb::ToString(remote_));
  if (negotiation_complete_) {
    resp->set_state(RpcConnectionPB::OPEN);
    resp->set_remote_user_credentials(user_credentials_.ToString());
  } else {
    // It's racy to dump credentials while negotiating, since the Connection
    // object is owned by the negotiation thread at that point.
    resp->set_state(RpcConnectionPB::NEGOTIATING);
  }

  if (direction_ == Direction::CLIENT) {
    for (auto& entry : awaiting_response_) {
      if (entry.second) {
        entry.second->DumpPB(req, resp->add_calls_in_flight());
      }
    }
    for (auto& call : sending_outbound_datas_) {
      if (call) {
        down_cast<OutboundCall*>(call.get())->DumpPB(req, resp->add_calls_in_flight());
      }
    }
  } else if (direction_ != Direction::SERVER) {
    LOG(FATAL) << "Invalid direction: " << util::to_underlying(direction_);
  }
  context_->DumpPB(req, resp);

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
                                                   shutdown_status_));
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
  DCHECK_EQ(direction_, Direction::SERVER);

  if (!shutdown_status_.ok()) {
    outbound_data->Transferred(shutdown_status_);
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

}  // namespace rpc
}  // namespace yb
