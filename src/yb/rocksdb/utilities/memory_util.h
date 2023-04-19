// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
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


#pragma once

#include <map>
#include <string>
#include <unordered_set>
#include <vector>

#include "yb/rocksdb/cache.h"
#include "yb/rocksdb/db.h"

namespace rocksdb {

// Returns the current memory usage of the specified DB instances.
class MemoryUtil {
 public:
  enum UsageType : int {
    // Memory usage of all the mem-tables.
    kMemTableTotal = 0,
    // Memory usage of those un-flushed mem-tables.
    kMemTableUnFlushed = 1,
    // Memory usage of all the table readers.
    kTableReadersTotal = 2,
    // Memory usage by Cache.
    kCacheTotal = 3,
    kNumUsageTypes = 4
  };

  // Returns the approximate memory usage of different types in the input
  // list of DBs and Cache set.  For instance, in the output map
  // usage_by_type, usage_by_type[kMemTableTotal] will store the memory
  // usage of all the mem-tables from all the input rocksdb instances.
  //
  // Note that for memory usage inside Cache class, we will
  // only report the usage of the input "cache_set" without
  // including those Cache usage inside the input list "dbs"
  // of DBs.
  static Status GetApproximateMemoryUsageByType(
      const std::vector<DB*>& dbs,
      const std::unordered_set<const Cache*> cache_set,
      std::map<MemoryUtil::UsageType, uint64_t>* usage_by_type);
};
}  // namespace rocksdb
