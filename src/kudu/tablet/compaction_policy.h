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
#ifndef KUDU_TABLET_COMPACTION_POLICY_H
#define KUDU_TABLET_COMPACTION_POLICY_H

#include <string>
#include <unordered_set>
#include <vector>

#include "kudu/gutil/macros.h"
#include "kudu/util/slice.h"
#include "kudu/util/status.h"

namespace kudu {
namespace tablet {

class RowSet;
class RowSetTree;

class RowSetInfo;

// A Compaction Policy is responsible for picking which files in a tablet
// should be compacted together.
class CompactionPolicy {
 public:
  CompactionPolicy() {}
  virtual ~CompactionPolicy() {}

  // Select a set of RowSets to compact out of 'tree'.
  //
  // Callers are responsible for externally synchronizing selection within a
  // given Tablet. This will only select rowsets whose compact_flush_lock
  // is unlocked, but will not itself take the lock. Hence no other threads
  // should lock or unlock the rowsets' compact_flush_lock while this method
  // is running.
  //
  // *quality is set to represent how effective the compaction will be on
  // reducing IO in the tablet. TODO: determine the units/ranges of this thing.
  //
  // If 'log' is not NULL, then a verbose log of the compaction selection
  // process will be appended to it.
  virtual Status PickRowSets(const RowSetTree &tree,
                             std::unordered_set<RowSet*>* picked,
                             double* quality,
                             std::vector<std::string>* log) = 0;

  // Return the size at which flush/compact should "roll" to new files. Some
  // compaction policies may prefer to deal with small constant-size files
  // whereas others may prefer large ones.
  virtual uint64_t target_rowset_size() const {
    return 1024 * 1024 * 1024; // no rolling
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(CompactionPolicy);
};

// Compaction policy which, given a size budget for a compaction, and a workload,
// tries to pick a set of RowSets which fit into that budget and minimize the
// future cost of operations on the tablet.
//
// See src/kudu/tablet/compaction-policy.txt for details.
class BudgetedCompactionPolicy : public CompactionPolicy {
 public:
  explicit BudgetedCompactionPolicy(int size_budget_mb);

  virtual Status PickRowSets(const RowSetTree &tree,
                             std::unordered_set<RowSet*>* picked,
                             double* quality,
                             std::vector<std::string>* log) OVERRIDE;

  virtual uint64_t target_rowset_size() const OVERRIDE;

 private:
  void SetupKnapsackInput(const RowSetTree &tree,
                          std::vector<RowSetInfo>* min_key,
                          std::vector<RowSetInfo>* max_key);

  size_t size_budget_mb_;
};

} // namespace tablet
} // namespace kudu
#endif
