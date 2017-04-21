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
#ifndef YB_TABLET_DELTA_ITERATOR_MERGER_H
#define YB_TABLET_DELTA_ITERATOR_MERGER_H

#include <string>
#include <memory>
#include <vector>

#include "yb/tablet/delta_store.h"

namespace yb {

class ScanSpec;

namespace tablet {

// DeltaIterator that simply combines together other DeltaIterators,
// applying deltas from each in order.
class DeltaIteratorMerger : public DeltaIterator {
 public:
  // Create a new DeltaIterator which combines the deltas from
  // all of the input delta stores.
  //
  // If only one store is input, this will automatically return an unwrapped
  // iterator for greater efficiency.
  static CHECKED_STATUS Create(
      const std::vector<std::shared_ptr<DeltaStore> > &stores,
      const Schema* projection,
      const MvccSnapshot &snapshot,
      std::shared_ptr<DeltaIterator>* out);

  ////////////////////////////////////////////////////////////
  // Implementations of DeltaIterator
  ////////////////////////////////////////////////////////////
  virtual CHECKED_STATUS Init(ScanSpec *spec) override;
  virtual CHECKED_STATUS SeekToOrdinal(rowid_t idx) override;
  virtual CHECKED_STATUS PrepareBatch(size_t nrows, PrepareFlag flag) override;
  virtual CHECKED_STATUS ApplyUpdates(size_t col_to_apply, ColumnBlock *dst) override;
  virtual CHECKED_STATUS ApplyDeletes(SelectionVector *sel_vec) override;
  virtual CHECKED_STATUS CollectMutations(vector<Mutation *> *dst, Arena *arena) override;
  virtual CHECKED_STATUS FilterColumnIdsAndCollectDeltas(const std::vector<ColumnId>& col_ids,
                                                 vector<DeltaKeyAndUpdate>* out,
                                                 Arena* arena) override;
  virtual bool HasNext() override;
  virtual std::string ToString() const override;

 private:
  explicit DeltaIteratorMerger(vector<std::shared_ptr<DeltaIterator> > iters);

  std::vector<std::shared_ptr<DeltaIterator> > iters_;
};

} // namespace tablet
} // namespace yb

#endif // YB_TABLET_DELTA_ITERATOR_MERGER_H
