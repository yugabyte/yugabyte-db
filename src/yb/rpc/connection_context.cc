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

#include "yb/rpc/connection_context.h"

#include "yb/rpc/connection.h"

#include "yb/util/mem_tracker.h"
#include "yb/util/flags.h"

DEFINE_UNKNOWN_int64(read_buffer_memory_limit, -5,
             "Overall limit for read buffers. "
             "Positive value - limit in bytes. "
             "Negative value - percent of root process memory. "
             "Zero - unlimited.");

namespace yb {
namespace rpc {

Status ConnectionContextBase::ReportPendingWriteBytes(size_t bytes_in_queue) {
  return Status::OK();
}

void ConnectionContext::UpdateLastRead(const ConnectionPtr& connection) {
  // By default any read events on connection updates it's last activity. This could be
  // overriden in subclasses for example in order to not treat application-level heartbeats as
  // activity preventing connection from being GCed.
  connection->UpdateLastActivity();
}

ConnectionContextFactory::ConnectionContextFactory(
    int64_t memory_limit, const std::string& name,
    const std::shared_ptr<MemTracker>& parent_mem_tracker)
    : parent_tracker_(parent_mem_tracker) {
  int64_t root_buffer_limit = AbsRelMemLimit(FLAGS_read_buffer_memory_limit, [] {
    return MemTracker::GetRootTracker()->limit();
  });

  auto root_buffer_tracker = MemTracker::FindOrCreateTracker(
      root_buffer_limit, "Read Buffer", parent_mem_tracker);
  memory_limit = AbsRelMemLimit(memory_limit, [&root_buffer_tracker] {
    return root_buffer_tracker->limit();
  });
  buffer_tracker_ = MemTracker::FindOrCreateTracker(memory_limit, name, root_buffer_tracker);
  auto root_call_tracker = MemTracker::FindOrCreateTracker("Call", parent_mem_tracker);
  call_tracker_ = MemTracker::FindOrCreateTracker(name, root_call_tracker);
}

ConnectionContextFactory::~ConnectionContextFactory() = default;

std::string ProcessCallsResult::ToString() const {
  return Format(
      "{ consumed: $0 buffer.size(): $1 bytes_to_skip: $2 }",
      consumed, buffer.size(), bytes_to_skip);
}

} // namespace rpc
} // namespace yb
