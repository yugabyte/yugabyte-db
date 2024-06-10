// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/tserver/pg_mutation_counter.h"

#include "yb/common/entity_ids.h"

#include "yb/util/logging.h"
#include "yb/util/shared_lock.h"

namespace yb {
namespace tserver {

void PgMutationCounter::Increase(const TableId& table_id, uint64 mutation_count) {
  VLOG_WITH_FUNC(4) << "table_id: " << table_id << " mutation_count: " << mutation_count;
  {
    // The mutex_ is used for only guarding membership changes to the map. A shared lock is needed
    // when checking for membership and an exclusive lock is needed if updating membership i.e.,
    // adding/ removing a key from the map.
    //
    // Incrementing the counters doesn't need a mutex because we use atomic for the counter.
    SharedLock shared_lock(mutex_);
    auto it = table_mutation_counts_.find(table_id);
    if (it != table_mutation_counts_.end()) {
      it->second += mutation_count;
      return;
    }
  }

  std::lock_guard lock(mutex_);
  table_mutation_counts_[table_id] += mutation_count;
}

TableMutationCounts PgMutationCounter::GetAndClear() {
  VLOG_WITH_FUNC(4) << "Getting and clearing global table mutation counter";
  decltype(table_mutation_counts_) result;
  std::lock_guard lock(mutex_);
  table_mutation_counts_.swap(result);
  return result;
}

}  // namespace tserver
}  // namespace yb
