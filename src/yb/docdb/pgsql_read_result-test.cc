// Copyright (c) YugabyteDB, Inc.
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

#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "yb/bfpg/tserver_opcodes.h"

#include "yb/common/pg_system_attr.h"
#include "yb/common/pgsql_protocol.messages.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/docdb/pgsql_read_result.h"
#include "yb/docdb/ql_rowwise_iterator_interface.h"

#include "yb/util/status_log.h"
#include "yb/util/test_macros.h"
#include "yb/util/write_buffer.h"

#include "yb/yql/pggate/util/pg_doc_data.h"

namespace yb::docdb {
namespace {

const ColumnId kGroupKeyColumnId = 0_ColId;
const ColumnId kRowIdColumnId = 1_ColId;
const ColumnId kValueColumnId = 2_ColId;

QLValuePB OptionalInt32Value(std::optional<int32_t> value) {
  QLValuePB pb;
  if (value.has_value()) {
    pb.set_int32_value(*value);
  }
  return pb;
}

std::string ExpectedResultBuffer(
    size_t fetched_rows, const std::vector<std::vector<QLValuePB>>& rows) {
  WriteBuffer buffer(256);
  pggate::PgWire::WriteInt64(fetched_rows, &buffer);
  for (const auto& row : rows) {
    for (const auto& column : row) {
      CHECK_OK(pggate::WriteColumn(column, &buffer));
    }
  }
  return buffer.ToBuffer();
}

std::string ExpectedRowCountPrefixBuffer(size_t fetched_rows) {
  return ExpectedResultBuffer(fetched_rows, {});
}

class PgsqlReadResultTest : public ::testing::Test {
 protected:
  template <class Processor = PgsqlReadAggregateProcessor>
  Processor CreateProcessor(YQLRowwiseIteratorIf::UniPtr* table_iter_holder = nullptr) {
    result_buffer_.Reset();
    num_rows_pos_ = result_buffer_.Position();
    pggate::PgWire::WriteInt64(0, &result_buffer_);
    auto processor = Processor(read_req_, &result_buffer_, table_iter_holder);
    CHECK_OK(processor.Init(projection_));
    return processor;
  }

  Status PatchFetchedRows(size_t fetched_rows) {
    return pggate::PgWire::WriteInt64(fetched_rows, &result_buffer_, num_rows_pos_);
  }

