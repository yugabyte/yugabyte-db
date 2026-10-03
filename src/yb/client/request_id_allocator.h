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
#include <string>

#include "yb/common/retryable_request.h"

namespace yb::client::internal {

class RequestIdAllocator;

/// A running retryable request: what to send with it, and what finishes it.
struct RequestIdAllocation {
  RetryableRequestId id;
  RetryableRequestId min_running;

  /// Client id to send with the request. The server deduplicates by client id and request id
  /// together, and a sharded allocator has one per shard.
  const ClientId* client_id;

  /// Finishes the request: the shard, for a sharded allocator.
  RequestIdAllocator* allocator;

  /// Per request state of the allocator, opaque to the caller.
  std::shared_ptr<void> state;
};

/// Allocates the retryable request ids of a client. The implementation is picked by
/// FLAGS_client_request_id_allocator, see CreateRequestIdAllocator.
class RequestIdAllocator {
 public:
  virtual ~RequestIdAllocator() = default;

  virtual RequestIdAllocation Next() = 0;

  /// Reports that the request will never be retried. Exactly once per allocation, through the
  /// allocator of the allocation.
  virtual void Finish(const RequestIdAllocation& allocation) = 0;
};

/// Creates the allocator named by FLAGS_client_request_id_allocator. The allocators with a single
/// id space send client_id with their requests, the sharded ones generate a client id per shard.
std::unique_ptr<RequestIdAllocator> CreateRequestIdAllocator(const ClientId& client_id);

/// The same for the given name, which the benchmark uses to run the allocators that only exist
/// behind the interface.
std::unique_ptr<RequestIdAllocator> CreateRequestIdAllocator(
    const std::string& name, const ClientId& client_id);

} // namespace yb::client::internal
