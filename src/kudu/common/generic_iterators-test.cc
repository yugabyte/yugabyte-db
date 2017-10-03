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

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <memory>

#include "kudu/common/iterator.h"
#include "kudu/common/generic_iterators.h"
#include "kudu/common/rowblock.h"
#include "kudu/common/scan_spec.h"
#include "kudu/common/schema.h"
#include "kudu/gutil/casts.h"
#include "kudu/gutil/mathlimits.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/test_macros.h"
#include "kudu/util/test_util.h"

DEFINE_int32(num_lists, 3, "Number of lists to merge");
DEFINE_int32(num_rows, 1000, "Number of entries per list");
DEFINE_int32(num_iters, 1, "Number of times to run merge");

namespace kudu {

using std::shared_ptr;

static const Schema kIntSchema({ ColumnSchema("val", UINT32) }, 1);

// Test iterator which just yields integer rows from a provided
// vector.
class VectorIterator : public ColumnwiseIterator {
 public:
  explicit VectorIterator(vector<uint32_t> ints)
      : ints_(std::move(ints)),
        cur_idx_(0) {
  }

  Status Init(ScanSpec *spec) OVERRIDE {
    return Status::OK();
  }

  virtual Status PrepareBatch(size_t *nrows) OVERRIDE {
    int rem = ints_.size() - cur_idx_;
    if (rem < *nrows) {
      *nrows = rem;
    }
    prepared_ = rem;
    return Status::OK();
  }

  virtual Status InitializeSelectionVector(SelectionVector *sel_vec) OVERRIDE {
    sel_vec->SetAllTrue();
    return Status::OK();
  }

  virtual Status MaterializeColumn(size_t col, ColumnBlock *dst) OVERRIDE {
    CHECK_EQ(UINT32, dst->type_info()->physical_type());
    DCHECK_LE(prepared_, dst->nrows());

    for (size_t i = 0; i < prepared_; i++) {
      dst->SetCellValue(i, &(ints_[cur_idx_++]));
    }

    return Status::OK();
  }

  virtual Status FinishBatch() OVERRIDE {
    prepared_ = 0;
    return Status::OK();
  }

  virtual bool HasNext() const OVERRIDE {
    return cur_idx_ < ints_.size();
  }

  virtual string ToString() const OVERRIDE {
    return string("VectorIterator");
  }

  virtual const Schema &schema() const OVERRIDE {
    return kIntSchema;
  }

  virtual void GetIteratorStats(vector<IteratorStats>* stats) const OVERRIDE {
    stats->resize(schema().num_columns());
  }

 private:
  vector<uint32_t> ints_;
  int cur_idx_;
  size_t prepared_;
};

// Test that empty input to a merger behaves correctly.
TEST(TestMergeIterator, TestMergeEmpty) {
  vector<uint32_t> empty_vec;
  shared_ptr<RowwiseIterator> iter(
    new MaterializingIterator(
      shared_ptr<ColumnwiseIterator>(new VectorIterator(empty_vec))));

  vector<shared_ptr<RowwiseIterator> > to_merge;
  to_merge.push_back(iter);

  MergeIterator merger(kIntSchema, to_merge);
  ASSERT_OK(merger.Init(nullptr));
  ASSERT_FALSE(merger.HasNext());
}


class TestIntRangePredicate {
 public:
  TestIntRangePredicate(uint32_t lower, uint32_t upper) :
    lower_(lower),
    upper_(upper),
    pred_(kIntSchema.column(0), &lower_, &upper_) {}

