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

namespace yb {
namespace rpc {

using std::ostringstream;
using std::string;
using strings::Substitute;

TransferCallbacks::~TransferCallbacks() {}

OutboundTransfer::OutboundTransfer(const std::vector<Slice>& payload, TransferCallbacks* callbacks,
    scoped_refptr<Histogram> handler_latency_OutboundTransfer)
    : handler_latency_outbound_transfer_(handler_latency_OutboundTransfer),
      cur_slice_idx_(0),
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

OutboundTransfer::~OutboundTransfer() {
  auto end_time = MonoTime::Now(MonoTime::FINE);
  if (handler_latency_outbound_transfer_) {
    handler_latency_outbound_transfer_->Increment(end_time.GetDeltaSince(start_).ToMicroseconds());
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