  Schema schema_;
  dockv::ReaderProjection projection_;
  ThreadSafeArena arena_;
  LWPgsqlReadRequestPB read_req_{&arena_};
  WriteBuffer result_buffer_{256};
  WriteBufferPos num_rows_pos_;
};

struct GroupedInt32InputRow {
  std::optional<int32_t> k;
  int32_t v;
};

class GroupedInt32Test : public PgsqlReadResultTest {
 protected:
  void SetUp() override {
    schema_ = Schema(
        {
            ColumnSchema("k", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
            ColumnSchema("r", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
            ColumnSchema("v", DataType::INT32),
        },
        {kGroupKeyColumnId, kRowIdColumnId, kValueColumnId});
    projection_ = dockv::ReaderProjection(schema_, {kGroupKeyColumnId.rep(), kValueColumnId.rep()});
  }

  Status SetRow(dockv::PgTableRow* row, const GroupedInt32InputRow& input) {
    RETURN_NOT_OK(row->SetValue(kGroupKeyColumnId, OptionalInt32Value(input.k)));
    return row->SetValue(kValueColumnId, QLValue::Primitive(input.v));
  }

  void InitSumRequest(size_t row_count_limit, uint64_t size_limit) {
    read_req_.Clear();
    read_req_.set_is_aggregate(true);
    read_req_.set_schema_version(0);
    read_req_.add_col_refs()->set_column_id(kGroupKeyColumnId);
    read_req_.add_col_refs()->set_column_id(kValueColumnId);
    read_req_.add_targets()->set_column_id(kGroupKeyColumnId);
    auto* tscall = read_req_.add_targets()->mutable_tscall();
    tscall->set_opcode(static_cast<int32_t>(bfpg::TSOpcode::kSumInt32));
    tscall->add_operands()->set_column_id(kValueColumnId);
    read_req_.set_limit(row_count_limit);
    read_req_.set_size_limit(size_limit);
  }
};

struct GroupedInt32ExpectedGroup {
  std::optional<int32_t> k;
  int64_t sum_v;
};

std::string ExpectedGroupedInt32SumBuffer(
    size_t fetched_rows, const std::vector<GroupedInt32ExpectedGroup>& groups) {
  std::vector<std::vector<QLValuePB>> rows;
  rows.reserve(groups.size());
  for (const auto& group : groups) {
    rows.push_back({OptionalInt32Value(group.k), QLValue::Primitive(group.sum_v)});
  }
  return ExpectedResultBuffer(fetched_rows, rows);
}

// ------------------------------------------------------------------------------------------------
// GroupedInt32 fold: GROUP BY INT32 k, SUM(INT32 v). Groups, null keys, Finish.
// ------------------------------------------------------------------------------------------------

TEST_F(GroupedInt32Test, Base) {
  InitSumRequest(/*row_count_limit=*/10, /*size_limit=*/1 << 20);
  auto processor = CreateProcessor();
  dockv::PgTableRow row(projection_);
  for (const auto& input : std::vector<GroupedInt32InputRow>{{1, 10}, {1, 11}, {2, 20}}) {
    ASSERT_OK(SetRow(&row, input));
    ASSERT_EQ(ASSERT_RESULT(processor.ProcessMatchedRow(row)), FetchLimit::kNotReached);
  }
  // 1 group is emitted and one in progress
  ASSERT_OK(PatchFetchedRows(1));
  EXPECT_EQ(result_buffer_.ToBuffer(), ExpectedGroupedInt32SumBuffer(1, {{1, 21}}));
  // the second group is emitted in Finish
  const auto fetched_rows = ASSERT_RESULT(processor.Finish());
  ASSERT_EQ(fetched_rows, 2U);
  ASSERT_OK(PatchFetchedRows(fetched_rows));
  EXPECT_EQ(result_buffer_.ToBuffer(), ExpectedGroupedInt32SumBuffer(2, {{1, 21}, {2, 20}}));
}

TEST_F(GroupedInt32Test, EqualAndNull) {
  InitSumRequest(/*row_count_limit=*/10, /*size_limit=*/1 << 20);
  auto processor = CreateProcessor();
  dockv::PgTableRow row(projection_);
  for (const auto& input : std::vector<GroupedInt32InputRow>{
           {std::nullopt, 10},
           {std::nullopt, 20},
           {1, 5},
           {1, 7},
           {2, 40},
       }) {
    ASSERT_OK(SetRow(&row, input));
    ASSERT_EQ(ASSERT_RESULT(processor.ProcessMatchedRow(row)), FetchLimit::kNotReached);
  }
  const auto fetched_rows = ASSERT_RESULT(processor.Finish());
  ASSERT_EQ(fetched_rows, 3U);
  ASSERT_OK(PatchFetchedRows(fetched_rows));
  EXPECT_EQ(
      result_buffer_.ToBuffer(),
      ExpectedGroupedInt32SumBuffer(3, {{std::nullopt, 30}, {1, 12}, {2, 40}}));
}

TEST_F(GroupedInt32Test, FinishWithNoRows) {
  InitSumRequest(/*row_count_limit=*/10, /*size_limit=*/1 << 20);
  auto processor = CreateProcessor();
  const auto fetched_rows = ASSERT_RESULT(processor.Finish());
  ASSERT_EQ(fetched_rows, 0U);
  ASSERT_OK(PatchFetchedRows(fetched_rows));
  EXPECT_EQ(result_buffer_.ToBuffer(), ExpectedRowCountPrefixBuffer(0));
}

// ------------------------------------------------------------------------------------------------
// GroupedInt32 row count limit: stop before the next group, 0 is unlimited, and batch reads
// ignore it.
// ------------------------------------------------------------------------------------------------

TEST_F(GroupedInt32Test, RowCountLimitReturnsExceededBeforeFoldingNextGroup) {
  InitSumRequest(/*row_count_limit=*/1, /*size_limit=*/1 << 20);
  auto processor = CreateProcessor();
  dockv::PgTableRow row(projection_);
  for (const auto& [input, expected] : std::vector<std::pair<GroupedInt32InputRow, FetchLimit>>{
           {{1, 10}, FetchLimit::kNotReached},
           {{1, 11}, FetchLimit::kNotReached},
           {{2, 20}, FetchLimit::kExceeded},
       }) {
    ASSERT_OK(SetRow(&row, input));
    ASSERT_EQ(ASSERT_RESULT(processor.ProcessMatchedRow(row)), expected);
  }
  ASSERT_OK(PatchFetchedRows(1));
  EXPECT_EQ(result_buffer_.ToBuffer(), ExpectedGroupedInt32SumBuffer(1, {{1, 21}}));
  const auto fetched_rows = ASSERT_RESULT(processor.Finish());
  ASSERT_EQ(fetched_rows, 1U);
  ASSERT_OK(PatchFetchedRows(fetched_rows));
  EXPECT_EQ(result_buffer_.ToBuffer(), ExpectedGroupedInt32SumBuffer(1, {{1, 21}}));
}

TEST_F(GroupedInt32Test, RowCountLimitZeroIsUnlimited) {
  InitSumRequest(/*row_count_limit=*/0, /*size_limit=*/1 << 20);
  auto processor = CreateProcessor();
  dockv::PgTableRow row(projection_);
  for (const auto& input : std::vector<GroupedInt32InputRow>{{1, 10}, {1, 11}, {2, 20}, {3, 30}}) {
    ASSERT_OK(SetRow(&row, input));
    ASSERT_EQ(ASSERT_RESULT(processor.ProcessMatchedRow(row)), FetchLimit::kNotReached);
  }
  const auto fetched_rows = ASSERT_RESULT(processor.Finish());
  ASSERT_EQ(fetched_rows, 3U);
  ASSERT_OK(PatchFetchedRows(fetched_rows));
  EXPECT_EQ(
      result_buffer_.ToBuffer(), ExpectedGroupedInt32SumBuffer(3, {{1, 21}, {2, 20}, {3, 30}}));
}

TEST_F(GroupedInt32Test, RowCountLimitThreeEmitsThreeGroups) {
  InitSumRequest(/*row_count_limit=*/3, /*size_limit=*/1 << 20);
  auto processor = CreateProcessor();
  dockv::PgTableRow row(projection_);
  for (const auto& [input, expected] : std::vector<std::pair<GroupedInt32InputRow, FetchLimit>>{
           {{1, 10}, FetchLimit::kNotReached},
           {{1, 11}, FetchLimit::kNotReached},
           {{2, 20}, FetchLimit::kNotReached},
           {{3, 30}, FetchLimit::kNotReached},
           {{4, 40}, FetchLimit::kExceeded},
       }) {
    ASSERT_OK(SetRow(&row, input));
    ASSERT_EQ(ASSERT_RESULT(processor.ProcessMatchedRow(row)), expected);
  }
  const auto fetched_rows = ASSERT_RESULT(processor.Finish());
  ASSERT_EQ(fetched_rows, 3U);
  ASSERT_OK(PatchFetchedRows(fetched_rows));
  EXPECT_EQ(
      result_buffer_.ToBuffer(), ExpectedGroupedInt32SumBuffer(3, {{1, 21}, {2, 20}, {3, 30}}));
}

// Vector search reaches ExecuteBatchKeys with empty batch_arguments, so the row-count limit
// must be recognized as a batch read from index_request().vector_idx_options() alone.
TEST_F(GroupedInt32Test, VectorSearchRequestIgnoresRowCountLimit) {
  InitSumRequest(/*row_count_limit=*/1, /*size_limit=*/1 << 20);
  read_req_.mutable_index_request()->mutable_vector_idx_options()->set_prefetch_size(10);
  auto processor = CreateProcessor();
  dockv::PgTableRow row(projection_);
  for (const auto& input : std::vector<GroupedInt32InputRow>{{1, 10}, {1, 11}, {2, 20}}) {
    ASSERT_OK(SetRow(&row, input));
    ASSERT_EQ(ASSERT_RESULT(processor.ProcessMatchedRow(row)), FetchLimit::kNotReached);
  }
  const auto fetched_rows = ASSERT_RESULT(processor.Finish());
  ASSERT_EQ(fetched_rows, 2U);
  ASSERT_OK(PatchFetchedRows(fetched_rows));
  EXPECT_EQ(result_buffer_.ToBuffer(), ExpectedGroupedInt32SumBuffer(2, {{1, 21}, {2, 20}}));
}

TEST_F(GroupedInt32Test, YbctidBatchRequestIgnoresRowCountLimit) {
  InitSumRequest(/*row_count_limit=*/1, /*size_limit=*/1 << 20);
  read_req_.add_batch_arguments()->set_order(0);
  auto processor = CreateProcessor();
  dockv::PgTableRow row(projection_);
  for (const auto& input : std::vector<GroupedInt32InputRow>{{1, 10}, {1, 11}, {2, 20}}) {
    ASSERT_OK(SetRow(&row, input));
    ASSERT_EQ(ASSERT_RESULT(processor.ProcessMatchedRow(row)), FetchLimit::kNotReached);
  }
  const auto fetched_rows = ASSERT_RESULT(processor.Finish());
  ASSERT_EQ(fetched_rows, 2U);
  ASSERT_OK(PatchFetchedRows(fetched_rows));
  EXPECT_EQ(result_buffer_.ToBuffer(), ExpectedGroupedInt32SumBuffer(2, {{1, 21}, {2, 20}}));
}

// ------------------------------------------------------------------------------------------------
// GroupedInt32 response size limit: stop before the next group, exact fill, and a 1-byte cap
// still emits the first group.
// ------------------------------------------------------------------------------------------------

TEST_F(GroupedInt32Test, ResponseSizeLimitReturnsExceededBeforeFoldingNextGroup) {
  // 8-byte row-count prefix + one group (5-byte k + 9-byte SUM) = 22. The next
  // group projects to 36, so a 35-byte cap stops before folding k=2.
  InitSumRequest(/*row_count_limit=*/10, /*size_limit=*/35);
  auto processor = CreateProcessor();
  dockv::PgTableRow row(projection_);
  for (const auto& [input, expected] : std::vector<std::pair<GroupedInt32InputRow, FetchLimit>>{
           {{1, 10}, FetchLimit::kNotReached},
           {{1, 11}, FetchLimit::kNotReached},
           {{2, 20}, FetchLimit::kExceeded},
       }) {
    ASSERT_OK(SetRow(&row, input));
    ASSERT_EQ(ASSERT_RESULT(processor.ProcessMatchedRow(row)), expected);
  }
  ASSERT_OK(PatchFetchedRows(1));
  EXPECT_EQ(result_buffer_.ToBuffer(), ExpectedGroupedInt32SumBuffer(1, {{1, 21}}));
  const auto fetched_rows = ASSERT_RESULT(processor.Finish());
  ASSERT_EQ(fetched_rows, 1U);
  ASSERT_OK(PatchFetchedRows(fetched_rows));
  EXPECT_EQ(result_buffer_.ToBuffer(), ExpectedGroupedInt32SumBuffer(1, {{1, 21}}));
}

TEST_F(GroupedInt32Test, ResponseSizeLimitExactFillAfterFirstGroup) {
  InitSumRequest(/*row_count_limit=*/10, /*size_limit=*/22);
  auto processor = CreateProcessor();
  dockv::PgTableRow row(projection_);
  for (const auto& [input, expected] : std::vector<std::pair<GroupedInt32InputRow, FetchLimit>>{
           {{1, 10}, FetchLimit::kNotReached},
           {{1, 11}, FetchLimit::kNotReached},
           {{2, 20}, FetchLimit::kExceeded},
       }) {
    ASSERT_OK(SetRow(&row, input));
    ASSERT_EQ(ASSERT_RESULT(processor.ProcessMatchedRow(row)), expected);
  }
  ASSERT_OK(PatchFetchedRows(1));
  EXPECT_EQ(result_buffer_.ToBuffer(), ExpectedGroupedInt32SumBuffer(1, {{1, 21}}));
  const auto fetched_rows = ASSERT_RESULT(processor.Finish());
  ASSERT_EQ(fetched_rows, 1U);
  ASSERT_OK(PatchFetchedRows(fetched_rows));
  EXPECT_EQ(result_buffer_.ToBuffer(), ExpectedGroupedInt32SumBuffer(1, {{1, 21}}));
}

TEST_F(GroupedInt32Test, ResponseSizeLimitZeroStillEmitsFirstGroup) {
  // Request size_limit 0 is treated as unlimited in ResolveResponseSizeLimit. A
  // 1-byte cap is the tightest positive limit; the first group is still emitted
  // because emitted_rows_ == 0.
  InitSumRequest(/*row_count_limit=*/10, /*size_limit=*/1);
  auto processor = CreateProcessor();
  dockv::PgTableRow row(projection_);
  for (const auto& [input, expected] : std::vector<std::pair<GroupedInt32InputRow, FetchLimit>>{
           {{1, 10}, FetchLimit::kNotReached},
           {{1, 11}, FetchLimit::kNotReached},
           {{2, 20}, FetchLimit::kExceeded},
       }) {
    ASSERT_OK(SetRow(&row, input));
    ASSERT_EQ(ASSERT_RESULT(processor.ProcessMatchedRow(row)), expected);
  }
  ASSERT_OK(PatchFetchedRows(1));
  EXPECT_EQ(result_buffer_.ToBuffer(), ExpectedGroupedInt32SumBuffer(1, {{1, 21}}));
  const auto fetched_rows = ASSERT_RESULT(processor.Finish());
  ASSERT_EQ(fetched_rows, 1U);
  ASSERT_OK(PatchFetchedRows(fetched_rows));
  EXPECT_EQ(result_buffer_.ToBuffer(), ExpectedGroupedInt32SumBuffer(1, {{1, 21}}));
}

// ------------------------------------------------------------------------------------------------
// GroupedInt32 target order: SUM then the group key.
// ------------------------------------------------------------------------------------------------

class GroupedInt32SumThenKeyTest : public GroupedInt32Test {
 protected:
  void InitSumThenKeyRequest(size_t row_count_limit, uint64_t size_limit) {
    read_req_.Clear();
    read_req_.set_is_aggregate(true);
    read_req_.set_schema_version(0);
    read_req_.add_col_refs()->set_column_id(kGroupKeyColumnId);
    read_req_.add_col_refs()->set_column_id(kValueColumnId);
    auto* tscall = read_req_.add_targets()->mutable_tscall();
    tscall->set_opcode(static_cast<int32_t>(bfpg::TSOpcode::kSumInt32));
    tscall->add_operands()->set_column_id(kValueColumnId);
    read_req_.add_targets()->set_column_id(kGroupKeyColumnId);
    read_req_.set_limit(row_count_limit);
    read_req_.set_size_limit(size_limit);
  }
};

std::string ExpectedGroupedInt32SumThenKeyBuffer(
    size_t fetched_rows, const std::vector<GroupedInt32ExpectedGroup>& groups) {
  std::vector<std::vector<QLValuePB>> rows;
  rows.reserve(groups.size());
  for (const auto& group : groups) {
    rows.push_back({QLValue::Primitive(group.sum_v), OptionalInt32Value(group.k)});
  }
  return ExpectedResultBuffer(fetched_rows, rows);
}

TEST_F(GroupedInt32SumThenKeyTest, SumThenKey) {
  InitSumThenKeyRequest(/*row_count_limit=*/10, /*size_limit=*/1 << 20);
  auto processor = CreateProcessor();
  dockv::PgTableRow row(projection_);
  for (const auto& input : std::vector<GroupedInt32InputRow>{{1, 10}, {1, 11}, {2, 20}}) {
    ASSERT_OK(SetRow(&row, input));
    ASSERT_EQ(ASSERT_RESULT(processor.ProcessMatchedRow(row)), FetchLimit::kNotReached);
  }
  const auto fetched_rows = ASSERT_RESULT(processor.Finish());
  ASSERT_EQ(fetched_rows, 2U);
  ASSERT_OK(PatchFetchedRows(fetched_rows));
  EXPECT_EQ(
      result_buffer_.ToBuffer(), ExpectedGroupedInt32SumThenKeyBuffer(2, {{1, 21}, {2, 20}}));
}

// ------------------------------------------------------------------------------------------------
// GroupedInt32 init: a missing projection column fails, a ybctid operand is accepted,
// and a system-column group key fails.
// ------------------------------------------------------------------------------------------------

TEST_F(GroupedInt32Test, MissingProjectionColumnFailsRead) {
  InitSumRequest(/*row_count_limit=*/10, /*size_limit=*/1 << 20);
  projection_ = dockv::ReaderProjection(schema_, {kValueColumnId.rep()});
  result_buffer_.Reset();
  pggate::PgWire::WriteInt64(0, &result_buffer_);
  auto processor = PgsqlReadAggregateProcessor(read_req_, &result_buffer_, nullptr);
  ASSERT_NOK(processor.Init(projection_));
}

TEST_F(GroupedInt32Test, SystemColumnGroupKeyInitFails) {
  for (const auto column_id : {
           static_cast<int>(PgSystemAttrNum::kYBTupleId),
           static_cast<int>(PgSystemAttrNum::kMinTransactionId),
       }) {
    read_req_.Clear();
    read_req_.set_is_aggregate(true);
    read_req_.add_targets()->set_column_id(column_id);
    auto* tscall = read_req_.add_targets()->mutable_tscall();
    tscall->set_opcode(static_cast<int32_t>(bfpg::TSOpcode::kCount));
    result_buffer_.Reset();
    pggate::PgWire::WriteInt64(0, &result_buffer_);
    auto processor = PgsqlReadAggregateProcessor(read_req_, &result_buffer_, nullptr);
    ASSERT_NOK(processor.Init(projection_));
  }
}

TEST_F(GroupedInt32Test, YbctidAggregateOperandInitSucceeds) {
  for (const auto opcode : {bfpg::TSOpcode::kCount, bfpg::TSOpcode::kSumInt32}) {
    read_req_.Clear();
    read_req_.set_is_aggregate(true);
    read_req_.add_targets()->set_column_id(kGroupKeyColumnId);
    auto* tscall = read_req_.add_targets()->mutable_tscall();
    tscall->set_opcode(static_cast<int32_t>(opcode));
    tscall->add_operands()->set_column_id(static_cast<int>(PgSystemAttrNum::kYBTupleId));
    result_buffer_.Reset();
    pggate::PgWire::WriteInt64(0, &result_buffer_);
    auto processor = PgsqlReadAggregateProcessor(read_req_, &result_buffer_, nullptr);
    ASSERT_OK(processor.Init(projection_));
  }
}

// Only ybctid is served by GetSpecialColumn; other system columns fail evaluation.
TEST_F(GroupedInt32Test, NonTupleIdSystemColumnAggregateFails) {
  for (const auto opcode : {bfpg::TSOpcode::kMax, bfpg::TSOpcode::kCount}) {
    read_req_.Clear();
    read_req_.set_is_aggregate(true);
    read_req_.set_schema_version(0);
    read_req_.add_col_refs()->set_column_id(kGroupKeyColumnId);
    read_req_.add_targets()->set_column_id(kGroupKeyColumnId);
    auto* tscall = read_req_.add_targets()->mutable_tscall();
    tscall->set_opcode(static_cast<int32_t>(opcode));
    tscall->add_operands()->set_column_id(
        static_cast<int>(PgSystemAttrNum::kMinTransactionId));
    read_req_.set_limit(10);
    read_req_.set_size_limit(1 << 20);
    auto processor = CreateProcessor();
    dockv::PgTableRow row(projection_);
    ASSERT_OK(SetRow(&row, {1, 10}));
    ASSERT_NOK(processor.ProcessMatchedRow(row));
  }
}

// ------------------------------------------------------------------------------------------------
// ybctid: GROUP BY INT32 k over MIN/MAX/COUNT(ybctid), and a ybctid rows target. Tuple ids come
// from the iterator, not the row, and must be included in the response-size bound.
// ------------------------------------------------------------------------------------------------

// Supplies GetTupleId() for ybctid operands and targets. The other iterator methods are unused.
class TupleIdIterator : public YQLRowwiseIteratorIf {
 public:
  void SetTupleId(std::string tuple_id) { tuple_id_ = std::move(tuple_id); }

  Slice GetTupleId() const override { return tuple_id_; }

  Result<bool> PgFetchNext(dockv::PgTableRow* /*table_row*/) override {
    return STATUS(NotSupported, "TupleIdIterator");
  }

  Result<ReadRestartData> GetReadRestartData() override {
    return STATUS(NotSupported, "TupleIdIterator");
  }

  std::string ToString() const override { return "TupleIdIterator"; }

 protected:
  Result<bool> DoFetchNext(
      qlexpr::QLTableRow* /*table_row*/, const dockv::ReaderProjection* /*projection*/,
      qlexpr::QLTableRow* /*static_row*/,
      const dockv::ReaderProjection* /*static_projection*/) override {
    return STATUS(NotSupported, "TupleIdIterator");
  }

 private:
  std::string tuple_id_;
};

class YbctidTest : public PgsqlReadResultTest {
 protected:
  void SetUp() override {
    schema_ = Schema(
        {ColumnSchema("k", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST)},
        {kGroupKeyColumnId});
    projection_ = dockv::ReaderProjection(schema_, {kGroupKeyColumnId.rep()});
    auto iter = std::make_unique<TupleIdIterator>();
    tuple_ids_ = iter.get();
    holder_ = std::move(iter);
  }

  // GROUP BY k with one aggregate target per opcode, each over ybctid.
  void InitGroupedRequest(
      const std::vector<bfpg::TSOpcode>& opcodes, size_t row_count_limit, uint64_t size_limit) {
    read_req_.Clear();
    read_req_.set_is_aggregate(true);
    read_req_.set_schema_version(0);
    read_req_.add_col_refs()->set_column_id(kGroupKeyColumnId);
    read_req_.add_targets()->set_column_id(kGroupKeyColumnId);
    for (const auto opcode : opcodes) {
      auto* tscall = read_req_.add_targets()->mutable_tscall();
      tscall->set_opcode(static_cast<int32_t>(opcode));
      tscall->add_operands()->set_column_id(static_cast<int>(PgSystemAttrNum::kYBTupleId));
    }
    read_req_.set_limit(row_count_limit);
    read_req_.set_size_limit(size_limit);
  }

  void InitRowsRequest(size_t row_count_limit, uint64_t size_limit) {
    read_req_.Clear();
    read_req_.set_schema_version(0);
    read_req_.add_col_refs()->set_column_id(kGroupKeyColumnId);
    read_req_.add_targets()->set_column_id(static_cast<int>(PgSystemAttrNum::kYBTupleId));
    read_req_.set_limit(row_count_limit);
    read_req_.set_size_limit(size_limit);
  }

  Status SetRow(dockv::PgTableRow* row, int32_t k, const std::string& tuple_id) {
    tuple_ids_->SetTupleId(tuple_id);
    return row->SetValue(kGroupKeyColumnId, QLValue::Primitive(k));
  }

  TupleIdIterator* tuple_ids_ = nullptr;
  YQLRowwiseIteratorIf::UniPtr holder_;
};

QLValuePB BinaryValue(const std::string& bytes) {
  QLValuePB value;
  value.set_binary_value(bytes);
  return value;
}

TEST_F(YbctidTest, GroupedMinMaxCount) {
  InitGroupedRequest(
      {bfpg::TSOpcode::kMax, bfpg::TSOpcode::kMin, bfpg::TSOpcode::kCount},
      /*row_count_limit=*/10, /*size_limit=*/1 << 20);
  auto processor = CreateProcessor(&holder_);
  dockv::PgTableRow row(projection_);
  for (const auto& [k, tuple_id] : std::vector<std::pair<int32_t, std::string>>{
           {1, "b"}, {1, "c"}, {1, "a"}, {2, "z"}}) {
    ASSERT_OK(SetRow(&row, k, tuple_id));
    ASSERT_EQ(ASSERT_RESULT(processor.ProcessMatchedRow(row)), FetchLimit::kNotReached);
  }
  const auto fetched_rows = ASSERT_RESULT(processor.Finish());
  ASSERT_EQ(fetched_rows, 2U);
  ASSERT_OK(PatchFetchedRows(fetched_rows));
  EXPECT_EQ(
      result_buffer_.ToBuffer(),
      ExpectedResultBuffer(
          2, {{QLValue::Primitive(1), BinaryValue("c"), BinaryValue("a"),
               QLValue::Primitive(int64_t{3})},
              {QLValue::Primitive(2), BinaryValue("z"), BinaryValue("z"),
               QLValue::Primitive(int64_t{1})}}));
}

TEST_F(YbctidTest, RowsTargetStopsAtSizeLimit) {
  // Prefix 8 + "a" (9 + 1) = 18. "bb" would reach 29, over the 28-byte cap.
  InitRowsRequest(/*row_count_limit=*/10, /*size_limit=*/28);
  auto processor = CreateProcessor<PgsqlReadRowsProcessor>(&holder_);
  dockv::PgTableRow row(projection_);
  for (const auto& [tuple_id, expected] : std::vector<std::pair<std::string, FetchLimit>>{
           {"a", FetchLimit::kNotReached},
           {"bb", FetchLimit::kExceeded},
       }) {
    ASSERT_OK(SetRow(&row, 1, tuple_id));
    ASSERT_EQ(ASSERT_RESULT(processor.ProcessMatchedRow(row)), expected);
  }
  const auto fetched_rows = ASSERT_RESULT(processor.Finish());
  ASSERT_EQ(fetched_rows, 1U);
  ASSERT_OK(PatchFetchedRows(fetched_rows));
  EXPECT_EQ(result_buffer_.ToBuffer(), ExpectedResultBuffer(1, {{BinaryValue("a")}}));
}

TEST_F(YbctidTest, ResponseSizeLimitStopsBeforeGrowingMaxYbctid) {
  // Binary wire size is 9 + length. After k=1 "a" is emitted the buffer is 23 and the INT32 key
  // adds 5, so a later projection is 28 + max(current MAX wire size, incoming tuple id, 9).
  // "b" projects to 38 and fits; "cc" projects to 39 and is not folded.
  constexpr size_t kSizeLimit = 38;
  InitGroupedRequest({bfpg::TSOpcode::kMax}, /*row_count_limit=*/10, kSizeLimit);
  auto processor = CreateProcessor(&holder_);

  dockv::PgTableRow row(projection_);
  for (const auto& [k, tuple_id] : std::vector<std::pair<int32_t, std::string>>{
           {1, "a"}, {2, "b"}, {2, "cc"}}) {
    ASSERT_OK(SetRow(&row, k, tuple_id));
    ASSERT_EQ(
        ASSERT_RESULT(processor.ProcessMatchedRow(row)),
        tuple_id == "cc" ? FetchLimit::kExceeded : FetchLimit::kNotReached);
  }

  ASSERT_OK(PatchFetchedRows(1));
  EXPECT_EQ(
      result_buffer_.ToBuffer(),
      ExpectedResultBuffer(1, {{QLValue::Primitive(1), BinaryValue("a")}}));
  const auto fetched_rows = ASSERT_RESULT(processor.Finish());
  ASSERT_EQ(fetched_rows, 2U);
  ASSERT_OK(PatchFetchedRows(fetched_rows));
  EXPECT_EQ(result_buffer_.size(), kSizeLimit);
  EXPECT_EQ(
      result_buffer_.ToBuffer(),
      ExpectedResultBuffer(
          2, {{QLValue::Primitive(1), BinaryValue("a")},
              {QLValue::Primitive(2), BinaryValue("b")}}));
}

// ------------------------------------------------------------------------------------------------
// GroupedStringKey response size limit: GROUP BY STRING k, SUM(INT32 v). Stop before a group
// whose key makes the projected row exceed the cap.
// ------------------------------------------------------------------------------------------------

QLValuePB OptionalStringValue(const std::optional<std::string>& value) {
  QLValuePB pb;
  if (value.has_value()) {
    pb.set_string_value(*value);
  }
  return pb;
}

struct GroupedStringKeyInputRow {
  std::optional<std::string> k;
  std::optional<int32_t> v;
};

class GroupedStringKeyTest : public PgsqlReadResultTest {
 protected:
  Status SetRow(dockv::PgTableRow* row, const GroupedStringKeyInputRow& input) {
    RETURN_NOT_OK(row->SetValue(kGroupKeyColumnId, OptionalStringValue(input.k)));
    return row->SetValue(kValueColumnId, OptionalInt32Value(input.v));
  }

  void InitSumRequest(size_t row_count_limit, uint64_t size_limit) {
    read_req_.Clear();
    schema_ = Schema(
        {
            ColumnSchema("k", DataType::STRING, ColumnKind::RANGE_ASC_NULL_FIRST),
            ColumnSchema("v", DataType::INT32),
        },
        {kGroupKeyColumnId, kValueColumnId});
    projection_ = dockv::ReaderProjection(schema_, {kGroupKeyColumnId.rep(), kValueColumnId.rep()});
    read_req_.set_is_aggregate(true);
    read_req_.set_schema_version(0);
    read_req_.add_col_refs()->set_column_id(kGroupKeyColumnId);
    read_req_.add_col_refs()->set_column_id(kValueColumnId);
    read_req_.add_targets()->set_column_id(kGroupKeyColumnId);
    auto* tscall = read_req_.add_targets()->mutable_tscall();
    tscall->set_opcode(static_cast<int32_t>(bfpg::TSOpcode::kSumInt32));
    tscall->add_operands()->set_column_id(kValueColumnId);
    read_req_.set_limit(row_count_limit);
    read_req_.set_size_limit(size_limit);
  }
};

struct GroupedStringKeyExpectedGroup {
  std::optional<std::string> k;
  std::optional<int64_t> sum_v;
};

QLValuePB OptionalInt64Value(std::optional<int64_t> value) {
  QLValuePB pb;
  if (value.has_value()) {
    pb.set_int64_value(*value);
  }
  return pb;
}

std::string ExpectedGroupedStringKeySumBuffer(
    size_t fetched_rows, const std::vector<GroupedStringKeyExpectedGroup>& groups) {
  std::vector<std::vector<QLValuePB>> rows;
  rows.reserve(groups.size());
  for (const auto& group : groups) {
    rows.push_back({OptionalStringValue(group.k), OptionalInt64Value(group.sum_v)});
  }
  return ExpectedResultBuffer(fetched_rows, rows);
}

TEST_F(GroupedStringKeyTest, ResponseSizeLimitStopsBeforeNextGroup) {
  // Emitted bytes: 8-byte prefix + NULL key group (1 + 9) + "" (10 + 9) + "a" with an all-NULL
  // SUM (11 + 1) + "b" (11 + 9) + "b b" (13 + 9) = 91. The 64-byte key group projects to
  // 91 + 74 + 9 = 174, so a 173-byte cap stops right before it.
  InitSumRequest(/*row_count_limit=*/10, /*size_limit=*/173);
  auto processor = CreateProcessor();
  dockv::PgTableRow row(projection_);
  const std::string long_key(64, 'z');
  for (const auto& [input, expected] : std::vector<std::pair<GroupedStringKeyInputRow, FetchLimit>>{
           {{std::nullopt, 1}, FetchLimit::kNotReached},
           {{std::nullopt, std::nullopt}, FetchLimit::kNotReached},
           {{std::nullopt, 2}, FetchLimit::kNotReached},
           {{"", 7}, FetchLimit::kNotReached},
           {{"a", std::nullopt}, FetchLimit::kNotReached},
           {{"b", 10}, FetchLimit::kNotReached},
           {{"b", -4}, FetchLimit::kNotReached},
           {{"b b", 3}, FetchLimit::kNotReached},
           {{"b b", std::nullopt}, FetchLimit::kNotReached},
           {{long_key, 20}, FetchLimit::kExceeded},
       }) {
    ASSERT_OK(SetRow(&row, input));
    ASSERT_EQ(ASSERT_RESULT(processor.ProcessMatchedRow(row)), expected);
  }
  const std::vector<GroupedStringKeyExpectedGroup> expected_groups{
      {std::nullopt, 3}, {"", 7}, {"a", std::nullopt}, {"b", 6}, {"b b", 3}};
  ASSERT_OK(PatchFetchedRows(expected_groups.size()));
  EXPECT_EQ(
      result_buffer_.ToBuffer(),
      ExpectedGroupedStringKeySumBuffer(expected_groups.size(), expected_groups));
  const auto fetched_rows = ASSERT_RESULT(processor.Finish());
  ASSERT_EQ(fetched_rows, expected_groups.size());
  ASSERT_OK(PatchFetchedRows(fetched_rows));
  EXPECT_EQ(
      result_buffer_.ToBuffer(),
      ExpectedGroupedStringKeySumBuffer(expected_groups.size(), expected_groups));
}

// ------------------------------------------------------------------------------------------------
// GroupedStringMinMax response size limit: GROUP BY INT32 k, MIN/MAX(STRING s).
// MeasureProjectedUpperBound keeps the larger of the current aggregate and the incoming value.
// ------------------------------------------------------------------------------------------------

class GroupedStringMaxTest : public PgsqlReadResultTest {
 protected:
  void InitMinMaxRequest(bfpg::TSOpcode opcode, size_t row_count_limit, uint64_t size_limit) {
    read_req_.Clear();
    schema_ = Schema(
        {
            ColumnSchema("k", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
            ColumnSchema("s", DataType::STRING),
        },
        {kGroupKeyColumnId, kValueColumnId});
    projection_ = dockv::ReaderProjection(schema_, {kGroupKeyColumnId.rep(), kValueColumnId.rep()});
    read_req_.set_is_aggregate(true);
    read_req_.set_schema_version(0);
    read_req_.add_col_refs()->set_column_id(kGroupKeyColumnId);
    read_req_.add_col_refs()->set_column_id(kValueColumnId);
    read_req_.add_targets()->set_column_id(kGroupKeyColumnId);
    auto* tscall = read_req_.add_targets()->mutable_tscall();
    tscall->set_opcode(static_cast<int32_t>(opcode));
    tscall->add_operands()->set_column_id(kValueColumnId);
    read_req_.set_limit(row_count_limit);
    read_req_.set_size_limit(size_limit);
  }
};

std::string ExpectedGroupedStringMaxBuffer(
    size_t fetched_rows, int32_t k, const std::string& max_v) {
  return ExpectedResultBuffer(fetched_rows, {{QLValue::Primitive(k), QLValue::Primitive(max_v)}});
}

TEST_F(GroupedStringMaxTest, ResponseSizeLimitGrowingMaxStopsBeforeFoldingRow) {
  // Prefix 8 + INT32 k (5) + 64-byte MAX string (74) = 87. Cap 88 fits the grown
  // first group and stops before folding k=2.
  const std::string grown(64, 'z');
  InitMinMaxRequest(bfpg::TSOpcode::kMax, /*row_count_limit=*/10, /*size_limit=*/88);
  auto processor = CreateProcessor();
  dockv::PgTableRow row(projection_);
  for (const auto& [k, value, expected] : std::vector<std::tuple<int32_t, std::string, FetchLimit>>{
           {1, "a", FetchLimit::kNotReached},
           {1, grown, FetchLimit::kNotReached},
           {2, "b", FetchLimit::kExceeded},
       }) {
    ASSERT_OK(row.SetValue(kGroupKeyColumnId, QLValue::Primitive(k)));
    ASSERT_OK(row.SetValue(kValueColumnId, QLValue::Primitive(value)));
    ASSERT_EQ(ASSERT_RESULT(processor.ProcessMatchedRow(row)), expected);
  }
  ASSERT_OK(PatchFetchedRows(1));
  EXPECT_EQ(result_buffer_.ToBuffer(), ExpectedGroupedStringMaxBuffer(1, 1, grown));
  const auto fetched_rows = ASSERT_RESULT(processor.Finish());
  ASSERT_EQ(fetched_rows, 1U);
  ASSERT_OK(PatchFetchedRows(fetched_rows));
  EXPECT_EQ(result_buffer_.ToBuffer(), ExpectedGroupedStringMaxBuffer(1, 1, grown));
}

std::string ExpectedGroupedStringMinMaxBuffer(
    size_t fetched_rows, const std::vector<std::pair<int32_t, std::string>>& groups) {
  std::vector<std::vector<QLValuePB>> rows;
  rows.reserve(groups.size());
  for (const auto& [k, value] : groups) {
    rows.push_back({QLValue::Primitive(k), QLValue::Primitive(value)});
  }
  return ExpectedResultBuffer(fetched_rows, rows);
}

TEST_F(GroupedStringMaxTest, ResponseSizeLimitMaxWireSizeUpAndDown) {
  // String wire size is 10 + length. After k=1 "b" is emitted the buffer is 24 bytes and the
  // INT32 key adds 5, so a later projection is 29 + max(current MAX, incoming value, 9).
  //   "z"  40, "cc" 41, "zzzz" 43, "{a" 43 (bound keeps "zzzz").
  const std::vector<std::pair<int32_t, std::string>> inputs{
      {1, "b"}, {2, "z"}, {2, "cc"}, {2, "zzzz"}, {2, "{a"}, {2, "a"}, {3, "b"}};

  // Cap 42 stops on "zzzz", before that size-up is folded. MAX stays "z".
  const std::vector<std::pair<int32_t, std::string>> expected_42{{1, "b"}, {2, "z"}};
  InitMinMaxRequest(bfpg::TSOpcode::kMax, /*row_count_limit=*/10, /*size_limit=*/42);
  auto processor_42 = CreateProcessor();
  dockv::PgTableRow row(projection_);
  for (const auto& [k, value] : inputs) {
    ASSERT_OK(row.SetValue(kGroupKeyColumnId, QLValue::Primitive(k)));
    ASSERT_OK(row.SetValue(kValueColumnId, QLValue::Primitive(value)));
    const auto reached_limit = value == "zzzz";
    ASSERT_EQ(
        ASSERT_RESULT(processor_42.ProcessMatchedRow(row)),
        reached_limit ? FetchLimit::kExceeded : FetchLimit::kNotReached);
    if (reached_limit) {
      break;
    }
  }
  ASSERT_OK(PatchFetchedRows(1));
  EXPECT_EQ(result_buffer_.ToBuffer(), ExpectedGroupedStringMinMaxBuffer(1, {{1, "b"}}));
  const auto fetched_42 = ASSERT_RESULT(processor_42.Finish());
  ASSERT_EQ(fetched_42, expected_42.size());
  ASSERT_OK(PatchFetchedRows(fetched_42));
  EXPECT_EQ(
      result_buffer_.ToBuffer(),
      ExpectedGroupedStringMinMaxBuffer(expected_42.size(), expected_42));

  // Cap 43 folds the size-up to "zzzz" and the size-down to "{a", then stops on k=3.
  const std::vector<std::pair<int32_t, std::string>> expected_43{{1, "b"}, {2, "{a"}};
  InitMinMaxRequest(bfpg::TSOpcode::kMax, /*row_count_limit=*/10, /*size_limit=*/43);
  auto processor_43 = CreateProcessor();
  for (const auto& [k, value] : inputs) {
    ASSERT_OK(row.SetValue(kGroupKeyColumnId, QLValue::Primitive(k)));
    ASSERT_OK(row.SetValue(kValueColumnId, QLValue::Primitive(value)));
    const auto reached_limit = k == 3;
    ASSERT_EQ(
        ASSERT_RESULT(processor_43.ProcessMatchedRow(row)),
        reached_limit ? FetchLimit::kExceeded : FetchLimit::kNotReached);
    if (reached_limit) {
      break;
    }
  }
  ASSERT_OK(PatchFetchedRows(expected_43.size()));
  EXPECT_EQ(
      result_buffer_.ToBuffer(),
      ExpectedGroupedStringMinMaxBuffer(expected_43.size(), expected_43));
  const auto fetched_43 = ASSERT_RESULT(processor_43.Finish());
  ASSERT_EQ(fetched_43, expected_43.size());
  ASSERT_OK(PatchFetchedRows(fetched_43));
  EXPECT_EQ(
      result_buffer_.ToBuffer(),
      ExpectedGroupedStringMinMaxBuffer(expected_43.size(), expected_43));
}

TEST_F(GroupedStringMaxTest, ResponseSizeLimitMinWireSizeUpAndDown) {
  // Same rows as the MAX test. MIN bounds: "z" 40, "cc" 41, "zzzz" 43, "{a" 41, "a" 41.
  const std::vector<std::pair<int32_t, std::string>> inputs{
      {1, "b"}, {2, "z"}, {2, "cc"}, {2, "zzzz"}, {2, "{a"}, {2, "a"}, {3, "b"}};

  // Cap 42 stops on "zzzz". MIN has grown from "z" to "cc".
  const std::vector<std::pair<int32_t, std::string>> expected_42{{1, "b"}, {2, "cc"}};
  InitMinMaxRequest(bfpg::TSOpcode::kMin, /*row_count_limit=*/10, /*size_limit=*/42);
  auto processor_42 = CreateProcessor();
  dockv::PgTableRow row(projection_);
  for (const auto& [k, value] : inputs) {
    ASSERT_OK(row.SetValue(kGroupKeyColumnId, QLValue::Primitive(k)));
    ASSERT_OK(row.SetValue(kValueColumnId, QLValue::Primitive(value)));
    const auto reached_limit = value == "zzzz";
    ASSERT_EQ(
        ASSERT_RESULT(processor_42.ProcessMatchedRow(row)),
        reached_limit ? FetchLimit::kExceeded : FetchLimit::kNotReached);
    if (reached_limit) {
      break;
    }
  }
  ASSERT_OK(PatchFetchedRows(1));
  EXPECT_EQ(result_buffer_.ToBuffer(), ExpectedGroupedStringMinMaxBuffer(1, {{1, "b"}}));
  const auto fetched_42 = ASSERT_RESULT(processor_42.Finish());
  ASSERT_EQ(fetched_42, expected_42.size());
  ASSERT_OK(PatchFetchedRows(fetched_42));
  EXPECT_EQ(
      result_buffer_.ToBuffer(),
      ExpectedGroupedStringMinMaxBuffer(expected_42.size(), expected_42));

  // Cap 43 folds the size-up to "cc" and the size-down to "a", then stops on k=3.
  const std::vector<std::pair<int32_t, std::string>> expected_43{{1, "b"}, {2, "a"}};
  InitMinMaxRequest(bfpg::TSOpcode::kMin, /*row_count_limit=*/10, /*size_limit=*/43);
  auto processor_43 = CreateProcessor();
  for (const auto& [k, value] : inputs) {
    ASSERT_OK(row.SetValue(kGroupKeyColumnId, QLValue::Primitive(k)));
    ASSERT_OK(row.SetValue(kValueColumnId, QLValue::Primitive(value)));
    const auto reached_limit = k == 3;
    ASSERT_EQ(
        ASSERT_RESULT(processor_43.ProcessMatchedRow(row)),
        reached_limit ? FetchLimit::kExceeded : FetchLimit::kNotReached);
    if (reached_limit) {
      break;
    }
  }
  ASSERT_OK(PatchFetchedRows(expected_43.size()));
  EXPECT_EQ(
      result_buffer_.ToBuffer(),
      ExpectedGroupedStringMinMaxBuffer(expected_43.size(), expected_43));
  const auto fetched_43 = ASSERT_RESULT(processor_43.Finish());
  ASSERT_EQ(fetched_43, expected_43.size());
  ASSERT_OK(PatchFetchedRows(fetched_43));
  EXPECT_EQ(
      result_buffer_.ToBuffer(),
      ExpectedGroupedStringMinMaxBuffer(expected_43.size(), expected_43));
}

// ------------------------------------------------------------------------------------------------
// GroupedTwoKey fold: GROUP BY INT32 a, INT32 b, SUM(INT32 v). Each null combination is a group.
// ------------------------------------------------------------------------------------------------

class GroupedTwoKeyTest : public PgsqlReadResultTest {
 protected:
  void InitNullableSumRequest(size_t row_count_limit, uint64_t size_limit) {
    read_req_.Clear();
    schema_ = Schema(
        {
            ColumnSchema("a", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
            ColumnSchema("b", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
            ColumnSchema("v", DataType::INT32),
        },
        {kGroupKeyColumnId, kRowIdColumnId, kValueColumnId});
    projection_ = dockv::ReaderProjection(
        schema_, {kGroupKeyColumnId.rep(), kRowIdColumnId.rep(), kValueColumnId.rep()});
    read_req_.set_is_aggregate(true);
    read_req_.set_schema_version(0);
    read_req_.add_col_refs()->set_column_id(kGroupKeyColumnId);
    read_req_.add_col_refs()->set_column_id(kRowIdColumnId);
    read_req_.add_col_refs()->set_column_id(kValueColumnId);
    read_req_.add_targets()->set_column_id(kGroupKeyColumnId);
    read_req_.add_targets()->set_column_id(kRowIdColumnId);
    auto* tscall = read_req_.add_targets()->mutable_tscall();
    tscall->set_opcode(static_cast<int32_t>(bfpg::TSOpcode::kSumInt32));
    tscall->add_operands()->set_column_id(kValueColumnId);
    read_req_.set_limit(row_count_limit);
    read_req_.set_size_limit(size_limit);
  }
};

using GroupedTwoKeyExpectedGroup =
    std::tuple<std::optional<int32_t>, std::optional<int32_t>, int64_t>;

std::string ExpectedGroupedTwoKeyNullableSumBuffer(
    size_t fetched_rows, const std::vector<GroupedTwoKeyExpectedGroup>& groups) {
  std::vector<std::vector<QLValuePB>> rows;
  rows.reserve(groups.size());
  for (const auto& [a, b, sum_v] : groups) {
    rows.push_back({OptionalInt32Value(a), OptionalInt32Value(b), QLValue::Primitive(sum_v)});
  }
  return ExpectedResultBuffer(fetched_rows, rows);
}

TEST_F(GroupedTwoKeyTest, NullCombinationsFoldCorrectly) {
  InitNullableSumRequest(/*row_count_limit=*/10, /*size_limit=*/1 << 20);
  auto processor = CreateProcessor();
  dockv::PgTableRow row(projection_);
  const std::vector<std::tuple<std::optional<int32_t>, std::optional<int32_t>, int32_t>> inputs{
      {std::nullopt, std::nullopt, 1},
      {std::nullopt, std::nullopt, 2},
      {std::nullopt, 1, 10},
      {std::nullopt, 1, 11},
      {1, std::nullopt, 20},
      {1, 1, 30},
      {1, 1, 31},
      {2, 2, 40},
  };
  for (const auto& [a, b, v] : inputs) {
    ASSERT_OK(row.SetValue(kGroupKeyColumnId, OptionalInt32Value(a)));
    ASSERT_OK(row.SetValue(kRowIdColumnId, OptionalInt32Value(b)));
    ASSERT_OK(row.SetValue(kValueColumnId, QLValue::Primitive(v)));
    ASSERT_EQ(ASSERT_RESULT(processor.ProcessMatchedRow(row)), FetchLimit::kNotReached);
  }
  const auto fetched_rows = ASSERT_RESULT(processor.Finish());
  ASSERT_EQ(fetched_rows, 5U);
  ASSERT_OK(PatchFetchedRows(fetched_rows));
  EXPECT_EQ(
      result_buffer_.ToBuffer(), ExpectedGroupedTwoKeyNullableSumBuffer(
                                     5, {{std::nullopt, std::nullopt, 3},
                                         {std::nullopt, 1, 21},
                                         {1, std::nullopt, 20},
                                         {1, 1, 61},
                                         {2, 2, 40}}));
}

// ------------------------------------------------------------------------------------------------
// GroupedDoubleKey fold: GROUP BY DOUBLE k, SUM(INT32 v). NaN keys fold like Postgres
// (NaN = NaN), and -0.0 and 0.0 share a group.
// ------------------------------------------------------------------------------------------------

QLValuePB DoubleValue(double value) {
  QLValuePB pb;
  pb.set_double_value(value);
  return pb;
}

TEST_F(PgsqlReadResultTest, GroupedDoubleKeyFoldsNaNAndSignedZero) {
  schema_ = Schema(
      {
          ColumnSchema("k", DataType::DOUBLE, ColumnKind::RANGE_ASC_NULL_FIRST),
          ColumnSchema("v", DataType::INT32),
      },
      {kGroupKeyColumnId, kValueColumnId});
  projection_ = dockv::ReaderProjection(schema_, {kGroupKeyColumnId.rep(), kValueColumnId.rep()});
  read_req_.Clear();
  read_req_.set_is_aggregate(true);
  read_req_.set_schema_version(0);
  read_req_.add_col_refs()->set_column_id(kGroupKeyColumnId);
  read_req_.add_col_refs()->set_column_id(kValueColumnId);
  read_req_.add_targets()->set_column_id(kGroupKeyColumnId);
  auto* tscall = read_req_.add_targets()->mutable_tscall();
  tscall->set_opcode(static_cast<int32_t>(bfpg::TSOpcode::kSumInt32));
  tscall->add_operands()->set_column_id(kValueColumnId);
  read_req_.set_limit(10);
  read_req_.set_size_limit(1 << 20);

  const auto nan = std::numeric_limits<double>::quiet_NaN();
  auto processor = CreateProcessor();
  dockv::PgTableRow row(projection_);
  for (const auto& [k, v] : std::vector<std::pair<double, int32_t>>{
           {-0.0, 1}, {0.0, 2}, {1.0, 10}, {nan, 100}, {nan, 200}, {nan, 300}}) {
    ASSERT_OK(row.SetValue(kGroupKeyColumnId, DoubleValue(k)));
    ASSERT_OK(row.SetValue(kValueColumnId, QLValue::Primitive(v)));
    ASSERT_EQ(ASSERT_RESULT(processor.ProcessMatchedRow(row)), FetchLimit::kNotReached);
  }
  const auto fetched_rows = ASSERT_RESULT(processor.Finish());
  ASSERT_EQ(fetched_rows, 3U);
  ASSERT_OK(PatchFetchedRows(fetched_rows));
  EXPECT_EQ(
      result_buffer_.ToBuffer(),
      ExpectedResultBuffer(
          3, {{DoubleValue(-0.0), QLValue::Primitive(int64_t{3})},
              {DoubleValue(1.0), QLValue::Primitive(int64_t{10})},
              {DoubleValue(nan), QLValue::Primitive(int64_t{600})}}));
}

// ------------------------------------------------------------------------------------------------
// PlainSum fold: every row folds into one SUM, including Finish with no rows.
// ------------------------------------------------------------------------------------------------

class PlainSumTest : public PgsqlReadResultTest {
 protected:
  void InitRequest(size_t row_count_limit, uint64_t size_limit) {
    read_req_.Clear();
    schema_ = Schema(
        {
            ColumnSchema("r", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
            ColumnSchema("v", DataType::INT32),
        },
        {kRowIdColumnId, kValueColumnId});
    projection_ = dockv::ReaderProjection(schema_, {kValueColumnId.rep()});
    read_req_.set_is_aggregate(true);
    read_req_.set_schema_version(0);
    read_req_.add_col_refs()->set_column_id(kValueColumnId);
    auto* tscall = read_req_.add_targets()->mutable_tscall();
    tscall->set_opcode(static_cast<int32_t>(bfpg::TSOpcode::kSumInt32));
    tscall->add_operands()->set_column_id(kValueColumnId);
    read_req_.set_limit(row_count_limit);
    read_req_.set_size_limit(size_limit);
  }
};

std::string ExpectedPlainSumBuffer(size_t fetched_rows, int64_t sum_v) {
  return ExpectedResultBuffer(fetched_rows, {{QLValue::Primitive(sum_v)}});
}

TEST_F(PlainSumTest, FoldsAllRowsIntoOne) {
  InitRequest(/*row_count_limit=*/10, /*size_limit=*/1 << 20);
  auto processor = CreateProcessor();
  dockv::PgTableRow row(projection_);
  for (int32_t v : {10, 11, 20}) {
    ASSERT_OK(row.SetValue(kValueColumnId, QLValue::Primitive(v)));
    ASSERT_EQ(ASSERT_RESULT(processor.ProcessMatchedRow(row)), FetchLimit::kNotReached);
  }
  const auto fetched_rows = ASSERT_RESULT(processor.Finish());
  ASSERT_EQ(fetched_rows, 1U);
  ASSERT_OK(PatchFetchedRows(fetched_rows));
  EXPECT_EQ(result_buffer_.ToBuffer(), ExpectedPlainSumBuffer(1, 41));
}

TEST_F(PlainSumTest, FinishWithNoRows) {
  InitRequest(/*row_count_limit=*/10, /*size_limit=*/1 << 20);
  auto processor = CreateProcessor();
  const auto fetched_rows = ASSERT_RESULT(processor.Finish());
  ASSERT_EQ(fetched_rows, 0U);
  ASSERT_OK(PatchFetchedRows(fetched_rows));
  EXPECT_EQ(result_buffer_.ToBuffer(), ExpectedRowCountPrefixBuffer(0));
}

// ------------------------------------------------------------------------------------------------
// PlainSum response size limit: keeps folding after the cap, then emits one row.
// ------------------------------------------------------------------------------------------------

TEST_F(PlainSumTest, SizeLimitDoesNotStopMidFold) {
  InitRequest(/*row_count_limit=*/10, /*size_limit=*/1);
  auto processor = CreateProcessor();
  dockv::PgTableRow row(projection_);
  for (int32_t v : {10, 11, 20}) {
    ASSERT_OK(row.SetValue(kValueColumnId, QLValue::Primitive(v)));
    ASSERT_EQ(ASSERT_RESULT(processor.ProcessMatchedRow(row)), FetchLimit::kNotReached);
  }
  const auto fetched_rows = ASSERT_RESULT(processor.Finish());
  ASSERT_EQ(fetched_rows, 1U);
  ASSERT_OK(PatchFetchedRows(fetched_rows));
  EXPECT_EQ(result_buffer_.ToBuffer(), ExpectedPlainSumBuffer(1, 41));
}

// ------------------------------------------------------------------------------------------------
// Rows response size limit: non-aggregate INT32 projection. The first row is kept when it
// exceeds the cap; the next row is truncated.
// ------------------------------------------------------------------------------------------------

class RowsTest : public PgsqlReadResultTest {
 protected:
  void InitRequest(size_t row_count_limit, uint64_t size_limit) {
    read_req_.Clear();
    schema_ = Schema(
        {
            ColumnSchema("v", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
        },
        {kValueColumnId});
    projection_ = dockv::ReaderProjection(schema_, {kValueColumnId.rep()});
    read_req_.set_schema_version(0);
    read_req_.add_col_refs()->set_column_id(kValueColumnId);
    read_req_.add_targets()->set_column_id(kValueColumnId);
    read_req_.set_limit(row_count_limit);
    read_req_.set_size_limit(size_limit);
  }

  PgsqlReadRowsProcessor CreateProcessor() {
    return PgsqlReadResultTest::CreateProcessor<PgsqlReadRowsProcessor>();
  }
};

std::string ExpectedRowsInt32Buffer(size_t fetched_rows, const std::vector<int32_t>& values) {
  std::vector<std::vector<QLValuePB>> rows;
  rows.reserve(values.size());
  for (int32_t value : values) {
    rows.push_back({QLValue::Primitive(value)});
  }
  return ExpectedResultBuffer(fetched_rows, rows);
}

TEST_F(RowsTest, FirstOversizedRowStillEmitted) {
  InitRequest(/*row_count_limit=*/10, /*size_limit=*/1);
  auto processor = CreateProcessor();
  dockv::PgTableRow row(projection_);
  ASSERT_OK(row.SetValue(kValueColumnId, QLValue::Primitive(10)));
  // First row is kept even when it exceeds the size cap; the rows processor reports kReached.
  ASSERT_EQ(ASSERT_RESULT(processor.ProcessMatchedRow(row)), FetchLimit::kReached);
  const auto fetched_rows = ASSERT_RESULT(processor.Finish());
  ASSERT_EQ(fetched_rows, 1U);
  ASSERT_OK(PatchFetchedRows(fetched_rows));
  EXPECT_EQ(result_buffer_.ToBuffer(), ExpectedRowsInt32Buffer(1, {10}));
}

TEST_F(RowsTest, SecondRowTruncatedWhenSizeExceeded) {
  // Prefix 8 + first INT32 (5) = 13. Cap 16 fits the first row and truncates the second.
  InitRequest(/*row_count_limit=*/10, /*size_limit=*/16);
  auto processor = CreateProcessor();
  dockv::PgTableRow row(projection_);
  for (const auto& [v, expected] : std::vector<std::pair<int32_t, FetchLimit>>{
           {10, FetchLimit::kNotReached},
           {20, FetchLimit::kExceeded},
       }) {
    ASSERT_OK(row.SetValue(kValueColumnId, QLValue::Primitive(v)));
    ASSERT_EQ(ASSERT_RESULT(processor.ProcessMatchedRow(row)), expected);
  }
  const auto fetched_rows = ASSERT_RESULT(processor.Finish());
  ASSERT_EQ(fetched_rows, 1U);
  ASSERT_OK(PatchFetchedRows(fetched_rows));
  EXPECT_EQ(result_buffer_.ToBuffer(), ExpectedRowsInt32Buffer(1, {10}));
}

// Vector search reads keys from a provider that cannot page, so a size limit could only fail the
// read. The rows processor applies none to it.
TEST_F(RowsTest, VectorSearchIgnoresSizeLimit) {
  InitRequest(/*row_count_limit=*/10, /*size_limit=*/1);
  read_req_.mutable_index_request()->mutable_vector_idx_options()->set_prefetch_size(10);
  auto processor = CreateProcessor();
  dockv::PgTableRow row(projection_);
  for (int32_t v : {10, 20, 30}) {
    ASSERT_OK(row.SetValue(kValueColumnId, QLValue::Primitive(v)));
    ASSERT_EQ(ASSERT_RESULT(processor.ProcessMatchedRow(row)), FetchLimit::kNotReached);
  }
  const auto fetched_rows = ASSERT_RESULT(processor.Finish());
  ASSERT_EQ(fetched_rows, 3U);
  ASSERT_OK(PatchFetchedRows(fetched_rows));
  EXPECT_EQ(result_buffer_.ToBuffer(), ExpectedRowsInt32Buffer(3, {10, 20, 30}));
}

// Group key types PgValue::ToQLValuePB cannot convert (UINT8, UINT16) would LOG(FATAL) when the
// first group is built; YSQL never sends them. Grouping by VECTOR is not supported. Init rejects
// them before any row is read.
TEST_F(PgsqlReadResultTest, UnsupportedGroupKeyTypesAreRejected) {
  const ColumnId kKeyColumnId = 0_ColId;
  for (const auto type : {DataType::UINT8, DataType::UINT16, DataType::VECTOR}) {
    SCOPED_TRACE(Format("type: $0", type));
    schema_ = Schema(
        {ColumnSchema("key", type, ColumnKind::RANGE_ASC_NULL_FIRST)}, {kKeyColumnId});
    projection_ = dockv::ReaderProjection(schema_, {kKeyColumnId.rep()});

    read_req_.Clear();
    read_req_.set_is_aggregate(true);
    read_req_.set_schema_version(0);
    read_req_.add_col_refs()->set_column_id(kKeyColumnId);
    read_req_.add_targets()->set_column_id(kKeyColumnId);
    auto* count_star = read_req_.add_targets()->mutable_tscall();
    count_star->set_opcode(static_cast<int32_t>(bfpg::TSOpcode::kCount));
    count_star->add_operands()->mutable_value()->set_int32_value(1);

    PgsqlReadAggregateProcessor processor(read_req_, &result_buffer_, nullptr);
    ASSERT_NOK_STR_CONTAINS(
        processor.Init(projection_), Format("Group key 0 has unsupported type $0", type));
  }
}

}  // namespace
}  // namespace yb::docdb
