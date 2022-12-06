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

#include "yb/docdb/value_type.h"
#include "yb/gutil/casts.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using std::string;

namespace yb {
namespace docdb {

const Schema test_range_schema(
    {ColumnSchema(
         "r1", DataType::INT32, /* is_nullable = */ false, false, false, false, 0,
         SortingType::kAscending),
     ColumnSchema(
         "r2", DataType::INT32, /* is_nullable = */ false, false, false, false, 0,
         SortingType::kAscending),
     // Non-key columns
     ColumnSchema("payload", DataType::INT32, true)},
    {10_ColId, 11_ColId, 12_ColId}, 2);

const Schema test_range_schema_3col(
    {ColumnSchema(
         "r1", DataType::INT32, /* is_nullable = */ false, false, false, false, 0,
         SortingType::kAscending),
     ColumnSchema(
         "r2", DataType::INT32, /* is_nullable = */ false, false, false, false, 0,
         SortingType::kAscending),
     ColumnSchema(
         "r3", DataType::INT32, /* is_nullable = */ false, false, false, false, 0,
         SortingType::kAscending),
     // Non-key columns
     ColumnSchema("payload", DataType::INT32, true)},
    {10_ColId, 11_ColId, 12_ColId, 13_ColId}, 3);

const Schema test_range_schema_6col(
    {ColumnSchema(
         "r1", DataType::INT32, /* is_nullable = */ false, false, false, false, 0,
         SortingType::kAscending),
     ColumnSchema(
         "r2", DataType::INT32, /* is_nullable = */ false, false, false, false, 0,
         SortingType::kAscending),
     ColumnSchema(
         "r3", DataType::INT32, /* is_nullable = */ false, false, false, false, 0,
         SortingType::kAscending),
     ColumnSchema(
         "r4", DataType::INT32, /* is_nullable = */ false, false, false, false, 0,
         SortingType::kAscending),
     ColumnSchema(
         "r5", DataType::INT32, /* is_nullable = */ false, false, false, false, 0,
         SortingType::kAscending),
     ColumnSchema(
         "r6", DataType::INT32, /* is_nullable = */ false, false, false, false, 0,
         SortingType::kDescending),
     // Non-key columns
     ColumnSchema("payload", DataType::INT32, true)},
    {10_ColId, 11_ColId, 12_ColId, 13_ColId, 14_ColId, 15_ColId, 16_ColId}, 6);

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
  void InitializeScanChoicesInstance(const Schema &schema, PgsqlConditionPB &cond);

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
void ScanChoicesTest::InitializeScanChoicesInstance(const Schema &schema, PgsqlConditionPB &cond) {
  current_schema_ = &schema;
  std::vector<KeyEntryValue> empty_components;
  DocPgsqlScanSpec spec(
      schema, rocksdb::kDefaultQueryId, empty_components, empty_components, &cond,
      boost::none, boost::none, nullptr, DocKey(), true);
  const auto &lower_bound = spec.LowerBound();
  EXPECT_OK(lower_bound);
  const auto &upper_bound = spec.UpperBound();
  EXPECT_OK(upper_bound);
  auto base_choices =
      ScanChoices::Create(schema, spec, lower_bound.get(), upper_bound.get()).release();

  choices_ = std::unique_ptr<HybridScanChoices>(down_cast<HybridScanChoices *>(base_choices));
}

// Validate the list of options that choices_ iterates over and the given expected list
void ScanChoicesTest::CheckOptions(const std::vector<std::vector<OptionRange>> &expected) {
  auto expected_it = expected.begin();
  KeyBytes target;

  while (!choices_->FinishedWithScanChoices()) {
    auto cur_opts = choices_->TEST_GetCurrentOptions();
    AssertChoicesEqual(*expected_it, cur_opts);
    for (auto opt : cur_opts) {
      opt.upper().AppendToKey(&target);
    }
    EXPECT_OK(choices_->SkipTargetsUpTo(target));
    EXPECT_OK(choices_->DoneWithCurrentTarget());
    EXPECT_NE(expected_it, expected.end());
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
    std::vector<KeyEntryValue> target_keyentries;
    std::vector<KeyEntryValue> expected_keyentries;
    for (size_t i = 0; i < target.size(); i++) {
      SortingType sorttype = schema.column(i).sorting_type();
      SortOrder sortorder = (sorttype == SortingType::kAscending ||
          sorttype == SortingType::kAscendingNullsLast) ? SortOrder::kAscending :
          SortOrder::kDescending;

      target_keyentries.push_back(KeyEntryValue::Int32(target[i], sortorder));
      expected_keyentries.push_back(KeyEntryValue::Int32(expected[i], sortorder));
    }

    KeyBytes target_keybytes = DocKey(target_keyentries).Encode();
    Slice target_slice = target_keybytes.AsSlice();
    EXPECT_OK(choices_->SkipTargetsUpTo(target_slice));

    KeyBytes expected_keybytes = DocKey(expected_keyentries).Encode();
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

  TestOptionIteration(schema, conds, {{{5}, {}}, {{6}, {}}});
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
