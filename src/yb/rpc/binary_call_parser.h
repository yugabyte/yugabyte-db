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

#pragma once

#include "yb/util/mem_tracker.h"
#include "yb/util/net/socket.h"
#include "yb/util/strongly_typed_bool.h"

#include "yb/rpc/rpc_fwd.h"
#include "yb/rpc/call_data.h"
#include "yb/rpc/reactor_thread_role.h"

namespace yb {
namespace rpc {

YB_STRONGLY_TYPED_BOOL(IncludeHeader);
YB_STRONGLY_TYPED_BOOL(SkipEmptyMessages);

// Listener of BinaryCallParser, invoked when call is parsed.
class BinaryCallParserListener {
 public:
  virtual Status HandleCall(const ConnectionPtr& connection, CallData* call_data) = 0;
 protected:
  ~BinaryCallParserListener() {}
};

// Utility class to parse binary calls with fixed length header.
class BinaryCallParser {
 public:
  explicit BinaryCallParser(const MemTrackerPtr& parent_tracker,
                            size_t header_size, size_t size_offset, size_t max_message_length,
                            IncludeHeader include_header, SkipEmptyMessages skip_empty_messages,
                            BinaryCallParserListener* listener);

  // If tracker_for_throttle is not nullptr - throttle big requests when tracker_for_throttle
  // (or any of its ancestors) exceeds soft memory limit.
  Result<ProcessCallsResult> Parse(
      const rpc::ConnectionPtr& connection, const IoVecs& data,
      ReadBufferFull read_buffer_full,
      const MemTrackerPtr* tracker_for_throttle) ON_REACTOR_THREAD;

 private:
  MemTrackerPtr buffer_tracker_;
  std::vector<char> call_header_buffer_;
  ScopedTrackedConsumption call_data_consumption_;
  CallData call_data_;
  const size_t size_offset_;
  const size_t max_message_length_;
  const IncludeHeader include_header_;
  const SkipEmptyMessages skip_empty_messages_;
  BinaryCallParserListener* const listener_;
};

// Returns whether we should throttle RPC call based on its size and memory consumption.
// Uses specified throttle_message when logging a warning about throttling an RPC call.
bool ShouldThrottleRpc(
    const MemTrackerPtr& throttle_tracker, ssize_t call_data_size, const char* throttle_message);

} // namespace rpc
} // namespace yb
