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
#include <tuple>

#include "yb/common/retryable_request.h"

namespace yb::client::internal {

class RequestIdAllocator;

/// A running retryable request: what to send with it, and what finishes it. Default constructed,
/// it is no request.
struct RequestIdAllocation {
  RetryableRequestId id = 0;
  RetryableRequestId min_running = 0;

  /// Client id to send with the request. The server deduplicates by client id and request id
  /// together, and the sharded allocator has one per shard.
  const ClientId* client_id = nullptr;

  /// Finishes the request: the shard, for the sharded allocator.
  RequestIdAllocator* allocator = nullptr;

  explicit operator bool() const {
    return client_id != nullptr;
  }

  /// Identifies the request: the ids of different allocators are unrelated.
  bool operator<(const RequestIdAllocation& rhs) const {
    return std::tie(allocator, id) < std::tie(rhs.allocator, rhs.id);
  }
};

/// Allocates the retryable request ids of a client, and generates the client ids that they are
/// sent with. The implementation is picked by FLAGS_client_request_id_allocator, see
/// CreateRequestIdAllocator.
class RequestIdAllocator {
 public:
  virtual ~RequestIdAllocator() = default;

  /// The id that identifies the client in logs: the one sent with the requests, or the one of
  /// the first shard of the sharded allocator.
  virtual const ClientId& client_id() const = 0;

  virtual RequestIdAllocation Next() = 0;

  /// Reports that the request will never be retried. Exactly once per allocation, through the
  /// allocator of the allocation.
  virtual void Finish(RetryableRequestId id) = 0;
};

/// Creates the allocator named by FLAGS_client_request_id_allocator.
std::unique_ptr<RequestIdAllocator> CreateRequestIdAllocator();

/// The same for the given name, for the tests.
std::unique_ptr<RequestIdAllocator> CreateRequestIdAllocator(const std::string& name);

} // namespace yb::client::internal
