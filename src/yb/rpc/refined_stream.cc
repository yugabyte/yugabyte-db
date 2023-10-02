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
#include "yb/util/result.h"
#include "yb/util/status_format.h"

namespace yb {
namespace rpc {

RefinedStream::RefinedStream(
    std::unique_ptr<Stream> lower_stream, std::unique_ptr<StreamRefiner> refiner,
    size_t receive_buffer_size, const MemTrackerPtr& buffer_tracker)
    : lower_stream_(std::move(lower_stream)), refiner_(std::move(refiner)),
      read_buffer_(receive_buffer_size, buffer_tracker) {
  VLOG_WITH_PREFIX(4) << "Receive buffer size: " << receive_buffer_size;
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

  pending_data_.clear();
  lower_stream_->Shutdown(status);
}

Result<size_t> RefinedStream::Send(OutboundDataPtr data) {
  switch (state_) {
    case RefinedStreamState::kInitial:
    case RefinedStreamState::kHandshake:
      pending_data_.push_back(std::move(data));
      return kUnknownCallHandle;
    case RefinedStreamState::kEnabled:
      RETURN_NOT_OK(refiner_->Send(std::move(data)));
      return kUnknownCallHandle;
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

bool RefinedStream::Cancelled(size_t handle) {
  if (state_ == RefinedStreamState::kDisabled) {
    return lower_stream_->Cancelled(handle);
  }
  LOG_WITH_PREFIX(DFATAL) << "Cancel is not supported for proxy stream: " << handle;
  return false;
}

bool RefinedStream::IsConnected() {
  return state_ == RefinedStreamState::kEnabled || state_ == RefinedStreamState::kDisabled;
}

const Protocol* RefinedStream::GetProtocol() {
  return refiner_->GetProtocol();
}

StreamReadBuffer& RefinedStream::ReadBuffer() {
  return state_ != RefinedStreamState::kDisabled ? read_buffer_ : context_->ReadBuffer();
}

Result<size_t> RefinedStream::ProcessReceived(ReadBufferFull read_buffer_full) {
  switch (state_) {
    case RefinedStreamState::kInitial: {
      RETURN_NOT_OK(refiner_->ProcessHeader());
      if (state_ == RefinedStreamState::kInitial) {
        // Received data was not enough to check stream header.
        return 0;
      }
      return ProcessReceived(read_buffer_full);
    }

    case RefinedStreamState::kDisabled:
      return context_->ProcessReceived(read_buffer_full);

    case RefinedStreamState::kHandshake:
      return Handshake();

    case RefinedStreamState::kEnabled: {
      return Read();
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

Status TransferData(StreamReadBuffer* source, StreamReadBuffer* dest) {
  auto dst = VERIFY_RESULT(dest->PrepareAppend());
  auto dst_it = dst.begin();
  size_t total_len = 0;
  for (auto src_vec : source->AppendedVecs()) {
    while (src_vec.iov_len != 0) {
      if (dst_it->iov_len == 0) {
        if (++dst_it == dst.end()) {
          return STATUS(RuntimeError, "No enough space in destination buffer");
        }
      }
      size_t len = std::min(dst_it->iov_len, src_vec.iov_len);
      memcpy(dst_it->iov_base, src_vec.iov_base, len);
      IoVecRemovePrefix(len, &*dst_it);
      IoVecRemovePrefix(len, &src_vec);
      total_len += len;
    }
  }
  source->Consume(total_len, Slice());
  dest->DataAppended(total_len);
  return Status::OK();
}

Status RefinedStream::Established(RefinedStreamState state) {
  state_ = state;

  if (state == RefinedStreamState::kDisabled) {
    RETURN_NOT_OK(TransferData(&read_buffer_, &context_->ReadBuffer()));
  }

  ResetLogPrefix();

  VLOG_WITH_PREFIX(1) << __func__ << ": " << state;

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

Result<size_t> RefinedStream::Handshake() {
  auto handshake_status = refiner_->Handshake();
  LOG_IF_WITH_PREFIX(INFO, !handshake_status.ok()) << "Handshake failed: " << handshake_status;
  RETURN_NOT_OK(handshake_status);

  if (state_ == RefinedStreamState::kEnabled) {
    return Read();
  }

  return 0;
}

// Used to read bytes to global skip buffer.
class SkipStreamReadBuffer : public StreamReadBuffer {
 public:
  explicit SkipStreamReadBuffer(const Slice& out, size_t skip_len)
      : out_(out), wpos_(out_.mutable_data()), skip_len_(skip_len) {}

  size_t DataAvailable() override { return 0; }

  bool ReadyToRead() override { return false; }

  bool Empty() override { return true; }

  void Reset() override {
    wpos_ = out_.mutable_data();
  }

  bool Full() override {
    return wpos_ == out_.end();
  }

  Result<IoVecs> PrepareAppend() override {
    return IoVecs({iovec{
      .iov_base = wpos_,
      .iov_len = std::min(static_cast<size_t>(out_.end() - wpos_), skip_len_),
    }});
  }

  void DataAppended(size_t len) override {
    skip_len_ -= len;
    wpos_ += len;
  }

  IoVecs AppendedVecs() override {
    return IoVecs({iovec{
      .iov_base = out_.mutable_data(),
      .iov_len = static_cast<size_t>(wpos_ - out_.mutable_data()),
    }});
  }

  void Consume(size_t count, const Slice& prepend) override {
  }

  std::string ToString() const override {
    return "SkipStreamReadBuffer";
  }

 private:
  Slice out_;
  uint8_t* wpos_;
  size_t skip_len_;
};

Result<size_t> RefinedStream::Read() {
  for (;;) {
    auto& out_buffer = context_->ReadBuffer();
    auto data_available_before_reading = read_buffer_.DataAvailable();

    if (upper_stream_bytes_to_skip_ > 0) {
      auto global_skip_buffer = GetGlobalSkipBuffer();
      do {
        SkipStreamReadBuffer buffer(global_skip_buffer, upper_stream_bytes_to_skip_);
        RETURN_NOT_OK(refiner_->Read(&buffer));
        size_t len = buffer.AppendedVecs()[0].iov_len;
        if (!len) {
          break;
        }
        VLOG_WITH_PREFIX(4) << "Skip upper: " << len << " of " << upper_stream_bytes_to_skip_;
        upper_stream_bytes_to_skip_ -= len;
      } while (upper_stream_bytes_to_skip_ > 0);
    }

    if (upper_stream_bytes_to_skip_ == 0) {
      auto read_buffer_full = VERIFY_RESULT(refiner_->Read(&out_buffer));
      if (out_buffer.ReadyToRead()) {
        auto temp = VERIFY_RESULT(context_->ProcessReceived(read_buffer_full));
        upper_stream_bytes_to_skip_ = temp;
        VLOG_IF_WITH_PREFIX(3, temp != 0) << "Skip: " << upper_stream_bytes_to_skip_;
      }
      // If read buffer was full, then it could happen that refiner already has more data,
      // so need to retry read.
      if (read_buffer_full) {
        continue;
      }
    }

    if (read_buffer_.Empty() || read_buffer_.DataAvailable() == data_available_before_reading) {
      break;
    }
  }

  return 0;
}

RefinedStreamFactory::RefinedStreamFactory(
    StreamFactoryPtr lower_layer_factory, const MemTrackerPtr& buffer_tracker,
    RefinerFactory refiner_factory)
    : lower_layer_factory_(std::move(lower_layer_factory)), buffer_tracker_(buffer_tracker),
      refiner_factory_(std::move(refiner_factory)) {
}

std::unique_ptr<Stream> RefinedStreamFactory::Create(const StreamCreateData& data) {
  return std::make_unique<RefinedStream>(
      lower_layer_factory_->Create(data), refiner_factory_(data), data.receive_buffer_size,
      buffer_tracker_);
}

}  // namespace rpc
}  // namespace yb