  uint32_t lower_, upper_;
  ColumnRangePredicate pred_;
};

void TestMerge(const TestIntRangePredicate &predicate) {
  vector<shared_ptr<RowwiseIterator> > to_merge;
  vector<uint32_t> ints;
  vector<uint32_t> all_ints;
  all_ints.reserve(FLAGS_num_rows * FLAGS_num_lists);

  // Setup predicate exclusion
  ScanSpec spec;
  spec.AddPredicate(predicate.pred_);
  LOG(INFO) << "Predicate: " << predicate.pred_.ToString();

  for (int i = 0; i < FLAGS_num_lists; i++) {
    ints.clear();
    ints.reserve(FLAGS_num_rows);

    uint32_t entry = 0;
    for (int j = 0; j < FLAGS_num_rows; j++) {
      entry += rand() % 5;
      ints.push_back(entry);
      // Evaluate the predicate before pushing to all_ints
      if (entry >= predicate.lower_ && entry <= predicate.upper_) {
        all_ints.push_back(entry);
      }
    }

    shared_ptr<RowwiseIterator> iter(
      new MaterializingIterator(
        shared_ptr<ColumnwiseIterator>(new VectorIterator(ints))));
    vector<shared_ptr<RowwiseIterator> > to_union;
    to_union.push_back(iter);
    to_merge.push_back(shared_ptr<RowwiseIterator>(new UnionIterator(to_union)));
  }

  VLOG(1) << "Predicate expects " << all_ints.size() << " results";

  LOG_TIMING(INFO, "std::sort the expected results") {
    std::sort(all_ints.begin(), all_ints.end());
  }

  for (int trial = 0; trial < FLAGS_num_iters; trial++) {
    LOG_TIMING(INFO, "Iterate merged lists") {
      MergeIterator merger(kIntSchema, to_merge);
      ASSERT_OK(merger.Init(&spec));

      RowBlock dst(kIntSchema, 100, nullptr);
      size_t total_idx = 0;
      while (merger.HasNext()) {
        ASSERT_OK(merger.NextBlock(&dst));
        ASSERT_GT(dst.nrows(), 0) <<
          "if HasNext() returns true, must return some rows";

        for (int i = 0; i < dst.nrows(); i++) {
          uint32_t this_row = *kIntSchema.ExtractColumnFromRow<UINT32>(dst.row(i), 0);
          ASSERT_GE(this_row, predicate.lower_) << "Yielded integer excluded by predicate";
          ASSERT_LE(this_row, predicate.upper_) << "Yielded integer excluded by predicate";
          if (all_ints[total_idx] != this_row) {
            ASSERT_EQ(all_ints[total_idx], this_row) <<
              "Yielded out of order at idx " << total_idx;
          }
          total_idx++;
        }
      }
    }
  }
}

TEST(TestMergeIterator, TestMerge) {
  TestIntRangePredicate predicate(0, MathLimits<uint32_t>::kMax);
  TestMerge(predicate);
}


TEST(TestMergeIterator, TestPredicate) {
  TestIntRangePredicate predicate(0, FLAGS_num_rows / 5);
  TestMerge(predicate);
}

// Test that the MaterializingIterator properly evaluates predicates when they apply
// to single columns.
TEST(TestMaterializingIterator, TestMaterializingPredicatePushdown) {
  ScanSpec spec;
  TestIntRangePredicate pred1(20, 29);
  spec.AddPredicate(pred1.pred_);
  LOG(INFO) << "Predicate: " << pred1.pred_.ToString();

  vector<uint32> ints;
  for (int i = 0; i < 100; i++) {
    ints.push_back(i);
  }

  shared_ptr<VectorIterator> colwise(new VectorIterator(ints));
  MaterializingIterator materializing(colwise);
  ASSERT_OK(materializing.Init(&spec));
  ASSERT_EQ(0, spec.predicates().size())
    << "Iterator should have pushed down predicate";

  Arena arena(1024, 1024);
  RowBlock dst(kIntSchema, 100, &arena);
  ASSERT_OK(materializing.NextBlock(&dst));
  ASSERT_EQ(dst.nrows(), 100);

  // Check that the resulting selection vector is correct (rows 20-29 selected)
  ASSERT_EQ(10, dst.selection_vector()->CountSelected());
  ASSERT_FALSE(dst.selection_vector()->IsRowSelected(0));
  ASSERT_TRUE(dst.selection_vector()->IsRowSelected(20));
  ASSERT_TRUE(dst.selection_vector()->IsRowSelected(29));
  ASSERT_FALSE(dst.selection_vector()->IsRowSelected(30));
}

// Test that PredicateEvaluatingIterator will properly evaluate predicates on its
// input.
TEST(TestPredicateEvaluatingIterator, TestPredicateEvaluation) {
  ScanSpec spec;
  TestIntRangePredicate pred1(20, 29);
  spec.AddPredicate(pred1.pred_);
  LOG(INFO) << "Predicate: " << pred1.pred_.ToString();

  vector<uint32> ints;
  for (int i = 0; i < 100; i++) {
    ints.push_back(i);
  }

  // Set up a MaterializingIterator with pushdown disabled, so that the
  // PredicateEvaluatingIterator will wrap it and do evaluation.
  shared_ptr<VectorIterator> colwise(new VectorIterator(ints));
  MaterializingIterator *materializing = new MaterializingIterator(colwise);
  materializing->disallow_pushdown_for_tests_ = true;

  // Wrap it in another iterator to do the evaluation
  shared_ptr<RowwiseIterator> outer_iter(materializing);
  ASSERT_OK(PredicateEvaluatingIterator::InitAndMaybeWrap(&outer_iter, &spec));

  ASSERT_NE(reinterpret_cast<uintptr_t>(outer_iter.get()),
            reinterpret_cast<uintptr_t>(materializing))
    << "Iterator pointer should differ after wrapping";

  PredicateEvaluatingIterator *pred_eval = down_cast<PredicateEvaluatingIterator *>(
    outer_iter.get());

  ASSERT_EQ(0, spec.predicates().size())
    << "Iterator tree should have accepted predicate";
  ASSERT_EQ(1, pred_eval->predicates_.size())
    << "Predicate should be evaluated by the outer iterator";

  Arena arena(1024, 1024);
  RowBlock dst(kIntSchema, 100, &arena);
  ASSERT_OK(outer_iter->NextBlock(&dst));
  ASSERT_EQ(dst.nrows(), 100);

  // Check that the resulting selection vector is correct (rows 20-29 selected)
  ASSERT_EQ(10, dst.selection_vector()->CountSelected());
  ASSERT_FALSE(dst.selection_vector()->IsRowSelected(0));
  ASSERT_TRUE(dst.selection_vector()->IsRowSelected(20));
  ASSERT_TRUE(dst.selection_vector()->IsRowSelected(29));
  ASSERT_FALSE(dst.selection_vector()->IsRowSelected(30));
}

// Test that PredicateEvaluatingIterator::InitAndMaybeWrap doesn't wrap an underlying
// iterator when there are no predicates left.
TEST(TestPredicateEvaluatingIterator, TestDontWrapWhenNoPredicates) {
  ScanSpec spec;

  vector<uint32> ints;
  shared_ptr<VectorIterator> colwise(new VectorIterator(ints));
  shared_ptr<RowwiseIterator> materializing(new MaterializingIterator(colwise));
  shared_ptr<RowwiseIterator> outer_iter(materializing);
  ASSERT_OK(PredicateEvaluatingIterator::InitAndMaybeWrap(&outer_iter, &spec));
  ASSERT_EQ(outer_iter, materializing) << "InitAndMaybeWrap should not have wrapped iter";
}

} // namespace kudu
