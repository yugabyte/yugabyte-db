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
#ifndef YB_TABLET_DELTA_STATS_H
#define YB_TABLET_DELTA_STATS_H

#include <glog/logging.h>
#include <boost/function.hpp>

#include <set>
#include <stdint.h>
#include <string>
#include <unordered_map>

#include "yb/gutil/atomicops.h"
#include "yb/gutil/map-util.h"
#include "yb/common/row_changelist.h"
#include "yb/tablet/mvcc.h"

namespace yb {

namespace tablet {

class DeltaStatsPB;

// A wrapper class for describing data statistics.
class DeltaStats {
 public:
  DeltaStats();

  // Increment update count for column 'col_id' by 'update_count'.
  void IncrUpdateCount(ColumnId col_id, int64_t update_count);

  // Increment the per-store delete count by 'delete_count'.
  void IncrDeleteCount(int64_t delete_count);

  // Increment delete and update counts based on changes contained in
  // 'update'.
  CHECKED_STATUS UpdateStats(const HybridTime& hybrid_time,
                     const RowChangeList& update);

  // Return the number of deletes in the current delta store.
  int64_t delete_count() const { return delete_count_; }

  // Returns number of updates for a given column.
  int64_t update_count_for_col_id(ColumnId col_id) const {
    return FindWithDefault(update_counts_by_col_id_, col_id, 0);
  }

  // Returns the maximum transaction id of any mutation in a delta file.
  HybridTime max_hybrid_time() const {
    return max_hybrid_time_;
  }

  // Returns the minimum transaction id of any mutation in a delta file.
  HybridTime min_hybrid_time() const {
    return min_hybrid_time_;
  }

  // Set the maximum transaction id of any mutation in a delta file.
  void set_max_hybrid_time(const HybridTime& hybrid_time) {
    max_hybrid_time_ = hybrid_time;
  }

  // Set the minimum transaction id in of any mutation in a delta file.
  void set_min_hybrid_time(const HybridTime& hybrid_time) {
    min_hybrid_time_ = hybrid_time;
  }

  std::string ToString() const;

  // Convert this object to the protobuf which is stored in the DeltaFile footer.
  void ToPB(DeltaStatsPB* pb) const;

  // Load this object from the protobuf which is stored in the DeltaFile footer.
  CHECKED_STATUS InitFromPB(const DeltaStatsPB& pb);

  // For each column which has at least one update, add that column's ID to the
  // set 'col_ids'.
  void AddColumnIdsWithUpdates(std::set<ColumnId>* col_ids) const;

 private:
  std::unordered_map<ColumnId, int64_t> update_counts_by_col_id_;
  uint64_t delete_count_;
  HybridTime max_hybrid_time_;
  HybridTime min_hybrid_time_;
};


} // namespace tablet
} // namespace yb

#endif
