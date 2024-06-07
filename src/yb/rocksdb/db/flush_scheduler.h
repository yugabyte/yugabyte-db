//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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

#include <atomic>
#include <mutex>
#include <set>

namespace rocksdb {

class ColumnFamilyData;

// Unless otherwise noted, all methods on FlushScheduler should be called
// only with the DB mutex held or from a single-threaded recovery context.
class FlushScheduler {
 public:
  FlushScheduler() : head_(nullptr) {}

  // May be called from multiple threads at once, but not concurrent with
  // any other method calls on this instance
  void ScheduleFlush(ColumnFamilyData* cfd);

  // Removes and returns Ref()-ed column family. Client needs to Unref().
  // Filters column families that have been dropped.
  ColumnFamilyData* TakeNextColumnFamily();

  bool Empty();

  void Clear();

 private:
  struct Node {
    ColumnFamilyData* column_family;
    Node* next;
  };

  std::atomic<Node*> head_;
#ifndef NDEBUG
  std::mutex checking_mutex_;
  std::set<ColumnFamilyData*> checking_set_;
#endif  // NDEBUG
};

}  // namespace rocksdb
