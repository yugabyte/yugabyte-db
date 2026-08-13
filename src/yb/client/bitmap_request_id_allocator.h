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

#include "yb/client/atomic_request_id_allocator.h"

#include "yb/common/retryable_request.h"

namespace yb::client::internal {

class BitmapRequestIdAllocatorImpl;

/// Tracks the finished requests in a bitmap, one bit per id, instead of the queue of
/// AtomicRequestIdAllocator. Kept for comparison.
///
/// Finish() adds the bit of the id to the bitmap: the bit is set exactly once, so adding it sets
/// it, and unlike fetch_or it returns the previous word, which tells whether the last id of that
/// word has just finished. min_running is then the first id whose bit is not set in the current
/// word, since every id below that word has finished. Leaving a word zeroes it, so a single thread
/// does that at a time, elected by a CAS as in PreparerImpl::Submit.
///
/// The bitmap is a ring of FLAGS_client_request_id_num_words words, which has to cover the ids
/// allocated while a request is running. The ids beyond it go to a spinlock guarded map.
class BitmapRequestIdAllocator {
 public:
  BitmapRequestIdAllocator();

  /// All allocated ids should be finished before destruction.
  ~BitmapRequestIdAllocator();

  BitmapRequestIdAllocator(const BitmapRequestIdAllocator&) = delete;
  void operator=(const BitmapRequestIdAllocator&) = delete;

  AtomicRequestIdAllocation Next();

  /// Reports that the request will never be retried. Exactly once per allocated id.
  void Finish(RetryableRequestId id);

  RetryableRequestId TEST_min_running() const;

  /// Number of ids that did not fit into the ring of words.
  int64_t TEST_num_overflows() const;

 private:
  const std::unique_ptr<BitmapRequestIdAllocatorImpl> impl_;
};

} // namespace yb::client::internal
