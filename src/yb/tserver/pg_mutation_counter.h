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

#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include "yb/common/entity_ids_types.h"

#include "yb/gutil/thread_annotations.h"

#include "yb/util/shared_lock.h"

namespace yb::tserver {

using TableMutationCounts = std::unordered_map<TableId, std::atomic<uint64_t>>;

class PgMutationCounter {
 public:
  void Increase(const TableId& table_id, uint64_t mutation_count) EXCLUDES(mutex_);
  TableMutationCounts GetAndClear() EXCLUDES(mutex_);

  template <class Batch>
  void IncreaseBatch(const Batch& batch) EXCLUDES(mutex_) {
    if (batch.empty()) {
      return;
    }
    auto batch_it = batch.begin();
    auto batch_end = batch.end();
    {
      // The mutex_ is used for only guarding membership changes to the map. A shared lock is needed
      // when checking for membership and an exclusive lock is needed if updating membership i.e.,
      // adding/ removing a key from the map.
      //
      // Incrementing the counters doesn't need a mutex because we use atomic for the counter.
      SharedLock shared_lock(mutex_);
      for (; batch_it != batch_end; ++batch_it) {
        const auto& [key, value] = *batch_it;
        auto it = table_mutation_counts_.find(key);
        if (it == table_mutation_counts_.end()) {
          break;
        }
        it->second += value;
      }
    }

    if (batch_it != batch_end) {
      std::lock_guard lock(mutex_);
      for (; batch_it != batch_end; ++batch_it) {
        const auto& [key, value] = *batch_it;
        table_mutation_counts_[key] += value;
      }
    }
  }
 private:
  std::shared_mutex mutex_;
  // Table id is not stored as oid as that will require conversion in each table of each
  // transaction.
  TableMutationCounts table_mutation_counts_ GUARDED_BY(mutex_);
};

}  // namespace yb::tserver
