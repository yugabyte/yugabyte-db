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
#ifndef KUDU_COMMON_SCAN_PREDICATE_H
#define KUDU_COMMON_SCAN_PREDICATE_H

#include <string>

#include <gtest/gtest_prod.h>

#include "kudu/common/schema.h"
#include "kudu/util/bitmap.h"
#include "kudu/util/faststring.h"

namespace kudu {

using std::string;

class RowBlock;
class SelectionVector;

class ValueRange {
 public:
  // Construct a new column range predicate.
  //
  // The min_value and upper_bound pointers should point to storage
  // which represents a constant cell value to be used as a range.
  // The range is inclusive on both ends.
  // The cells are not copied by this object, so should remain unchanged
  // for the lifetime of this object.
  //
  // If either optional is unspecified (i.e. NULL), then the range is
  // open on that end.
  //
  // A range must be bounded on at least one end.
  ValueRange(const TypeInfo* type,
             const void* lower_bound,
             const void* upper_bound);

  bool has_lower_bound() const {
    return lower_bound_;
  }

  bool has_upper_bound() const {
    return upper_bound_;
  }

  const void* lower_bound() const {
    return lower_bound_;
  }

  const void* upper_bound() const {
    return upper_bound_;
  }

  bool IsEquality() const;

  bool ContainsCell(const void* cell) const;

 private:
  const TypeInfo* type_info_;
  const void* lower_bound_;
  const void* upper_bound_;
};

// Predicate which evaluates to true when the value for a given column
// is within a specified range.
//
// TODO: extract an interface for this once it's clearer what the interface should
// look like. Column range is not the only predicate in the world.
class ColumnRangePredicate {
 public:

  // Construct a new column range predicate.
  // The lower_bound and upper_bound pointers should point to storage
  // which represents a constant cell value to be used as a range.
  // The range is inclusive on both ends.
  // If either optional is unspecified (i.e. NULL), then the range is
  // open on that end.
  ColumnRangePredicate(ColumnSchema col, const void* lower_bound,
                       const void* upper_bound);

  const ColumnSchema &column() const {
    return col_;
  }

  string ToString() const;

  // Return the value range for which this predicate passes.
  const ValueRange &range() const { return range_; }

 private:
  // For Evaluate.
  friend class MaterializingIterator;
  friend class PredicateEvaluatingIterator;
  FRIEND_TEST(TestPredicate, TestColumnRange);
  FRIEND_TEST(TestPredicate, TestDontEvalauteOnUnselectedRows);

  // Evaluate the predicate on every row in the rowblock.
  //
  // This is evaluated as an 'AND' with the current contents of *sel:
  // - wherever the predicate evaluates false, set the appropriate bit in the selection
  //   vector to 0.
  // - If the predicate evalutes true, does not make any change to the
  //   selection vector.
  //
  // On any rows where the current value of *sel is false, the predicate evaluation
  // may be skipped.
  //
  // NOTE: the evaluation result is stored into '*sel' which may or may not be the
  // same vector as block->selection_vector().
  void Evaluate(RowBlock *block, SelectionVector *sel) const;

  ColumnSchema col_;
  ValueRange range_;
};

} // namespace kudu
#endif
