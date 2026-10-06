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

#include "yb/client/request_id_allocator.h"

#include "yb/common/retryable_request.h"

namespace yb::client::internal {

class AtomicRequestIdAllocatorImpl;

/// Allocates retryable request ids, without a lock, unlike the spinlock guarded set of running
/// ids that it replaces. Meant to be sharded, see request_id_allocator.cc, so that a single
/// instance only sees the requests of a few threads.
///
/// Next() increments an atomic counter. Finish() pushes the id to a lock free queue, and the
/// thread that wins a CAS folds the queued ids into min_running, in place, as in
/// PreparerImpl::Submit. The ids that finish out of order wait in a priority queue that only that
/// thread touches, so min_running is the exact minimum of the running ids as of the last fold.
///
/// A thread folds a bounded number of ids per call, so that it does not stay the folder while its
/// own requests wait. It leaves the rest to the Finish call of a request that is still running,
/// and folds everything itself when there is none, so the ids of the last requests to finish are
/// never left unfolded.
///
/// min_running never exceeds the id of a running request, which is what the server relies on to
/// garbage collect its deduplication state and to reject expired ids, see
/// consensus/retryable_requests.cc. It only lags while ids wait in the queue, which just delays
/// that cleanup.
class AtomicRequestIdAllocator : public RequestIdAllocator {
 public:
  AtomicRequestIdAllocator();

  /// All allocated ids should be finished before destruction.
  ~AtomicRequestIdAllocator();

  AtomicRequestIdAllocator(const AtomicRequestIdAllocator&) = delete;
  void operator=(const AtomicRequestIdAllocator&) = delete;

  const ClientId& client_id() const override;

  RequestIdAllocation Next() override;

  void Finish(RetryableRequestId id) override;

  RetryableRequestId TEST_min_running() const;

 private:
  const ClientId client_id_ = ClientId::GenerateRandom();
  const std::unique_ptr<AtomicRequestIdAllocatorImpl> impl_;
};

} // namespace yb::client::internal
