// Copyright (c) YugaByte, Inc.
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

#include <memory>
#include <string>

#include "yb/common/pgsql_protocol.pb.h"
#include "yb/common/schema.h"

#include "yb/docdb/doc_pgsql_scanspec.h"
#include "yb/docdb/scan_choices.h"

#include "yb/dockv/value_type.h"
#include "yb/gutil/casts.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using std::string;

namespace yb {
namespace docdb {

using dockv::DocKey;
using dockv::KeyEntryValue;

const Schema test_range_schema(
    {ColumnSchema("r1", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
     ColumnSchema("r2", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
     // Non-key columns
     ColumnSchema("payload", DataType::INT32, ColumnKind::VALUE, Nullable::kTrue)},
    {10_ColId, 11_ColId, 12_ColId});

const Schema test_range_schema_3col(
    {ColumnSchema("r1", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
     ColumnSchema("r2", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
     ColumnSchema("r3", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
     // Non-key columns
     ColumnSchema("payload", DataType::INT32, ColumnKind::VALUE, Nullable::kTrue)},
    {10_ColId, 11_ColId, 12_ColId, 13_ColId});

const Schema test_range_schema_6col(
    {ColumnSchema("r1", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
     ColumnSchema("r2", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
     ColumnSchema("r3", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
     ColumnSchema("r4", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
     ColumnSchema("r5", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
     ColumnSchema("r6", DataType::INT32, ColumnKind::RANGE_DESC_NULL_FIRST),
     // Non-key columns
     ColumnSchema("payload", DataType::INT32, ColumnKind::VALUE, Nullable::kTrue)},
    {10_ColId, 11_ColId, 12_ColId, 13_ColId, 14_ColId, 15_ColId, 16_ColId});

class TestCondition {
 public:
  TestCondition(std::vector<ColumnId> &&lhs, yb::QLOperator op, std::vector<std::vector<int>> &&rhs)
      : lhs_{std::move(lhs)}, op_{op}, rhs_{std::move(rhs)} {}

  std::vector<ColumnId> lhs_;
  yb::QLOperator op_;
  std::vector<std::vector<int>> rhs_;
};

class ScanChoicesTest : public YBTest {
 protected:
  void AssertChoicesEqual(const std::vector<OptionRange> &lhs, const std::vector<OptionRange> &rhs);
  void SetupCondition(PgsqlConditionPB *cond_ptr, const std::vector<TestCondition> &conds);
  void InitializeScanChoicesInstance(const Schema& schema, const PgsqlConditionPB& cond);

  bool IsScanChoicesFinished();
  void AdjustForRangeConstraints();
  void CheckOptions(const std::vector<std::vector<OptionRange>> &expected);
  void CheckSkipTargetsUpTo(
      const Schema &schema,
      const std::vector<TestCondition> &conds,
      const std::vector<std::pair<std::vector<int>, std::vector<int>>> &test_targs);

  // Test case implementation.
  Status TestSimpleInFilterHybridScan();
  Status TestSimplePartialFilterHybridScan();
  Status TestSimpleMixedFilterHybridScan();
  void TestOptionIteration(
      const Schema &schema,
      const std::vector<TestCondition> &conds,
      std::vector<std::vector<OptionRange>> &&expected);

 private:
  const Schema *current_schema_;
  std::unique_ptr<HybridScanChoices> choices_;
};

static std::ostream &operator<<(std::ostream &out, const std::vector<OptionRange> &vec) {
  out << "{";

  bool first_elem = true;
  for (const auto &it : vec) {
    if (!first_elem) out << ", ";
    out << it;
    first_elem = false;
  }

  out << "}";
  return out;
}

void ScanChoicesTest::AssertChoicesEqual(
    const std::vector<OptionRange> &lhs, const std::vector<OptionRange> &rhs) {
  EXPECT_EQ(lhs.size(), rhs.size());
  for (size_t i = 0; i < lhs.size(); i++) {
    EXPECT_TRUE(lhs[i] == rhs[i]) << "Expected: " << lhs << " But got " << rhs;
  }
}

void ScanChoicesTest::SetupCondition(
    PgsqlConditionPB *cond_ptr, const std::vector<TestCondition> &conds) {
  PgsqlConditionPB &cond = *cond_ptr;
  cond.set_op(QL_OP_AND);

  for (auto it : conds) {
    auto cond1 = cond.add_operands()->mutable_condition();
    cond1->set_op(it.op_);
    auto &lhs = it.lhs_;
    auto &rhs = it.rhs_;

    auto lhs_operand = cond1->add_operands();
    lhs_operand->set_column_id(lhs[0]);
    if (lhs.size() > 1) {
      lhs_operand->Clear();
      auto lhs_tup = lhs_operand->mutable_tuple();
      for (auto lhs_col : lhs) {
        lhs_tup->add_elems()->set_column_id(lhs_col);
      }
    }

    auto rhs_op = cond1->add_operands();

    if (rhs.size() > 1) {
      auto options = rhs_op->mutable_value()->mutable_list_value();
      for (auto rhs_opt : rhs) {
        if (rhs_opt.size() == 1) {
          options->add_elems()->set_int32_value(rhs_opt[0]);
        } else {
          auto tup = options->add_elems()->mutable_tuple_value();
          for (auto opt_val : rhs_opt) {
            tup->add_elems()->set_int32_value(opt_val);
          }
        }
      }
    } else {
      auto rhs_opt = rhs[0];
      if (rhs_opt.size() == 1) {
        rhs_op->mutable_value()->set_int32_value(rhs[0][0]);
      } else {
        auto tup = rhs_op->mutable_value()->mutable_tuple_value();
        for (auto rhs_opt_val : rhs_opt) {
          tup->add_elems()->set_int32_value(rhs_opt_val);
        }
      }
    }
  }
}

// Initializes an instance of ScanChoices in choices_
void ScanChoicesTest::InitializeScanChoicesInstance(
    const Schema& schema, const PgsqlConditionPB& cond) {
  current_schema_ = &schema;
  dockv::KeyEntryValues empty_components;
  DocPgsqlScanSpec spec(
      schema, rocksdb::kDefaultQueryId, empty_components, empty_components, &cond,
      std::nullopt /* hash_code */, std::nullopt /* max_hash_code */, DocKey(), true);
  const auto& bounds = spec.bounds();
  auto base_choices = ScanChoices::Create(schema, spec, bounds).release();

  choices_ = std::unique_ptr<HybridScanChoices>(down_cast<HybridScanChoices *>(base_choices));
}

bool ScanChoicesTest::IsScanChoicesFinished() {
  return choices_->Finished() ||
         choices_->current_scan_target_ >= choices_->upper_doc_key_;
}

// Utility function to help the test iterate past a range option that originate from a
// non-trivial range constraint. Suppose we had range options
// {[2,2]*, [3,3]}, {[4,9]*}, {[5,5]*} with the active ones marked by * and
// current_scan_target_ is {2,4,+Inf} which is expected to be the result of
// SkipTargetsUpTo({2,4,5}) followed by a DoneWithCurrentTarget(). In this case,
// for the purposes of the option iteration in CheckOption, we want to iterate to the next
// set of active options. Usually, in the real world, we will invoke SkipTargetsUpTo on a higher
// row from the associated table such as {2,9,5} in order to trigger the move to the next set of
// active options but we have no such table in this test file.
// So in order to emulate that behavior, this function adjusts a given current_scan_target_ that
// might contain a +Inf as a result of a range constraint and activates the next set of options.
// In the given example, this function would take the current_scan_target_ value of {2,4,+Inf},
// produce {2,9,5} and invoke SkipTargetsUpTo + DoneWithCurrentTargets to move on to the next set
// of active options.
void ScanChoicesTest::AdjustForRangeConstraints() {
  if (IsScanChoicesFinished()) {
    return;
  }

  EXPECT_FALSE(choices_->Finished());
  const auto &cur_target = choices_->current_scan_target_;
  dockv::DocKeyDecoder decoder(cur_target);
  EXPECT_OK(decoder.DecodeToKeys());
  KeyEntryValue cur_val;
  // The size of the dockey we have found so far that does not need adjustment
  size_t valid_size = 0;

  // Size of the dockey we have read so far
  size_t prev_size = 0;
  auto cur_opts = choices_->TEST_GetCurrentOptions();
  for (size_t i = 0; i < current_schema_->num_range_key_columns(); i++) {
    if (i < cur_opts.size()) {
      EXPECT_OK(decoder.DecodeKeyEntryValue(&cur_val));
    }

    if (i == cur_opts.size() || cur_val.IsInfinity()) {
      dockv::KeyBytes new_target;
      new_target.Reset(Slice(cur_target.data().AsSlice().data(),
          cur_target.data().AsSlice().data() + valid_size));
      ASSERT_GE(i, 1);

      auto is_inclusive = cur_opts[i - 1].upper_inclusive();
      auto sorttype = current_schema_->column(i - 1).sorting_type();
      auto sortorder = (sorttype == SortingType::kAscending ||
          sorttype == SortingType::kAscendingNullsLast) ? SortOrder::kAscending :
          SortOrder::kDescending;

      auto j = i - 1;
      if (is_inclusive) {
        // If column i - 1 was inclusive, we move that column up by one to move
        // the previous OptionRange for column i - 1.
        KeyEntryValue::Int32(cur_opts[j].upper().GetInt32() + 1,
            sortorder).AppendToKey(&new_target);
        j++;
      }

      for (; j < current_schema_->num_range_key_columns(); j++) {
        auto upper = j < cur_opts.size() ? cur_opts[j].upper()
                                         : KeyEntryValue(dockv::KeyEntryType::kHighest);
        upper.AppendToKey(&new_target);
      }

      EXPECT_OK(choices_->SkipTargetsUpTo(new_target));
      // We don't have to invoke DoneWithCurrentTarget to move on to the next set of options
      // if the upper bound we adjusted to was non-inclusive. SkipTargetsUpTo should've shifted
      // the set of active options in this case.
      if (is_inclusive && !IsScanChoicesFinished()) {
        EXPECT_OK(choices_->DoneWithCurrentTarget());
      }
      return;
    }
    valid_size = prev_size;
    prev_size += cur_val.ToKeyBytes().size();
  }
}

// Validate the list of options that choices_ iterates over and the given expected list
void ScanChoicesTest::CheckOptions(const std::vector<std::vector<OptionRange>> &expected) {
  auto expected_it = expected.begin();
  dockv::KeyBytes target;
  dockv::KeyEntryValues target_vals;
  // We don't test for backwards scan yet
  ASSERT_TRUE(choices_->is_forward_scan_);

  while (!IsScanChoicesFinished()) {
    auto cur_opts = choices_->TEST_GetCurrentOptions();
    EXPECT_NE(expected_it, expected.end());
    AssertChoicesEqual(*expected_it, cur_opts);
    for (auto opt : cur_opts) {
      // We don't support testing for (a,b) options where a and b are finite
      // values as of now.
      ASSERT_TRUE((opt.lower_inclusive() || opt.upper_inclusive()) ||
                  opt.upper().type() == dockv::KeyEntryType::kHighest);
      if (opt.lower_inclusive()) {
        opt.lower().AppendToKey(&target);
      } else {
        opt.upper().AppendToKey(&target);
      }
    }
    EXPECT_OK(choices_->SkipTargetsUpTo(target));
    if (!IsScanChoicesFinished()) {
      EXPECT_OK(choices_->DoneWithCurrentTarget());
    }
    AdjustForRangeConstraints();
    expected_it++;
    target.Clear();
  }
}

void ScanChoicesTest::TestOptionIteration(
    const Schema &schema,
    const std::vector<TestCondition> &conds,
    std::vector<std::vector<OptionRange>> &&expected) {
  PgsqlConditionPB cond;
  SetupCondition(&cond, conds);
  InitializeScanChoicesInstance(schema, cond);
  CheckOptions(expected);
}

// Check pairs of inputs and outputs of SkipTargetsUpTo
void ScanChoicesTest::CheckSkipTargetsUpTo(
    const Schema &schema,
    const std::vector<TestCondition> &conds,
    const std::vector<std::pair<std::vector<int>, std::vector<int>>> &test_targs) {
  PgsqlConditionPB cond;
  SetupCondition(&cond, conds);
  InitializeScanChoicesInstance(schema, cond);

  for (auto [target, expected] : test_targs) {
    dockv::KeyEntryValues target_keyentries;
    dockv::KeyEntryValues expected_keyentries;
    for (size_t i = 0; i < target.size(); i++) {
      SortingType sorttype = schema.column(i).sorting_type();
      SortOrder sortorder = (sorttype == SortingType::kAscending ||
          sorttype == SortingType::kAscendingNullsLast) ? SortOrder::kAscending :
          SortOrder::kDescending;

      target_keyentries.push_back(KeyEntryValue::Int32(target[i], sortorder));
      expected_keyentries.push_back(KeyEntryValue::Int32(expected[i], sortorder));
    }

    auto target_keybytes = DocKey(target_keyentries).Encode();
    Slice target_slice = target_keybytes.AsSlice();
    EXPECT_OK(choices_->SkipTargetsUpTo(target_slice));

    auto expected_keybytes = DocKey(expected_keyentries).Encode();
    Slice expected_slice = expected_keybytes.AsSlice();
    EXPECT_TRUE(choices_->CurrentTargetMatchesKey(expected_slice))
        << "Expected: " << DocKey::DebugSliceToString(expected_slice)
        << "but got: " << DocKey::DebugSliceToString(choices_->current_scan_target_);
  }
}

// Tests begin here
TEST_F(ScanChoicesTest, SimpleInFilterHybridScan) {
  std::vector<TestCondition> conds =
      {{{10_ColId}, QL_OP_IN, {{5}, {6}}},
       {{11_ColId}, QL_OP_IN, {{5}, {6}}}};
  const Schema &schema = test_range_schema;

  TestOptionIteration(schema, conds, {{{5}, {5}}, {{5}, {6}}, {{6}, {5}}, {{6}, {6}}});
  CheckSkipTargetsUpTo(schema, conds, {{{4, 4}, {5, 5}}, {{5, 7}, {6, 5}}});
}

TEST_F(ScanChoicesTest, SimplePartialFilterHybridScan) {
  std::vector<TestCondition> conds =
    {{{10_ColId}, QL_OP_IN, {{5}, {6}}}};
  const Schema &schema = test_range_schema;

  TestOptionIteration(schema, conds, {{{5}}, {{6}}});
}

TEST_F(ScanChoicesTest, SimpleMixedFilterHybridScan) {
  std::vector<TestCondition> conds =
      {{{10_ColId}, QL_OP_LESS_THAN_EQUAL, {{21}}},
       {{11_ColId}, QL_OP_IN, {{5}, {6}}}};
  const Schema &schema = test_range_schema;

  TestOptionIteration(schema, conds, {{{21, true}, {5}}, {{21, true}, {6}}});
  CheckSkipTargetsUpTo(schema, conds, {{{10, 4}, {10, 5}}, {{11, 6}, {11, 6}}});
}

TEST_F(ScanChoicesTest, SimpleTupleFilterHybridScan) {
  std::vector<TestCondition> conds =
      {{{10_ColId, 12_ColId}, QL_OP_IN, {{10, 21}, {11, 9}, {11, 18}, {12, 4}}},
       {{11_ColId}, QL_OP_IN, {{5}, {6}}}};
  const Schema &schema = test_range_schema_3col;

  TestOptionIteration(
      schema,
      conds,
      {{{10}, {5}, {21}},
       {{10}, {6}, {21}},
       {{11}, {5}, {9}},
       {{11}, {5}, {18}},
       {{11}, {6}, {9}},
       {{11}, {6}, {18}},
       {{12}, {5}, {4}},
       {{12}, {6}, {4}}});
  CheckSkipTargetsUpTo(
      schema,
      conds,
      {{{10, 6, 22}, {11, 5, 9}},
       {{11, 7, 18}, {12, 5, 4}}});
}

TEST_F(ScanChoicesTest, MixedTupleFilterHybridScan) {
  std::vector<TestCondition> conds =
      {{{10_ColId, 12_ColId, 13_ColId}, QL_OP_IN, {{10, 21, 11},
                                                   {11, 9, 3},
                                                   {11, 18, 4},
                                                   {12, 4, 23}}},
       {{14_ColId}, QL_OP_GREATER_THAN_EQUAL, {{10}}},
       {{11_ColId, 15_ColId}, QL_OP_IN, {{11, 12}, {11, 23}, {14, 10}}}};
  const Schema &schema = test_range_schema_6col;

  TestOptionIteration(
      schema,
      conds,
      {{{10}, {11}, {21}, {11}, {10, false}, {23, SortOrder::kDescending}},
       {{10}, {11}, {21}, {11}, {10, false}, {12, SortOrder::kDescending}},
       {{10}, {14}, {21}, {11}, {10, false}, {10, SortOrder::kDescending}},
       {{11}, {11}, {9}, {3}, {10, false}, {23, SortOrder::kDescending}},
       {{11}, {11}, {9}, {3}, {10, false}, {12, SortOrder::kDescending}},
       {{11}, {11}, {18}, {4}, {10, false}, {23, SortOrder::kDescending}},
       {{11}, {11}, {18}, {4}, {10, false}, {12, SortOrder::kDescending}},
       {{11}, {14}, {9}, {3}, {10, false}, {10, SortOrder::kDescending}},
       {{11}, {14}, {18}, {4}, {10, false}, {10, SortOrder::kDescending}},
       {{12}, {11}, {4}, {23}, {10, false}, {23, SortOrder::kDescending}},
       {{12}, {11}, {4}, {23}, {10, false}, {12, SortOrder::kDescending}},
       {{12}, {14}, {4}, {23}, {10, false}, {10, SortOrder::kDescending}}});
  CheckSkipTargetsUpTo(
      schema,
      conds,
      {{{11, 13, 9, 3, 12, 11}, {11, 14, 9, 3, 10, 10}},
       {{12, 11, 4, 23, 14, 22}, {12, 11, 4, 23, 14, 12}}});
}

}  // namespace docdb
}  // namespace yb
