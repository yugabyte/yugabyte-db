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

#include "yb/gutil/endian.h"

#include "yb/rpc/connection.h"
#include "yb/rpc/connection_context.h"
#include "yb/rpc/stream.h"

#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_format.h"
#include "yb/util/std_util.h"
#include "yb/util/flags.h"

DEFINE_UNKNOWN_bool(binary_call_parser_reject_on_mem_tracker_hard_limit, true,
    "Whether to reject/ignore calls on hitting mem tracker hard limit.");

DECLARE_int64(rpc_throttle_threshold_bytes);
DECLARE_int32(memory_limit_warn_threshold_percentage);

namespace yb {
namespace rpc {

bool ShouldThrottleRpc(
    const MemTrackerPtr& throttle_tracker, ssize_t call_data_size, const char* throttle_message) {
  return (FLAGS_rpc_throttle_threshold_bytes >= 0 &&
      call_data_size > FLAGS_rpc_throttle_threshold_bytes &&
      !CheckMemoryPressureWithLogging(throttle_tracker, 0 /* score */, throttle_message));
}

BinaryCallParser::BinaryCallParser(
    const MemTrackerPtr& parent_tracker, size_t header_size, size_t size_offset,
    size_t max_message_length, IncludeHeader include_header, SkipEmptyMessages skip_empty_messages,
    BinaryCallParserListener* listener)
    : call_header_buffer_(header_size),
      size_offset_(size_offset),
      max_message_length_(max_message_length),
      include_header_(include_header),
      skip_empty_messages_(skip_empty_messages),
      listener_(listener) {
  buffer_tracker_ = MemTracker::FindOrCreateTracker("Reading", parent_tracker);
}

Result<ProcessCallsResult> BinaryCallParser::Parse(
    const rpc::ConnectionPtr& connection, const IoVecs& data, ReadBufferFull read_buffer_full,
    const MemTrackerPtr* tracker_for_throttle) {
  if (call_data_.should_reject()) {
    // We can't properly respond with error, because we don't have enough call data since we
    // have ignored it. So, we will just ignore this call and client will have timeout.
    // We can implement partial higher level call header parsing to be able to respond with proper
    // error if we will need to detect case when server is hitting memory limit at client side.
    call_data_.Reset();
    call_data_consumption_ = ScopedTrackedConsumption();
  } else if (!call_data_.empty()) {
    RETURN_NOT_OK(listener_->HandleCall(connection, &call_data_));
    call_data_.Reset();
    call_data_consumption_ = ScopedTrackedConsumption();
  }

  const auto full_input_size = IoVecsFullSize(data);
  VLOG(4) << "BinaryCallParser::Parse, full_input_size: " << full_input_size;
  size_t consumed = 0;
  const size_t header_size = call_header_buffer_.size();
  const size_t body_offset = include_header_ ? 0 : header_size;
  VLOG(4) << "BinaryCallParser::Parse, header_size: " << header_size
          << " body_offset: " << body_offset;
  while (full_input_size >= consumed + header_size) {
    IoVecsToBuffer(data, consumed, consumed + header_size, &call_header_buffer_);

    const size_t data_length = NetworkByteOrder::Load32(call_header_buffer_.data() + size_offset_);
    const size_t total_length = data_length + header_size;
    VLOG(4) << "BinaryCallParser::Parse, consumed: " << consumed << " data_length: " << data_length
            << " total_length: " << total_length;
    if (total_length > max_message_length_) {
      return STATUS_FORMAT(
          NetworkError,
          "The frame had a length of $0, but we only support messages up to $1 bytes long.",
          total_length, max_message_length_);
    }
    const size_t call_data_size = total_length - body_offset;
    VLOG(4) << "BinaryCallParser::Parse, call_data_size: " << call_data_size;
    if (consumed + total_length > full_input_size) {
      // `data` only contains beginning of the current call. Here we allocate memory buffer for the
      // whole call data and fill beginning with `data`, the rest of the data for the call will be
      // received and appended by the caller into the same buffer.
      const size_t call_received_size = full_input_size - (consumed + body_offset);
      VLOG(4) << "BinaryCallParser::Parse, call_received_size: " << call_received_size;

      if (tracker_for_throttle) {
        VLOG(4) << "BinaryCallParser::Parse, tracker_for_throttle memory usage: "
                << (*tracker_for_throttle)->LogUsage("");
        if (ShouldThrottleRpc(*tracker_for_throttle, call_data_size, "Ignoring RPC call: ")) {
          call_data_ = CallData(call_data_size, CallData::ShouldRejectTag());
          return ProcessCallsResult{
              .consumed = full_input_size,
              .buffer = Slice(),
              .bytes_to_skip = call_data_size - call_received_size
          };
        }
      }

      MemTracker* blocking_mem_tracker = nullptr;
      if (buffer_tracker_->TryConsume(call_data_size, &blocking_mem_tracker)) {
        call_data_consumption_ = ScopedTrackedConsumption(
            buffer_tracker_, call_data_size, AlreadyConsumed::kTrue);
        call_data_ = CallData(call_data_size);
        IoVecsToBuffer(data, consumed + body_offset, full_input_size, call_data_.data());
        Slice buffer(call_data_.data() + call_received_size, call_data_size - call_received_size);
        VLOG(4) << "BinaryCallParser::Parse, consumed: " << consumed
                << " returning: { full_input_size: " << full_input_size
                << " buffer.size(): " << buffer.size() << " }";
        return ProcessCallsResult{
          .consumed = full_input_size,
          .buffer = buffer,
        };
      } else if (read_buffer_full && consumed == 0) {
        auto consumption = blocking_mem_tracker ? blocking_mem_tracker->consumption() : -1;
        auto limit = blocking_mem_tracker ? blocking_mem_tracker->limit() : -1;
        if (FLAGS_binary_call_parser_reject_on_mem_tracker_hard_limit) {
          YB_LOG_EVERY_N_SECS(WARNING, 3)
              << "Unable to allocate read buffer because of limit, required: " << call_data_size
              << ", blocked by: " << AsString(blocking_mem_tracker)
              << ", consumption: " << consumption << " of " << limit << ". Call will be ignored.\n"
              << DumpMemoryUsage();
          call_data_ = CallData(call_data_size, CallData::ShouldRejectTag());
          return ProcessCallsResult{
            .consumed = full_input_size,
            .buffer = Slice(),
            .bytes_to_skip = call_data_size - call_received_size
          };
        } else {
          // For backward compatibility in behavior until we fix
          // https://github.com/yugabyte/yugabyte-db/issues/2563.
          LOG(WARNING) << "Unable to allocate read buffer because of limit, required: "
                       << call_data_size << ", blocked by: " << AsString(blocking_mem_tracker)
                       << ", consumption: " << consumption << " of " << limit << "\n"
                       << DumpMemoryUsage();
        }
      }
      break;
    }

    // We might need to skip empty messages (we use them as low level heartbeats for inter-YB RPC
    // connections, don't confuse with RAFT heartbeats which are higher level non-empty messages).
    if (!skip_empty_messages_ || data_length > 0) {
      connection->UpdateLastActivity();
      CallData call_data(call_data_size);
      IoVecsToBuffer(data, consumed + body_offset, consumed + total_length, call_data.data());
      RETURN_NOT_OK(listener_->HandleCall(connection, &call_data));
    }

    consumed += total_length;
  }
  VLOG(4) << "BinaryCallParser::Parse, returning: { consumed: " << consumed << " buffer: empty }";
  return ProcessCallsResult {
    .consumed = consumed,
    .buffer = Slice(),
  };
}

} // namespace rpc
} // namespace yb
