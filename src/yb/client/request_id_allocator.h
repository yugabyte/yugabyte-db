// Copyright (c) YugabyteDB, Inc.
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

#include <memory>

#include "yb/common/retryable_request.h"

namespace yb {
namespace client {
namespace internal {

class RequestIdBlock;
using RequestIdBlockPtr = std::shared_ptr<RequestIdBlock>;

class RequestIdAllocatorImpl;

// Result of allocating a retryable request id.
struct RequestIdAllocation {
  RetryableRequestId id;

  // Safe lower bound for the ids of requests that are currently running or could still be
  // issued by this client. Sent to the server as min_running_request_id, which the server uses
  // to garbage-collect its retryable-request dedup state and to reject requests with smaller
  // ids. It is always <= id, and it is monotonically non-decreasing across allocations, so it
  // may lag the exact minimum of the running set. Lagging is safe: it only delays server-side
  // cleanup.
  RetryableRequestId min_running;

  // Handle used to report completion of this request via RequestIdAllocator::Finished.
  RequestIdBlockPtr block;
};

// Allocates retryable request ids for a YBClient.
//
// Replaces a single spinlock-guarded set of running request ids, which serialized every write
// from every thread of the client process (profiled as the top contended lock under single-row
// insert load on many-core hosts). Instead, each allocating thread holds a private block of
// consecutive ids and hands them out with thread-local operations only; the shared registry
// lock is taken once per block (FLAGS_client_request_id_block_size allocations), not once per
// request.
//
// Invariants relied upon by the server (see consensus/retryable_requests.cc):
// - Ids are unique per client.
// - min_running advertised to the server never exceeds the id of any request that is still
//   running or that the client may still issue. The server ratchets its per-client
//   min_running_request_id up to the advertised value and rejects smaller request ids with
//   an Expired error.
// The allocator maintains the second invariant by tracking a floor per active block:
// min_running is the minimum floor across active blocks, and a block stays active until it is
// sealed (no further ids will be allocated from it) and all its allocated ids have finished.
// Blocks left idle by threads that stopped allocating are sealed by a periodic sweep so they
// do not pin min_running forever.
class RequestIdAllocator {
 public:
  RequestIdAllocator();
  ~RequestIdAllocator();

  RequestIdAllocator(const RequestIdAllocator&) = delete;
  void operator=(const RequestIdAllocator&) = delete;

  // Allocates a new request id. Lock-free except once per block.
  RequestIdAllocation Next();

  // Reports that a request allocated from the given block has finished (i.e. it will never be
  // retried with the same id). Must be called exactly once per successful Next().
  static void Finished(const RequestIdBlockPtr& block);

  // Current min-running lower bound, as would be advertised by the next allocation.
  RetryableRequestId TEST_min_running() const;

  // Number of active (not yet retired) blocks.
  size_t TEST_num_active_blocks() const;

  // Runs the idle-block sweep unconditionally, ignoring the time gate.
  void TEST_Sweep();

 private:
  const uint64_t instance_id_;
  const std::shared_ptr<RequestIdAllocatorImpl> impl_;
};

} // namespace internal
} // namespace client
} // namespace yb
