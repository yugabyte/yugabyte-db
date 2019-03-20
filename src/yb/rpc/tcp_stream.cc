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

#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/string_util.h"

using namespace std::literals;

DECLARE_uint64(rpc_connection_timeout_ms);
DEFINE_test_flag(int32, TEST_delay_connect_ms, 0,
                 "Delay connect in tests for specified amount of milliseconds.");

namespace yb {
namespace rpc {

namespace {

const size_t kMaxIov = 16;

}

TcpStream::TcpStream(const StreamCreateData& data)
    : socket_(std::move(*data.socket)),
      remote_(data.remote),
      read_buffer_(data.allocator, data.limit) {
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

  if (connect && FLAGS_TEST_delay_connect_ms) {
    connect_delayer_.set(*loop);
    connect_delayer_.set<TcpStream, &TcpStream::DelayConnectHandler>(this);
    connect_delayer_.start(
        static_cast<double>(FLAGS_TEST_delay_connect_ms) / MonoTime::kMillisecondsPerSecond, 0);
    return Status::OK();
  }

  return DoStart(loop, connect);
}

Status TcpStream::DoStart(ev::loop_ref* loop, bool connect) {
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

void TcpStream::DelayConnectHandler(ev::timer& watcher, int revents) { // NOLINT
  if (EV_ERROR & revents) {
    LOG_WITH_PREFIX(WARNING) << "Got an error in handle delay connect";
    return;
  }

  auto status = DoStart(&watcher.loop, true /* connect */);
  if (!status.ok()) {
    Shutdown(status);
  }
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

  read_buffer_.Reset();

  WARN_NOT_OK(socket_.Close(), "Error closing socket");
}

Status TcpStream::TryWrite() {
  auto result = DoWrite();
  if (result.ok()) {
    UpdateEvents();
  }
  return result;
}

int TcpStream::FillIov(iovec* out) {
  int index = 0;
  size_t offset = send_position_;
  for (auto& data : sending_) {
    if (data.skipped || (offset == 0 && data.data && data.data->IsFinished())) {
      data.skipped = true;
      continue;
    }
    for (const auto& bytes : data.bytes) {
      if (offset >= bytes.size()) {
        offset -= bytes.size();
        continue;
      }

      out[index].iov_base = bytes.data() + offset;
      out[index].iov_len = bytes.size() - offset;
      offset = 0;
      if (++index == kMaxIov) {
        return index;
      }
    }
  }

  return index;
}

Status TcpStream::DoWrite() {
  if (!connected_ || waiting_write_ready_ || !is_epoll_registered_) {
    return Status::OK();
  }

  // If we weren't waiting write to be ready, we could try to write data to socket.
  while (!sending_.empty()) {
    iovec iov[kMaxIov];
    int iov_len = FillIov(iov);

    context_->UpdateLastActivity();

    int32_t written = 0;
    auto status = iov_len != 0 ? socket_.Writev(iov, iov_len, &written) : Status::OK();
    DVLOG_WITH_PREFIX(4) << "Queued writes " << queued_bytes_to_send_ << " bytes. written "
                         << written << " . Status " << status << " sending_ .size() "
                         << sending_.size();

    if (PREDICT_FALSE(!status.ok())) {
      if (!Socket::IsTemporarySocketError(status)) {
        YB_LOG_WITH_PREFIX_EVERY_N(WARNING, 50) << "Send failed: " << status;
        return status;
      } else {
        return Status::OK();
      }
    }

    send_position_ += written;
    while (!sending_.empty()) {
      auto& front = sending_.front();
      size_t full_size = front.bytes_size();
      if (front.skipped) {
        queued_bytes_to_send_ -= full_size;
        sending_.pop_front();
        continue;
      }
      if (send_position_ < full_size) {
        break;
      }
      auto data = front.data;
      send_position_ -= full_size;
      queued_bytes_to_send_ -= full_size;
      sending_.pop_front();
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
  auto iov = read_buffer_.valid() ? read_buffer_.PrepareAppend()
                                  : STATUS(IllegalState, "Read buffer was reset");
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
  for (auto& data : sending_) {
    if (data.data) {
      context_->Transferred(data.data, status);
    }
  }
  sending_.clear();
  queued_bytes_to_send_ = 0;
}

void TcpStream::Send(OutboundDataPtr data) {
  // Serialize the actual bytes to be put on the wire.
  sending_.emplace_back(std::move(data));
  queued_bytes_to_send_ += sending_.back().bytes_size();
  DVLOG_WITH_PREFIX(3) << "Added data queued_bytes_to_send_: " << queued_bytes_to_send_;
}

void TcpStream::DumpPB(const DumpRunningRpcsRequestPB& req, RpcConnectionPB* resp) {
  auto call_in_flight = resp->add_calls_in_flight();
  for (auto& entry : sending_) {
    if (entry.data && !entry.data->IsFinished() && entry.data->DumpPB(req, call_in_flight)) {
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
    std::unique_ptr<Stream> Create(const StreamCreateData& data) override {
      return std::make_unique<TcpStream>(data);
    }
  };

  return std::make_shared<TcpStreamFactory>();
}

TcpStream::SendingData::SendingData(OutboundDataPtr data_) : data(std::move(data_)) {
  data->Serialize(&bytes);
}


} // namespace rpc
} // namespace yb
