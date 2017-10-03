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
#ifndef KUDU_COMMON_ITERATOR_H
#define KUDU_COMMON_ITERATOR_H

#include <string>
#include <vector>

#include "kudu/common/columnblock.h"
#include "kudu/common/rowblock.h"
#include "kudu/common/schema.h"
#include "kudu/common/iterator_stats.h"
#include "kudu/util/slice.h"
#include "kudu/util/status.h"

namespace kudu {

class Arena;
class RowBlock;
class ScanSpec;

class IteratorBase {
 public:
  // Initialize the iterator with the given scan spec.
  //
  // The scan spec may be transformed by this call to remove predicates
  // which will be fully pushed down into the iterator.
  //
  // The scan spec pointer must remain valid for the lifetime of the
  // iterator -- the iterator does not take ownership of the object.
  //
  // This may be NULL if there are no predicates, etc.
  // TODO: passing NULL is just convenience for unit tests, etc.
  // Should probably simplify the API by not allowing NULL.
  virtual Status Init(ScanSpec *spec) = 0;

  // Return true if the next call to PrepareBatch is expected to return at least
  // one row.
  virtual bool HasNext() const = 0;

  // Return a string representation of this iterator, suitable for debug output.
  virtual string ToString() const = 0;

  // Return the schema for the rows which this iterator produces.
  virtual const Schema &schema() const = 0;

  virtual ~IteratorBase() {}
};

class RowwiseIterator : public virtual IteratorBase {
 public:
  // Materialize all columns in the destination block.
  //
  // Any indirect data (eg strings) are copied into the destination block's
  // arena, if non-null.
  //
  // The destination row block's selection vector is set to indicate whether
  // each row in the result has passed scan predicates and is still live in
  // the current MVCC snapshot. The iterator implementation should not assume
  // that the selection vector has been initialized prior to this call.
  //
  // The iterator will resize RowBlock to a sufficiently large number of rows,
  // at most its row_capacity. The iterator will attempt to have the maximum
  // number of rows in the batch, but may have less if it is near the end of data.
  virtual Status NextBlock(RowBlock *dst) = 0;

  // Get IteratorStats for each column in the row, including
  // (potentially) columns that are iterated over but not projected;
  virtual void GetIteratorStats(std::vector<IteratorStats>* stats) const = 0;
};

class ColumnwiseIterator : public virtual IteratorBase {
 public:

  // Prepare to read the next nrows from the underlying base data.
  // Sets *nrows back to the number of rows available to be read,
  // which may be less than the requested number in the case that the iterator
  // is at the end of the available data.
  virtual Status PrepareBatch(size_t *nrows) = 0;

  // Materialize the given column into the given column block.
  // col_idx is within the projection schema, not the underlying schema.
  //
  // Any indirect data (eg strings) are copied into the destination block's
  // arena, if non-null.
  virtual Status MaterializeColumn(size_t col_idx, ColumnBlock *dst) = 0;

  // Finish the current batch.
  virtual Status FinishBatch() = 0;

  // Initialize the given SelectionVector to indicate which rows in the currently
  // prepared batch are live vs deleted.
  //
  // The SelectionVector passed in is uninitialized -- i.e its bits are in
  // an undefined state and need to be explicitly set to 1 if the row is live.
  virtual Status InitializeSelectionVector(SelectionVector *sel_vec) = 0;

  // Get IteratorStats for each column.
  virtual void GetIteratorStats(std::vector<IteratorStats>* stats) const = 0;
};

} // namespace kudu
#endif
