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

#include <cstddef>

#include "yb/util/mem_tracker.h"

namespace yb::ann_methods {

// Bookkeeping helper used by vector index wrappers (e.g. usearch / hnswlib) to attribute their
// heap usage to two child MemTrackers under a single parent:
//
//   - "index_data": the persistent graph state intrinsic to the instance (graph nodes,
//                   neighbour lists, label lookups, lock arrays, ...).
//   - "search_contexts": per-thread scratch buffers used during search (visited-set buffers,
//                        candidate priority queues, ...) -- could in principle be shared
//                        across instances of compatible capacity (see the corresponding TODOs
//                        in usearch_wrapper.cc / hnswlib_wrapper.cc).
//
// Each child is updated independently on its own events: data consumption typically grows
// during insert (and on Reserve() because some allocations are sized to the reserved
// capacity), while search-context consumption is set up by Reserve() and only grows further
// when the underlying library expands its search scratch (e.g. additional VisitedList
// instances spawned under concurrent search load).
//
// The trackers are monotonic: a smaller new value than the currently tracked amount is
// ignored. This matches the underlying libraries which never release memory until destruction.
// Concurrent updaters race optimistically and reconcile under per-tracker spinlocks (provided
// by MonotonicThreadSafeScopedTrackedConsumption).
class IndexMemoryConsumption {
 public:
  IndexMemoryConsumption() = default;

  IndexMemoryConsumption(const IndexMemoryConsumption&) = delete;
  void operator=(const IndexMemoryConsumption&) = delete;

  // Splits parent into "index_data" and "search_contexts" children. Safe to call with a
  // null parent; in that case the consumption tracking becomes a no-op.
  void Init(const MemTrackerPtr& parent);

  // Updates the "index_data" tracker. Monotonic: a non-strictly-greater value is ignored.
  void UpdateData(size_t new_bytes);

  // Updates the "search_contexts" tracker. Monotonic: a non-strictly-greater value is ignored.
  void UpdateSearch(size_t new_bytes);

 private:
  MonotonicThreadSafeScopedTrackedConsumption data_;
  MonotonicThreadSafeScopedTrackedConsumption search_;
};

}  // namespace yb::ann_methods
