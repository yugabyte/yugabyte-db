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

#include "yb/rpc/tcp_stream.h"

#include "yb/rpc/outbound_data.h"

#include "yb/util/logging.h"
#include "yb/util/string_util.h"

using namespace std::literals;

DECLARE_uint64(rpc_connection_timeout_ms);

namespace yb {
namespace rpc {

TcpStream::TcpStream(
    const Endpoint& remote, Socket socket, GrowableBufferAllocator* allocator, size_t limit)
    : socket_(std::move(socket)),
      remote_(remote),
      read_buffer_(allocator, limit) {
}

TcpStream::~TcpStream() {
  // Must clear the outbound_transfers_ list before deleting.
  CHECK(sending_.empty()) << ToString();

  // It's crucial that the stream is Shutdown first -- otherwise
  // our destructor will end up calling io_.stop()
  // from a possibly non-reactor thread context. This can then make all
  // hell break loose with libev.
  CHECK(!is_epoll_registered_) << ToString();
}

Status TcpStream::Start(bool connect, ev::loop_ref* loop, StreamContext* context) {
  context_ = context;
  connected_ = !connect;

  RETURN_NOT_OK(socket_.SetNoDelay(true));
  RETURN_NOT_OK(socket_.SetSendTimeout(FLAGS_rpc_connection_timeout_ms * 1ms));
  RETURN_NOT_OK(socket_.SetRecvTimeout(FLAGS_rpc_connection_timeout_ms * 1ms));

  if (connect) {
    auto status = socket_.Connect(remote_);
    if (!status.ok() && !Socket::IsTemporarySocketError(status)) {
      LOG_WITH_PREFIX(WARNING) << "Connect failed: " << status;
      return status;
    }
  }
  RETURN_NOT_OK(socket_.GetSocketAddress(&local_));
  log_prefix_.clear();

  io_.set(*loop);
  io_.set<TcpStream, &TcpStream::Handler>(this);
  int events = ev::READ | (!connected_ ? ev::WRITE : 0);
  io_.start(socket_.GetFd(), events);

  DVLOG_WITH_PREFIX(4) << "Starting, listen events: " << events << ", fd: " << socket_.GetFd();

  is_epoll_registered_ = true;

  if (connected_) {
    context_->Connected();
  }

  return Status::OK();
}

void TcpStream::Close() {
  if (socket_.GetFd() >= 0) {
    auto status = socket_.Shutdown(true, true);
    LOG_IF(INFO, !status.ok()) << "Failed to shutdown socket: " << status;
  }
}

void TcpStream::Shutdown(const Status& status) {
  ClearSending(status);

  if (!read_buffer_.empty()) {
    LOG_WITH_PREFIX(WARNING) << "Shutting down with pending inbound data ("
                             << read_buffer_ << ", status = " << status << ")";
  }

  io_.stop();
  is_epoll_registered_ = false;
  WARN_NOT_OK(socket_.Close(), "Error closing socket");
}

Status TcpStream::TryWrite() {
  auto result = DoWrite();
  if (result.ok()) {
    UpdateEvents();
  }
  return result;
}

Status TcpStream::DoWrite() {
  if (!connected_ || waiting_write_ready_ || !is_epoll_registered_) {
    return Status::OK();
  }

  // If we weren't waiting write to be ready, we could try to write data to socket.
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

    context_->UpdateLastActivity();
    int32_t written = 0;

    auto status = socket_.Writev(iov, iov_len, &written);
    if (PREDICT_FALSE(!status.ok())) {
      if (!Socket::IsTemporarySocketError(status)) {
        YB_LOG_WITH_PREFIX_EVERY_N(WARNING, 50) << "Send failed: " << status;
        return status;
      } else {
        return Status::OK();
      }
    }

    send_position_ += written;
    while (!sending_.empty() && send_position_ >= sending_.front().size()) {
      auto data = sending_outbound_datas_.front();
      send_position_ -= sending_.front().size();
      sending_.pop_front();
      sending_outbound_datas_.pop_front();
      if (data) {
        context_->Transferred(data, Status::OK());
      }
    }
  }

