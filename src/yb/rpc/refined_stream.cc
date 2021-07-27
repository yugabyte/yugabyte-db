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

#include "yb/rpc/refined_stream.h"

#include "yb/rpc/rpc_util.h"

#include "yb/util/logging.h"
#include "yb/util/size_literals.h"

namespace yb {
namespace rpc {

RefinedStream::RefinedStream(
    std::unique_ptr<Stream> lower_stream, std::unique_ptr<StreamRefiner> refiner,
    size_t receive_buffer_size, const MemTrackerPtr& buffer_tracker)
    : lower_stream_(std::move(lower_stream)), refiner_(std::move(refiner)),
      read_buffer_(receive_buffer_size, buffer_tracker) {
}

size_t RefinedStream::GetPendingWriteBytes() {
  return lower_stream_->GetPendingWriteBytes();
}

void RefinedStream::Close() {
  lower_stream_->Close();
}

Status RefinedStream::TryWrite() {
  return lower_stream_->TryWrite();
}

void RefinedStream::ParseReceived() {
  lower_stream_->ParseReceived();
}

bool RefinedStream::Idle(std::string* reason) {
  return lower_stream_->Idle(reason);
}

void RefinedStream::DumpPB(const DumpRunningRpcsRequestPB& req, RpcConnectionPB* resp) {
  lower_stream_->DumpPB(req, resp);
}

const Endpoint& RefinedStream::Remote() const {
  return lower_stream_->Remote();
}

const Endpoint& RefinedStream::Local() const {
  return lower_stream_->Local();
}

Status RefinedStream::Start(bool connect, ev::loop_ref* loop, StreamContext* context) {
  local_side_ = connect ? LocalSide::kClient : LocalSide::kServer;
  context_ = context;
  refiner_->Start(this);
  return lower_stream_->Start(connect, loop, this);
}

void RefinedStream::Shutdown(const Status& status) {
  VLOG_WITH_PREFIX(1) << "Shutdown with status: " << status;

  for (auto& data : pending_data_) {
    if (data) {
      context().Transferred(data, status);
    }
  }

  pending_data_.clear();
  lower_stream_->Shutdown(status);
}

Result<size_t> RefinedStream::Send(OutboundDataPtr data) {
  switch (state_) {
  case RefinedStreamState::kInitial:
  case RefinedStreamState::kHandshake:
    pending_data_.push_back(std::move(data));
    return std::numeric_limits<size_t>::max();
  case RefinedStreamState::kEnabled:
    RETURN_NOT_OK(refiner_->Send(std::move(data)));
    return std::numeric_limits<size_t>::max();
  case RefinedStreamState::kDisabled:
    return lower_stream_->Send(std::move(data));
  }

  FATAL_INVALID_ENUM_VALUE(RefinedStreamState, state_);
}

void RefinedStream::UpdateLastActivity() {
  context_->UpdateLastActivity();
}

void RefinedStream::UpdateLastRead() {
  context_->UpdateLastRead();
}

void RefinedStream::UpdateLastWrite() {
  context_->UpdateLastWrite();
}

void RefinedStream::Transferred(const OutboundDataPtr& data, const Status& status) {
  context_->Transferred(data, status);
}

void RefinedStream::Destroy(const Status& status) {
  context_->Destroy(status);
}

std::string RefinedStream::ToString() const {
  return Format("$0[$1] $2 $3",
                *refiner_, local_side_ == LocalSide::kClient ? "C" : "S", state_, *lower_stream_);
}

void RefinedStream::Cancelled(size_t handle) {
  LOG_WITH_PREFIX(DFATAL) << "Cancel is not supported for proxy stream: " << handle;
}

bool RefinedStream::IsConnected() {
  return state_ == RefinedStreamState::kEnabled || state_ == RefinedStreamState::kDisabled;
}

const Protocol* RefinedStream::GetProtocol() {
  return refiner_->GetProtocol();
}

StreamReadBuffer& RefinedStream::ReadBuffer() {
  return read_buffer_;
}

Result<ProcessDataResult> RefinedStream::ProcessReceived(
    const IoVecs& data, ReadBufferFull read_buffer_full) {
  switch (state_) {
    case RefinedStreamState::kInitial: {
      IoVecs data_copy = data;
      auto consumed = VERIFY_RESULT(refiner_->ProcessHeader(data_copy));
      if (state_ == RefinedStreamState::kInitial) {
        // Received data was not enough to check stream header.
        RSTATUS_DCHECK_EQ(consumed, 0, InternalError,
                          "Consumed data while keeping stream in initial state");
        return ProcessDataResult{0, Slice()};
      }
      data_copy[0].iov_len -= consumed;
      data_copy[0].iov_base = static_cast<char*>(data_copy[0].iov_base) + consumed;
      auto result = VERIFY_RESULT(ProcessReceived(data_copy, read_buffer_full));
      result.consumed += consumed;
      return result;
    }

    case RefinedStreamState::kDisabled:
      return context_->ProcessReceived(data, read_buffer_full);

    case RefinedStreamState::kHandshake: FALLTHROUGH_INTENDED;
    case RefinedStreamState::kEnabled: {
      size_t result = 0;
      for (const auto& iov : data) {
        Slice slice(static_cast<char*>(iov.iov_base), iov.iov_len);
        for (;;) {
          auto len = VERIFY_RESULT(refiner_->Receive(slice));
          result += len;
          if (len == slice.size()) {
            break;
          }
          slice.remove_prefix(len);
          RETURN_NOT_OK(HandshakeOrRead());
        }
      }
      RETURN_NOT_OK(HandshakeOrRead());
      return ProcessDataResult{ result, Slice() };
    }
  }

  return STATUS_FORMAT(IllegalState, "Unexpected state: $0", to_underlying(state_));
}

void RefinedStream::Connected() {
  if (local_side_ != LocalSide::kClient) {
    return;
  }

  auto status = StartHandshake();
  if (status.ok()) {
    status = refiner_->Handshake();
  }
  if (!status.ok()) {
    context_->Destroy(status);
  }
}

Status RefinedStream::Established(RefinedStreamState state) {
  state_ = state;
  ResetLogPrefix();
  context().Connected();
  for (auto& data : pending_data_) {
    RETURN_NOT_OK(Send(std::move(data)));
  }
  pending_data_.clear();
  return Status::OK();
}

Status RefinedStream::SendToLower(OutboundDataPtr data) {
  return ResultToStatus(lower_stream_->Send(std::move(data)));
}

Status RefinedStream::StartHandshake() {
  state_ = RefinedStreamState::kHandshake;
  ResetLogPrefix();
  return Status::OK();
}

Status RefinedStream::HandshakeOrRead() {
  if (PREDICT_FALSE(state_ != RefinedStreamState::kEnabled)) {
    auto handshake_status = refiner_->Handshake();
    LOG_IF_WITH_PREFIX(INFO, !handshake_status.ok()) << "Handshake failed: " << handshake_status;
    RETURN_NOT_OK(handshake_status);
  }

  if (state_ == RefinedStreamState::kEnabled) {
    return Read();
  }

  return Status::OK();
}

Status RefinedStream::Read() {
  auto& refined_read_buffer = context_->ReadBuffer();
  bool done = false;
  while (!done) {
    if (lower_stream_bytes_to_skip_ > 0) {
      auto global_skip_buffer = GetGlobalSkipBuffer();
      do {
        auto len = VERIFY_RESULT(refiner_->Read(
            global_skip_buffer.mutable_data(),
            std::min(global_skip_buffer.size(), lower_stream_bytes_to_skip_)));
        if (len == 0) {
          done = true;
          break;
        }
        VLOG_WITH_PREFIX(4) << "Skip lower: " << len;
        lower_stream_bytes_to_skip_ -= len;
      } while (lower_stream_bytes_to_skip_ > 0);
    }
    auto out = VERIFY_RESULT(refined_read_buffer.PrepareAppend());
    size_t appended = 0;
    for (auto iov = out.begin(); iov != out.end();) {
      auto len = VERIFY_RESULT(refiner_->Read(iov->iov_base, iov->iov_len));
      if (len == 0) {
        done = true;
        break;
      }
      VLOG_WITH_PREFIX(4) << "Read lower: " << len;
      appended += len;
      iov->iov_base = static_cast<char*>(iov->iov_base) + len;
      iov->iov_len -= len;
      if (iov->iov_len <= 0) {
        ++iov;
      }
    }
    refined_read_buffer.DataAppended(appended);
    if (refined_read_buffer.ReadyToRead()) {
      auto temp = VERIFY_RESULT(context_->ProcessReceived(
          refined_read_buffer.AppendedVecs(), ReadBufferFull(refined_read_buffer.Full())));
      refined_read_buffer.Consume(temp.consumed, temp.buffer);
      DCHECK_EQ(lower_stream_bytes_to_skip_, 0);
      lower_stream_bytes_to_skip_ = temp.bytes_to_skip;
    }
  }

  return Status::OK();
}

RefinedStreamFactory::RefinedStreamFactory(
    StreamFactoryPtr lower_layer_factory, const MemTrackerPtr& buffer_tracker,
    RefinerFactory refiner_factory)
    : lower_layer_factory_(std::move(lower_layer_factory)), buffer_tracker_(buffer_tracker),
      refiner_factory_(std::move(refiner_factory)) {
}

std::unique_ptr<Stream> RefinedStreamFactory::Create(const StreamCreateData& data) {
  auto receive_buffer_size = data.socket->GetReceiveBufferSize();
  if (!receive_buffer_size.ok()) {
    LOG(WARNING) << "Compressed stream failure: " << receive_buffer_size.status();
    receive_buffer_size = 256_KB;
  }
  auto lower_stream = lower_layer_factory_->Create(data);
  return std::make_unique<RefinedStream>(
      std::move(lower_stream), refiner_factory_(*receive_buffer_size, buffer_tracker_, data),
      *receive_buffer_size, buffer_tracker_);
}

}  // namespace rpc
}  // namespace yb
