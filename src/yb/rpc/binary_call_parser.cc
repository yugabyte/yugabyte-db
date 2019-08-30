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

#include "yb/rpc/binary_call_parser.h"
#include "yb/rpc/stream.h"

#include "yb/gutil/endian.h"

#include "yb/rpc/connection.h"

namespace yb {
namespace rpc {

BinaryCallParser::BinaryCallParser(
    const MemTrackerPtr& parent_tracker,
    size_t header_size, size_t size_offset, size_t max_message_length, IncludeHeader include_header,
    SkipEmptyMessages skip_empty_messages, BinaryCallParserListener* listener)
    : buffer_(header_size), size_offset_(size_offset),
      max_message_length_(max_message_length), include_header_(include_header),
      skip_empty_messages_(skip_empty_messages), listener_(listener) {
  buffer_tracker_ = MemTracker::FindOrCreateTracker("Reading", parent_tracker);
}

Result<ProcessDataResult> BinaryCallParser::Parse(
    const rpc::ConnectionPtr& connection, const IoVecs& data, ReadBufferFull read_buffer_full) {
  if (!call_data_.empty()) {
    RETURN_NOT_OK(listener_->HandleCall(connection, &call_data_));
    call_data_.Reset();
    call_data_consumption_ = ScopedTrackedConsumption();
  }

  auto full_size = IoVecsFullSize(data);

  size_t consumed = 0;
  const size_t header_size = buffer_.size();
  const size_t body_offset = include_header_ ? 0 : header_size;
  while (full_size >= consumed + header_size) {
    IoVecsToBuffer(data, consumed, consumed + header_size, &buffer_);

    size_t data_length = NetworkByteOrder::Load32(buffer_.data() + size_offset_);
    const size_t total_length = data_length + header_size;
    if (total_length > max_message_length_) {
      return STATUS_FORMAT(
          NetworkError,
          "The frame had a length of $0, but we only support messages up to $1 bytes long.",
          total_length, max_message_length_);
    }
    if (consumed + total_length > full_size) {
      size_t call_data_size = header_size - body_offset + data_length;
      MemTracker* blocking_mem_tracker = nullptr;
      if (buffer_tracker_->TryConsume(call_data_size, &blocking_mem_tracker)) {
        call_data_consumption_ = ScopedTrackedConsumption(
            buffer_tracker_, call_data_size, AlreadyConsumed::kTrue);
        call_data_ = CallData(call_data_size);
        IoVecsToBuffer(data, consumed + body_offset, full_size, call_data_.data());
        size_t received_size = full_size - (consumed + body_offset);
        Slice buffer(call_data_.data() + received_size, call_data_size - received_size);
        return ProcessDataResult{ full_size, buffer };
      } else if (read_buffer_full && consumed == 0) {
        auto consumption = blocking_mem_tracker ? blocking_mem_tracker->consumption() : -1;
        auto limit = blocking_mem_tracker ? blocking_mem_tracker->limit() : -1;
        LOG(WARNING) << "Unable to allocate read buffer because of limit, required: "
                     << call_data_size << ", blocked by: " << yb::ToString(blocking_mem_tracker)
                     << ", consumption: " << consumption << " of " << limit;
      }
      break;
    }

    // We might need to skip empty messages (we use them as low level heartbeats for inter-YB RPC
    // connections, don't confuse with RAFT heartbeats which are higher level non-empty messages).
    if (!skip_empty_messages_ || data_length > 0) {
      connection->UpdateLastActivity();
      CallData call_data(total_length - body_offset);
      IoVecsToBuffer(data, consumed + body_offset, consumed + total_length, call_data.data());
      RETURN_NOT_OK(listener_->HandleCall(connection, &call_data));
    }

    consumed += total_length;
  }

  return ProcessDataResult{ consumed, Slice() };
}

} // namespace rpc
} // namespace yb
