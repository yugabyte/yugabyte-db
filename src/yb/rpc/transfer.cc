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

#include "yb/rpc/transfer.h"

#include <iostream>

#include "yb/gutil/endian.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rpc/messenger.h"
#include "yb/util/flag_tags.h"
#include "yb/util/split.h"

DEFINE_int32(rpc_max_message_size, (8 * 1024 * 1024),
             "The maximum size of a message that any RPC that the server will accept.");
TAG_FLAG(rpc_max_message_size, advanced);
TAG_FLAG(rpc_max_message_size, runtime);

METRIC_DEFINE_histogram(
    server, handler_latency_OutboundTransfer, "Time taken to transfer the response ",
    yb::MetricUnit::kMicroseconds, "Microseconds spent to queue and write the response to the wire",
    60000000LU, 2);

namespace yb {
namespace rpc {

using std::ostringstream;
using std::string;
using cqlserver::CQLMessage;

#define RETURN_ON_ERROR_OR_SOCKET_NOT_READY(status) \
  if (PREDICT_FALSE(!status.ok())) {                            \
    if (Socket::IsTemporarySocketError(status.posix_code())) {  \
      return Status::OK(); /* EAGAIN, etc. */                   \
    }                                                           \
    return status;                                              \
  }

TransferCallbacks::~TransferCallbacks() {}

YBInboundTransfer::YBInboundTransfer() {
  buf_.resize(total_length_);
}

string YBInboundTransfer::StatusAsString() const {
  return strings::Substitute("$0/$1 bytes received", cur_offset_, total_length_);
}

RedisInboundTransfer::~RedisInboundTransfer() {
}

Status YBInboundTransfer::ReceiveBuffer(Socket& socket) {
  if (cur_offset_ < kMsgLengthPrefixLength) {
    // receive int32 length prefix
    int32_t rem = kMsgLengthPrefixLength - cur_offset_;
    int32_t nread;
    Status status = socket.Recv(&buf_[cur_offset_], rem, &nread);
    RETURN_ON_ERROR_OR_SOCKET_NOT_READY(status);
    if (nread == 0) {
      return Status::OK();
    }
    DCHECK_GE(nread, 0);
    cur_offset_ += nread;
    if (cur_offset_ < kMsgLengthPrefixLength) {
      // If we still don't have the full length prefix, we can't continue
      // reading yet.
      return Status::OK();
    }
    // Since we only read 'rem' bytes above, we should now have exactly
    // the length prefix in our buffer and no more.
    DCHECK_EQ(cur_offset_, kMsgLengthPrefixLength);

    // The length prefix doesn't include its own 4 bytes, so we have to
    // add that back in.
    total_length_ = NetworkByteOrder::Load32(&buf_[0]) + kMsgLengthPrefixLength;
    if (total_length_ > FLAGS_rpc_max_message_size) {
      return STATUS(NetworkError, StringPrintf("the frame had a length of %d, but we only support "
                                                   "messages up to %d bytes long.", total_length_,
                                               FLAGS_rpc_max_message_size));
    }
    if (total_length_ <= kMsgLengthPrefixLength) {
      return STATUS(NetworkError, StringPrintf("the frame had a length of %d, which is invalid",
                                               total_length_));
    }
    buf_.resize(total_length_);

    // Fall through to receive the message body, which is likely to be already
    // available on the socket.
  }

  // receive message body
  int32_t nread;
  int32_t rem = total_length_ - cur_offset_;
  Status status = socket.Recv(&buf_[cur_offset_], rem, &nread);
  RETURN_ON_ERROR_OR_SOCKET_NOT_READY(status);
  cur_offset_ += nread;

  return Status::OK();
}

RedisInboundTransfer::RedisInboundTransfer() {
  buf_.resize(kProtoIOBufLen);
  ASAN_POISON_MEMORY_REGION(buf_.data(), kProtoIOBufLen);
}

string RedisInboundTransfer::StatusAsString() const {
  return strings::Substitute("$0 : $1 bytes received", this, cur_offset_);
}

bool RedisInboundTransfer::FindEndOfLine() {
  searching_pos_ = max(searching_pos_, parsing_pos_);
  const char* newline = static_cast<const char*>(memchr(buf_.c_str() + searching_pos_, '\n',
                                                        cur_offset_ - searching_pos_));

  // Nothing to do without a \r\n.
  if (newline == nullptr) {
    // Update searching_pos_ to cur_offset_ so that we don't search the searched bytes again.
    searching_pos_ = cur_offset_;
    return false;
  }

  return true;
}

Status RedisInboundTransfer::ParseNumber(int64_t* parse_result) {
  // NOLINTNEXTLINE
  static_assert(sizeof(long long) == sizeof(int64_t),
                "Expecting long long to be a 64-bit integer");
  char* end_ptr = nullptr;
  *parse_result = std::strtoll(buf_.c_str() + parsing_pos_, &end_ptr, 0);
  // If the length is well-formed, it should extend all the way until newline.
  SCHECK_EQ(
      '\r', end_ptr[0], Corruption, "Redis protocol error: expecting a number followed by newline");
  SCHECK_EQ(
      '\n', end_ptr[1], Corruption, "Redis protocol error: expecting a number followed by newline");
  parsing_pos_ = (end_ptr - buf_.c_str()) + 2;
  return Status::OK();
}

Status RedisInboundTransfer::CheckInlineBuffer() {
  if (done_) return Status::OK();

  if (!FindEndOfLine()) {
    return Status::OK();
  }

  client_command_.cmd_args.clear();
  const char* newline = static_cast<const char*>(memchr(buf_.c_str() + searching_pos_, '\r',
                                                        cur_offset_ - searching_pos_));
  const size_t query_len = newline - (buf_.c_str() + parsing_pos_);
  // Split the input buffer up to the \r\n.
  Slice aux(&buf_[parsing_pos_], query_len);
  // TODO: fail gracefully without killing the server.
  RETURN_NOT_OK(util::SplitArgs(aux, &client_command_.cmd_args));
  parsing_pos_ = query_len + 2;
  done_ = true;
  return Status::OK();
}

Status RedisInboundTransfer::CheckMultiBulkBuffer() {
  if (done_) return Status::OK();

  if (client_command_.num_multi_bulk_args_left == 0) {
    // Multi bulk length cannot be read without a \r\n.
    parsing_pos_ = 0;
    client_command_.cmd_args.clear();
    client_command_.current_multi_bulk_arg_len = -1;

    DVLOG(4) << "Looking at : "
             << Slice(buf_.c_str() + parsing_pos_, cur_offset_ - parsing_pos_).ToDebugString(8);
    if (!FindEndOfLine()) return Status::OK();

    SCHECK_EQ(
        '*', buf_[parsing_pos_], Corruption,
        StringPrintf("Expected to see '*' instead of %c", buf_[parsing_pos_]));
    parsing_pos_++;
    int64_t num_args = 0;
    RETURN_NOT_OK(ParseNumber(&num_args));
    SCHECK_GT(
        num_args, 0, Corruption,
        StringPrintf(
            "Number of lines in multibulk out of expected range (0, 1024 * 1024] : %lld",
            num_args));
    SCHECK_LE(
        num_args, 1024 * 1024, Corruption,
        StringPrintf(
            "Number of lines in multibulk out of expected range (0, 1024 * 1024] : %lld",
            num_args));
    client_command_.num_multi_bulk_args_left = num_args;
  }

  while (client_command_.num_multi_bulk_args_left > 0) {
    if (client_command_.current_multi_bulk_arg_len == -1) {  // Read bulk length if unknown.
      if (!FindEndOfLine()) return Status::OK();

      SCHECK_EQ(
          buf_[parsing_pos_], '$', Corruption,
          StringPrintf("Protocol error: expected '$', got '%c'", buf_[parsing_pos_]));
      parsing_pos_++;
      int64_t parsed_len = 0;
      RETURN_NOT_OK(ParseNumber(&parsed_len));
      SCHECK_GE(
          parsed_len, 0, Corruption,
          StringPrintf(
              "Protocol error: invalid bulk length not in the range [0, 512 * 1024 * 1024] : %lld",
              parsed_len));
      SCHECK_LE(
          parsed_len, 512 * 1024 * 1024, Corruption,
          StringPrintf(
              "Protocol error: invalid bulk length not in the range [0, 512 * 1024 * 1024] : %lld",
              parsed_len));
      client_command_.current_multi_bulk_arg_len = parsed_len;
    }

    // Read bulk argument.
    if (cur_offset_ < parsing_pos_ + client_command_.current_multi_bulk_arg_len + 2) {
      // Not enough data (+2 == trailing \r\n).
      return Status::OK();
    }

    client_command_.cmd_args.push_back(
        Slice(buf_.data() + parsing_pos_, client_command_.current_multi_bulk_arg_len));
    parsing_pos_ += client_command_.current_multi_bulk_arg_len + 2;
    client_command_.num_multi_bulk_args_left--;
    client_command_.current_multi_bulk_arg_len = -1;
  }

  // We're done consuming the client's command when num_multi_bulk_args_left == 0.
  done_ = true;
  return Status::OK();
}

RedisInboundTransfer* RedisInboundTransfer::ExcessData() const {
  CHECK_GE(cur_offset_, parsing_pos_) << "Parsing position cannot be past current offset.";
  if (cur_offset_ == parsing_pos_) return nullptr;

  // Copy excess data from buf_. Starting at pos_ up to cur_offset_.
  const int excess_bytes_len = cur_offset_ - parsing_pos_;
  RedisInboundTransfer *excess = new RedisInboundTransfer();
  // Right now, all the buffers are created with the same size. When we handle large sized
  // requests in RedisInboundTransfer, make sure that we have a large enough buffer.
  assert(excess->buf_.size() > excess_bytes_len);
  ASAN_UNPOISON_MEMORY_REGION(excess->buf_.data(), excess_bytes_len);
  memcpy(static_cast<void *>(excess->buf_.data()),
         static_cast<const void *>(buf_.data() + parsing_pos_),
         excess_bytes_len);
  excess->cur_offset_ = excess_bytes_len;
  excess->CheckReadCompletely();

  return excess;
}

Status RedisInboundTransfer::CheckReadCompletely() {
  /* Determine request type when unknown. */
  if (buf_[0] == '*') {
    RETURN_NOT_OK(CheckMultiBulkBuffer());
  } else {
    RETURN_NOT_OK(CheckInlineBuffer());
  }
  return Status::OK();
}

Status RedisInboundTransfer::ReceiveBuffer(Socket& socket) {
  // Try to read into the buffer whatever is available.
  const int32_t buf_space_left = kProtoIOBufLen - cur_offset_;
  int32_t bytes_read = 0;

  ASAN_UNPOISON_MEMORY_REGION(&buf_[cur_offset_], buf_space_left);
  Status status = socket.Recv(&buf_[cur_offset_], buf_space_left, &bytes_read);
  DCHECK_GE(bytes_read, 0);
  ASAN_POISON_MEMORY_REGION(&buf_[cur_offset_] + bytes_read, buf_space_left - bytes_read);

  RETURN_ON_ERROR_OR_SOCKET_NOT_READY(status);
  if (bytes_read == 0) {
    return Status::OK();
  }
  cur_offset_ += bytes_read;

  // Check if we have read the whole command.
  RETURN_NOT_OK(CheckReadCompletely());
  return Status::OK();
}

CQLInboundTransfer::CQLInboundTransfer() {
  buf_.resize(total_length_);
}

Status CQLInboundTransfer::ReceiveBuffer(Socket& socket) {

  if (cur_offset_ < CQLMessage::kMessageHeaderLength) {
    // receive the fixed header
    const int32_t rem = CQLMessage::kMessageHeaderLength - cur_offset_;
    int32_t nread = 0;
    const Status status = socket.Recv(&buf_[cur_offset_], rem, &nread);
    RETURN_ON_ERROR_OR_SOCKET_NOT_READY(status);
    if (nread == 0) {
      return Status::OK();
    }
    DCHECK_GE(nread, 0);
    cur_offset_ += nread;
    if (cur_offset_ < CQLMessage::kMessageHeaderLength) {
      // If we still don't have the full header, we can't continue reading yet.
      return Status::OK();
    }
    // Since we only read 'rem' bytes above, we should now have exactly the header in our buffer
    // and no more.
    DCHECK_EQ(cur_offset_, CQLMessage::kMessageHeaderLength);

    // Extract the body length field in buf_[5..8] and update the total length of the frame.
    total_length_ = CQLMessage::kMessageHeaderLength +
        NetworkByteOrder::Load32(&buf_[CQLMessage::kHeaderPosLength]);
    if (total_length_ > CQLMessage::kMaxMessageLength) {
      return STATUS(NetworkError, StringPrintf("the frame had a length of %d, but we only support "
          "messages up to %d bytes long.", total_length_, CQLMessage::kMaxMessageLength));
    }
    if (total_length_ < CQLMessage::kMessageHeaderLength) {
      // total_length_ can become less than kMessageHeaderLength if arithmetic overflow occurs.
      return STATUS(NetworkError, StringPrintf("the frame had a length of %d, which is invalid",
                                               total_length_));
    }
    buf_.resize(total_length_);

    // Fall through to receive the message body, which is likely to be already available on the
    // socket.
  }

  // receive message body
  int32_t nread = 0;
  const int32_t rem = total_length_ - cur_offset_;
  if (rem > 0) {
    Status status = socket.Recv(&buf_[cur_offset_], rem, &nread);
    RETURN_ON_ERROR_OR_SOCKET_NOT_READY(status);
    cur_offset_ += nread;
  }
  LOG(INFO) << "CQLInboundTransfer::ReceiveBuffer: " << cur_offset_ << " bytes read";

  return Status::OK();
}

string CQLInboundTransfer::StatusAsString() const {
  return strings::Substitute("$0: $1 bytes received", this, cur_offset_);
}

scoped_refptr<Histogram> OutboundTransfer::rpc_metric_ = nullptr;

OutboundTransfer::OutboundTransfer(const std::vector<Slice>& payload, TransferCallbacks* callbacks)
    : cur_slice_idx_(0),
      cur_offset_in_slice_(0),
      callbacks_(callbacks),
      aborted_(false),
      start_(MonoTime::Now(MonoTime::FINE)) {
  CHECK(!payload.empty());

  n_payload_slices_ = payload.size();
  CHECK_LE(n_payload_slices_, arraysize(payload_slices_));
  for (int i = 0; i < payload.size(); i++) {
    payload_slices_[i] = payload[i];
  }
}

void OutboundTransfer::InitializeMetric(const scoped_refptr<MetricEntity>& entity) {
  rpc_metric_ = METRIC_handler_latency_OutboundTransfer.Instantiate(entity);
}

OutboundTransfer::~OutboundTransfer() {
  if (rpc_metric_) {
    auto end_time = MonoTime::Now(MonoTime::FINE);
    rpc_metric_->Increment(end_time.GetDeltaSince(start_).ToMicroseconds());
  }

  if (!TransferFinished() && !aborted_) {
    callbacks_->NotifyTransferAborted(
        STATUS(RuntimeError, "RPC transfer destroyed before it finished sending"));
  }
}

void OutboundTransfer::Abort(const Status& status) {
  CHECK(!aborted_) << "Already aborted";
  CHECK(!TransferFinished()) << "Cannot abort a finished transfer";
  callbacks_->NotifyTransferAborted(status);
  aborted_ = true;
}

Status OutboundTransfer::SendBuffer(Socket& socket) {
  CHECK_LT(cur_slice_idx_, n_payload_slices_);

  int n_iovecs = n_payload_slices_ - cur_slice_idx_;
  struct iovec iovec[n_iovecs];
  {
    int offset_in_slice = cur_offset_in_slice_;
    for (int i = 0; i < n_iovecs; i++) {
      Slice& slice = payload_slices_[cur_slice_idx_ + i];
      iovec[i].iov_base = slice.mutable_data() + offset_in_slice;
      iovec[i].iov_len = slice.size() - offset_in_slice;

      offset_in_slice = 0;
    }
  }

  int32_t written;
  Status status = socket.Writev(iovec, n_iovecs, &written);
  RETURN_ON_ERROR_OR_SOCKET_NOT_READY(status);

  // Adjust our accounting of current writer position.
  for (int i = cur_slice_idx_; i < n_payload_slices_; i++) {
    Slice& slice = payload_slices_[i];
    int rem_in_slice = slice.size() - cur_offset_in_slice_;
    DCHECK_GE(rem_in_slice, 0);

    if (written >= rem_in_slice) {
      // Used up this entire slice, advance to the next slice.
      cur_slice_idx_++;
      cur_offset_in_slice_ = 0;
      written -= rem_in_slice;
    } else {
      // Partially used up this slice, just advance the offset within it.
      cur_offset_in_slice_ += written;
      break;
    }
  }

  if (cur_slice_idx_ == n_payload_slices_) {
    callbacks_->NotifyTransferFinished();
    DCHECK_EQ(0, cur_offset_in_slice_);
  } else {
    DCHECK_LT(cur_slice_idx_, n_payload_slices_);
    DCHECK_LT(cur_offset_in_slice_, payload_slices_[cur_slice_idx_].size());
  }

  return Status::OK();
}

string OutboundTransfer::HexDump() const {
  string ret;
  for (int i = 0; i < n_payload_slices_; i++) {
    ret.append(payload_slices_[i].ToDebugString());
  }
  return ret;
}

int32_t OutboundTransfer::TotalLength() const {
  int32_t ret = 0;
  for (int i = 0; i < n_payload_slices_; i++) {
    ret += payload_slices_[i].size();
  }
  return ret;
}

}  // namespace rpc
}  // namespace yb
