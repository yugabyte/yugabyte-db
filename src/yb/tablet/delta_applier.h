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
#ifndef YB_TABLET_DELTA_APPLIER_H
#define YB_TABLET_DELTA_APPLIER_H

#include <string>
#include <memory>
#include <vector>

#include <gtest/gtest_prod.h>

#include "yb/common/iterator.h"
#include "yb/common/schema.h"
#include "yb/gutil/macros.h"
#include "yb/util/status.h"
#include "yb/tablet/cfile_set.h"

namespace yb {
namespace tablet {

class DeltaIterator;

////////////////////////////////////////////////////////////
// Delta-applying iterators
////////////////////////////////////////////////////////////

// A DeltaApplier takes in a base ColumnwiseIterator along with a a
// DeltaIterator. It is responsible for applying the updates coming
// from the delta iterator to the results of the base iterator.
class DeltaApplier : public ColumnwiseIterator {
 public:
  virtual CHECKED_STATUS Init(ScanSpec *spec) OVERRIDE;
  CHECKED_STATUS PrepareBatch(size_t *nrows) OVERRIDE;

  CHECKED_STATUS FinishBatch() OVERRIDE;

  bool HasNext() const OVERRIDE;

  std::string ToString() const OVERRIDE;

  const Schema &schema() const OVERRIDE;

  virtual void GetIteratorStats(std::vector<IteratorStats>* stats) const OVERRIDE;

  // Initialize the selection vector for the current batch.
  // This processes DELETEs -- any deleted rows are set to 0 in 'sel_vec'.
  // All other rows are set to 1.
  virtual CHECKED_STATUS InitializeSelectionVector(SelectionVector *sel_vec) OVERRIDE;

  CHECKED_STATUS MaterializeColumn(size_t col_idx, ColumnBlock *dst) OVERRIDE;
 private:
  friend class DeltaTracker;

  FRIEND_TEST(TestMajorDeltaCompaction, TestCompact);

  DISALLOW_COPY_AND_ASSIGN(DeltaApplier);

  // Construct. The base_iter and delta_iter should not be Initted.
  DeltaApplier(std::shared_ptr<CFileSet::Iterator> base_iter,
               std::shared_ptr<DeltaIterator> delta_iter);
  virtual ~DeltaApplier();

  std::shared_ptr<CFileSet::Iterator> base_iter_;
  std::shared_ptr<DeltaIterator> delta_iter_;

  bool first_prepare_;
};

} // namespace tablet
} // namespace yb
#endif /* YB_TABLET_DELTA_APPLIER_H */
