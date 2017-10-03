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
#ifndef KUDU_COMMON_MERGE_ITERATOR_H
#define KUDU_COMMON_MERGE_ITERATOR_H

#include <deque>
#include <gtest/gtest_prod.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "kudu/common/iterator.h"
#include "kudu/common/scan_spec.h"
#include "kudu/util/object_pool.h"

namespace kudu {

class Arena;
class MergeIterState;

// An iterator which merges the results of other iterators, comparing
// based on keys.
class MergeIterator : public RowwiseIterator {
 public:
  // TODO: clarify whether schema is just the projection, or must include the merge
  // key columns. It should probably just be the required projection, which must be
  // a subset of the columns in 'iters'.
  MergeIterator(const Schema &schema,
                const std::vector<std::shared_ptr<RowwiseIterator> > &iters);

  // The passed-in iterators should be already initialized.
  Status Init(ScanSpec *spec) OVERRIDE;

  virtual bool HasNext() const OVERRIDE;

  virtual string ToString() const OVERRIDE;

  virtual const Schema& schema() const OVERRIDE;

  virtual void GetIteratorStats(std::vector<IteratorStats>* stats) const OVERRIDE;

  virtual Status NextBlock(RowBlock* dst) OVERRIDE;

 private:
  void PrepareBatch(RowBlock* dst);
  Status MaterializeBlock(RowBlock* dst);
  Status InitSubIterators(ScanSpec *spec);

  const Schema schema_;

  bool initted_;

  // Holds the subiterators until Init is called.
  // This is required because we can't create a MergeIterState of an uninitialized iterator.
  std::deque<std::shared_ptr<RowwiseIterator> > orig_iters_;
  std::vector<std::shared_ptr<MergeIterState> > iters_;

  // When the underlying iterators are initialized, each needs its own
  // copy of the scan spec in order to do its own pushdown calculations, etc.
  // The copies are allocated from this pool so they can be automatically freed
  // when the UnionIterator goes out of scope.
  ObjectPool<ScanSpec> scan_spec_copies_;
};


// An iterator which unions the results of other iterators.
// This is different from MergeIterator in that it lays the results out end-to-end
// rather than merging them based on keys. Hence it is more efficient since there is
// no comparison needed, and the key column does not need to be read if it is not
// part of the projection.
class UnionIterator : public RowwiseIterator {
 public:
  // Construct a union iterator of the given iterators.
  // The iterators must have matching schemas.
  // The passed-in iterators should not yet be initialized.
  //
  // All passed-in iterators must be fully able to evaluate all predicates - i.e.
  // calling iter->Init(spec) should remove all predicates from the spec.
  explicit UnionIterator(const std::vector<std::shared_ptr<RowwiseIterator> > &iters);

  Status Init(ScanSpec *spec) OVERRIDE;

  bool HasNext() const OVERRIDE;

  string ToString() const OVERRIDE;

  const Schema &schema() const OVERRIDE {
    CHECK(initted_);
    CHECK(schema_.get() != NULL) << "Bad schema in " << ToString();
    return *CHECK_NOTNULL(schema_.get());
  }

  virtual void GetIteratorStats(std::vector<IteratorStats>* stats) const OVERRIDE;

  virtual Status NextBlock(RowBlock* dst) OVERRIDE;

 private:
  void PrepareBatch();
  Status MaterializeBlock(RowBlock* dst);
  void FinishBatch();
  Status InitSubIterators(ScanSpec *spec);

  // Schema: initialized during Init()
  gscoped_ptr<Schema> schema_;
  bool initted_;
  std::deque<std::shared_ptr<RowwiseIterator> > iters_;

  // Since we pop from 'iters_' this field is needed in order to keep
  // the underlying iterators available for GetIteratorStats.
  std::vector<std::shared_ptr<RowwiseIterator> > all_iters_;

  // When the underlying iterators are initialized, each needs its own
  // copy of the scan spec in order to do its own pushdown calculations, etc.
  // The copies are allocated from this pool so they can be automatically freed
  // when the UnionIterator goes out of scope.
  ObjectPool<ScanSpec> scan_spec_copies_;
};

// An iterator which wraps a ColumnwiseIterator, materializing it into full rows.
//
// Predicates which only apply to a single column are pushed down into this iterator.
// While materializing a block, columns with associated predicates are materialized
// first, and the predicates evaluated. If the predicates succeed in filtering out
// an entire batch, then other columns may avoid doing any IO.
class MaterializingIterator : public RowwiseIterator {
 public:
  explicit MaterializingIterator(std::shared_ptr<ColumnwiseIterator> iter);

  // Initialize the iterator, performing predicate pushdown as described above.
  Status Init(ScanSpec *spec) OVERRIDE;

  bool HasNext() const OVERRIDE;

  string ToString() const OVERRIDE;

  const Schema &schema() const OVERRIDE {
    return iter_->schema();
  }

  virtual void GetIteratorStats(std::vector<IteratorStats>* stats) const OVERRIDE {
    iter_->GetIteratorStats(stats);
  }

  virtual Status NextBlock(RowBlock* dst) OVERRIDE;

 private:
  FRIEND_TEST(TestMaterializingIterator, TestPredicatePushdown);
  FRIEND_TEST(TestPredicateEvaluatingIterator, TestPredicateEvaluation);

  Status MaterializeBlock(RowBlock *dst);

  std::shared_ptr<ColumnwiseIterator> iter_;

  std::unordered_multimap<size_t, ColumnRangePredicate> preds_by_column_;

  // The order in which the columns will be materialized.
  std::vector<size_t> materialization_order_;

  // Set only by test code to disallow pushdown.
  bool disallow_pushdown_for_tests_;
};


// An iterator which wraps another iterator and evaluates any predicates that the
// wrapped iterator did not itself handle during push down.
class PredicateEvaluatingIterator : public RowwiseIterator {
 public:
  // Initialize the given '*base_iter' with the given 'spec'.
  //
  // If the base_iter accepts all predicates, then simply returns.
  // Otherwise, swaps out *base_iter for a PredicateEvaluatingIterator which wraps
  // the original iterator and accepts all predicates on its behalf.
  //
  // POSTCONDITION: spec->predicates().empty()
  // POSTCONDITION: base_iter and its wrapper are initialized
  static Status InitAndMaybeWrap(std::shared_ptr<RowwiseIterator> *base_iter,
                                 ScanSpec *spec);

  // Initialize the iterator.
  // POSTCONDITION: spec->predicates().empty()
  Status Init(ScanSpec *spec) OVERRIDE;

  virtual Status NextBlock(RowBlock *dst) OVERRIDE;

  bool HasNext() const OVERRIDE;

  string ToString() const OVERRIDE;

  const Schema &schema() const OVERRIDE {
    return base_iter_->schema();
  }

  virtual void GetIteratorStats(std::vector<IteratorStats>* stats) const OVERRIDE {
    base_iter_->GetIteratorStats(stats);
  }

 private:
  // Construct the evaluating iterator.
  // This is only called from ::InitAndMaybeWrap()
  // REQUIRES: base_iter is already Init()ed.
  explicit PredicateEvaluatingIterator(
      std::shared_ptr<RowwiseIterator> base_iter);

  FRIEND_TEST(TestPredicateEvaluatingIterator, TestPredicateEvaluation);

  std::shared_ptr<RowwiseIterator> base_iter_;
  std::vector<ColumnRangePredicate> predicates_;
};

} // namespace kudu
#endif