  return Status::OK();
}

void TcpStream::Handler(ev::io& watcher, int revents) {  // NOLINT
  DVLOG_WITH_PREFIX(3) << "Handler(revents=" << revents << ")";
  auto status = Status::OK();
  if (revents & ev::ERROR) {
    status = STATUS(NetworkError, ToString() + ": Handler encountered an error");
  }

  if (status.ok() && (revents & ev::READ)) {
    status = ReadHandler();
  }

  if (status.ok() && (revents & ev::WRITE)) {
    bool just_connected = !connected_;
    if (just_connected) {
      connected_ = true;
      context_->Connected();
    }
    status = WriteHandler(just_connected);
  }

  if (status.ok()) {
    UpdateEvents();
  } else {
    context_->Destroy(status);
  }
}

void TcpStream::UpdateEvents() {
  int events = 0;
  if (!read_buffer_full_) {
    events |= ev::READ;
  }
  waiting_write_ready_ = !sending_.empty() || !connected_;
  if (waiting_write_ready_) {
    events |= ev::WRITE;
  }
  if (events) {
    io_.set(events);
  }
}

Status TcpStream::ReadHandler() {
  context_->UpdateLastActivity();

  for (;;) {
    auto received = Receive();
    if (PREDICT_FALSE(!received.ok())) {
      if (received.status().error_code() == ESHUTDOWN) {
        VLOG_WITH_PREFIX(1) << "Shut down by remote end.";
      } else {
        YB_LOG_WITH_PREFIX_EVERY_N(INFO, 50) << " Recv failed: " << received;
      }
      return received.status();
    }
    // Exit the loop if we did not receive anything.
    if (!received.get()) {
      return Status::OK();
    }
    // If we were not able to process next call exit loop.
    // If status is ok, it means that we just do not have enough data to process yet.
    auto continue_receiving = TryProcessReceived();
    if (!continue_receiving.ok()) {
      return continue_receiving.status();
    }
    if (!continue_receiving.get()) {
      return Status::OK();
    }
  }
}

Result<bool> TcpStream::Receive() {
  auto iov = read_buffer_.PrepareAppend();
  if (!iov.ok()) {
    if (iov.status().IsBusy()) {
      read_buffer_full_ = true;
      return false;
    }
    return iov.status();
  }
  read_buffer_full_ = false;

  auto nread = socket_.Recvv(iov.get_ptr());
  if (!nread.ok()) {
    if (Socket::IsTemporarySocketError(nread.status())) {
      return false;
    }
    return nread.status();
  }

  read_buffer_.DataAppended(*nread);
  return *nread != 0;
}

void TcpStream::ParseReceived() {
  auto result = TryProcessReceived();
  if (!result.ok()) {
    context_->Destroy(result.status());
    return;
  }
  if (read_buffer_full_) {
    read_buffer_full_ = false;
    UpdateEvents();
  }
}

Result<bool> TcpStream::TryProcessReceived() {
  if (read_buffer_.empty()) {
    return false;
  }

  auto consumed = VERIFY_RESULT(context_->ProcessReceived(
      read_buffer_.AppendedVecs(), ReadBufferFull(read_buffer_.full())));

  read_buffer_.Consume(consumed);
  return true;
}

Status TcpStream::WriteHandler(bool just_connected) {
  waiting_write_ready_ = false;
  if (sending_.empty()) {
    LOG_IF_WITH_PREFIX(WARNING, !just_connected) <<
        "Got a ready-to-write callback, but there is nothing to write.";
    return Status::OK();
  }

  return DoWrite();
}

std::string TcpStream::ToString() const {
  return Format("{ local: $0 remote: $1 }", local_, remote_);
}

const std::string& TcpStream::LogPrefix() const {
  if (log_prefix_.empty()) {
    log_prefix_ = ToString() + ": ";
  }
  return log_prefix_;
}

bool TcpStream::Idle(std::string* reason_not_idle) {
  bool result = true;
  // Check if we're in the middle of receiving something.
  if (!read_buffer_.empty()) {
    if (reason_not_idle) {
      AppendWithSeparator("read buffer not empty", reason_not_idle);
    }
    result = false;
  }

  // Check if we still need to send something.
  if (!sending_.empty()) {
    if (reason_not_idle) {
      AppendWithSeparator("still sending", reason_not_idle);
    }
    result = false;
  }

  return result;
}

void TcpStream::ClearSending(const Status& status) {
  // Clear any outbound transfers.
  for (auto& data : sending_outbound_datas_) {
    if (data) {
      context_->Transferred(data, status);
    }
  }
  sending_outbound_datas_.clear();
  sending_.clear();
}

void TcpStream::Send(OutboundDataPtr data) {
  // Serialize the actual bytes to be put on the wire.
  data->Serialize(&sending_);

  sending_outbound_datas_.resize(sending_.size());
  sending_outbound_datas_.back() = std::move(data);
}

void TcpStream::DumpPB(const DumpRunningRpcsRequestPB& req, RpcConnectionPB* resp) {
  auto call_in_flight = resp->add_calls_in_flight();
  for (auto& data : sending_outbound_datas_) {
    if (data && data->DumpPB(req, call_in_flight)) {
      call_in_flight = resp->add_calls_in_flight();
    }
  }
  resp->mutable_calls_in_flight()->DeleteSubrange(resp->calls_in_flight_size() - 1, 1);
}

const Protocol* TcpStream::StaticProtocol() {
  static Protocol result("tcp");
  return &result;
}

StreamFactoryPtr TcpStream::Factory() {
  class TcpStreamFactory : public StreamFactory {
   private:
    std::unique_ptr<Stream> Create(
        const Endpoint& remote, Socket socket, GrowableBufferAllocator* allocator, size_t limit)
            override {
      return std::make_unique<TcpStream>(remote, std::move(socket), allocator, limit);
    }
  };

  return std::make_shared<TcpStreamFactory>();
}

} // namespace rpc
} // namespace yb
