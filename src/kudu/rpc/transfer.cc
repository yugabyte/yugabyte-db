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

#include "kudu/rpc/transfer.h"

#include <stdint.h>

#include <iostream>
#include <sstream>

#include <glog/logging.h>

#include "kudu/gutil/endian.h"
#include "kudu/gutil/stringprintf.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/rpc/constants.h"
#include "kudu/rpc/messenger.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/net/sockaddr.h"
#include "kudu/util/net/socket.h"

DEFINE_int32(rpc_max_message_size, (8 * 1024 * 1024),
             "The maximum size of a message that any RPC that the server will accept.");
TAG_FLAG(rpc_max_message_size, advanced);
TAG_FLAG(rpc_max_message_size, runtime);

namespace kudu {
namespace rpc {

using std::ostringstream;
using std::string;

#define RETURN_ON_ERROR_OR_SOCKET_NOT_READY(status) \
  if (PREDICT_FALSE(!status.ok())) {                            \
    if (Socket::IsTemporarySocketError(status.posix_code())) {  \
      return Status::OK(); /* EAGAIN, etc. */                   \
    }                                                           \
    return status;                                              \
  }

TransferCallbacks::~TransferCallbacks()
{}

InboundTransfer::InboundTransfer()
  : total_length_(kMsgLengthPrefixLength),
    cur_offset_(0) {
  buf_.resize(kMsgLengthPrefixLength);
}

Status InboundTransfer::ReceiveBuffer(Socket &socket) {
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
      return Status::NetworkError(StringPrintf("the frame had a "
               "length of %d, but we only support messages up to %d bytes "
               "long.", total_length_, FLAGS_rpc_max_message_size));
    }
    if (total_length_ <= kMsgLengthPrefixLength) {
      return Status::NetworkError(StringPrintf("the frame had a "
               "length of %d, which is invalid", total_length_));
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

bool InboundTransfer::TransferStarted() const {
  return cur_offset_ != 0;
}

bool InboundTransfer::TransferFinished() const {
  return cur_offset_ == total_length_;
}

string InboundTransfer::StatusAsString() const {
  return strings::Substitute("$0/$1 bytes received", cur_offset_, total_length_);
}

OutboundTransfer::OutboundTransfer(const std::vector<Slice> &payload,
                                   TransferCallbacks *callbacks)
  : cur_slice_idx_(0),
    cur_offset_in_slice_(0),
    callbacks_(callbacks),
    aborted_(false) {
  CHECK(!payload.empty());

  n_payload_slices_ = payload.size();
  CHECK_LE(n_payload_slices_, arraysize(payload_slices_));
  for (int i = 0; i < payload.size(); i++) {
    payload_slices_[i] = payload[i];
  }
}

OutboundTransfer::~OutboundTransfer() {
  if (!TransferFinished() && !aborted_) {
    callbacks_->NotifyTransferAborted(
      Status::RuntimeError("RPC transfer destroyed before it finished sending"));
  }
}

void OutboundTransfer::Abort(const Status &status) {
  CHECK(!aborted_) << "Already aborted";
  CHECK(!TransferFinished()) << "Cannot abort a finished transfer";
  callbacks_->NotifyTransferAborted(status);
  aborted_ = true;
}

Status OutboundTransfer::SendBuffer(Socket &socket) {
  CHECK_LT(cur_slice_idx_, n_payload_slices_);

  int n_iovecs = n_payload_slices_ - cur_slice_idx_;
  struct iovec iovec[n_iovecs];
  {
    int offset_in_slice = cur_offset_in_slice_;
    for (int i = 0; i < n_iovecs; i++) {
      Slice &slice = payload_slices_[cur_slice_idx_ + i];
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
    Slice &slice = payload_slices_[i];
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

bool OutboundTransfer::TransferStarted() const {
  return cur_offset_in_slice_ != 0 || cur_slice_idx_ != 0;
}

bool OutboundTransfer::TransferFinished() const {
  if (cur_slice_idx_ == n_payload_slices_) {
    DCHECK_EQ(0, cur_offset_in_slice_); // sanity check
    return true;
  }
  return false;
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

} // namespace rpc
} // namespace kudu
