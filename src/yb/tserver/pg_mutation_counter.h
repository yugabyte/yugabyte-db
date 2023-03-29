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
#include <shared_mutex>
#include <unordered_map>

#include "yb/common/entity_ids_types.h"

#include "yb/gutil/thread_annotations.h"

namespace yb {
namespace tserver {

using TableMutationCounts = std::unordered_map<TableId, std::atomic<uint64_t>>;

class PgMutationCounter {
 public:
  void Increase(const TableId& table_id, uint64_t mutation_count) EXCLUDES(mutex_);
  TableMutationCounts GetAndClear() EXCLUDES(mutex_);
 private:
  std::shared_mutex mutex_;
  // Table id is not stored as oid as that will require conversion in each table of each
  // transaction.
  TableMutationCounts table_mutation_counts_ GUARDED_BY(mutex_);
};

}  // namespace tserver
}  // namespace yb
