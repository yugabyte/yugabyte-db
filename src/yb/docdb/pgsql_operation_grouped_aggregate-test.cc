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

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "yb/bfpg/tserver_opcodes.h"

#include "yb/common/pgsql_protocol.messages.h"
#include "yb/common/schema.h"

#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/docdb_test_base.h"
#include "yb/docdb/pgsql_operation.h"
#include "yb/docdb/ql_rocksdb_storage.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/doc_path.h"
#include "yb/dockv/key_entry_value.h"

#include "yb/util/monotime.h"
#include "yb/util/size_literals.h"
#include "yb/util/test_macros.h"
#include "yb/util/uuid.h"

#include "yb/yql/pggate/util/pg_doc_data.h"

using namespace yb::size_literals;

namespace yb::docdb {
namespace {

// Primary key columns.
const ColumnId kGi2 = 0_ColId, kGi4 = 1_ColId, kGf = 2_ColId, kGs = 3_ColId, kR = 4_ColId;
// Aggregate columns.
const ColumnId kI2 = 5_ColId, kI4 = 6_ColId, kI8 = 7_ColId, kF = 8_ColId, kD = 9_ColId;
const ColumnId kS = 10_ColId, kBo = 11_ColId;
const TableId kAllTypesTableId = "grouped_agg_all_types_table";

struct AggPayload {
  std::optional<int16_t> i2;
  std::optional<int32_t> i4;
  std::optional<int64_t> i8;
  std::optional<float> f;
  std::optional<double> d;
  std::optional<std::string> s;
  std::optional<bool> bo;
};

struct AllTypesRow {
  int16_t gi2 = 0;
  int32_t gi4 = 0;
  float gf = 0.0f;
  std::string gs;
  int32_t r = 0;
  AggPayload p;
};

void SetPb(QLValuePB& pb, int16_t value) { pb.set_int16_value(value); }
void SetPb(QLValuePB& pb, int32_t value) { pb.set_int32_value(value); }
void SetPb(QLValuePB& pb, int64_t value) { pb.set_int64_value(value); }
void SetPb(QLValuePB& pb, float value) { pb.set_float_value(value); }
void SetPb(QLValuePB& pb, double value) { pb.set_double_value(value); }
void SetPb(QLValuePB& pb, bool value) { pb.set_bool_value(value); }
void SetPb(QLValuePB& pb, const std::string& value) { pb.set_string_value(value); }

template <class T>
QLValuePB ValuePb(const T& value) {
  QLValuePB pb;
  SetPb(pb, value);
  return pb;
}

template <class T>
QLValuePB ValuePb(const std::optional<T>& value) {
  QLValuePB pb;
  if (value) {
    SetPb(pb, *value);
  }
  return pb;
}

QLValuePB BinaryPb(std::string value) {
  QLValuePB pb;
  pb.set_binary_value(std::move(value));
  return pb;
}

dockv::KeyEntryValue KeyFrom(const QLValuePB& value) {
  return dockv::KeyEntryValue::FromQLValuePBForKey(value, SortingType::kAscending);
}

dockv::KeyEntryValues TableKey(const AllTypesRow& row) {
  return dockv::KeyEntryValues{
      KeyFrom(ValuePb(row.gi2)), KeyFrom(ValuePb(row.gi4)), KeyFrom(ValuePb(row.gf)),
      KeyFrom(ValuePb(row.gs)), KeyFrom(ValuePb(row.r))};
}

AggPayload BulkPayload(int16_t a, int32_t r) {
  return AggPayload{
      .i2 = a, .i4 = r, .i8 = static_cast<int64_t>(a) * 1000 + r, .f = 1.0f, .d = 2.0,
      .s = r < 5 ? std::string("a") : std::string("z"), .bo = (r % 2) == 0};
}

std::vector<AllTypesRow> MakeAllTypesInputRows() {
  const auto kI16Min = std::numeric_limits<int16_t>::min();
  const auto kI16Max = std::numeric_limits<int16_t>::max();
  const auto kI32Min = std::numeric_limits<int32_t>::min();
  const auto kI32Max = std::numeric_limits<int32_t>::max();
  const auto kI64Min = std::numeric_limits<int64_t>::min();
  const auto kI64Max = std::numeric_limits<int64_t>::max();

  std::vector<AllTypesRow> input_rows{
      {.gi2 = 0, .gi4 = 0, .gf = 0.0f, .gs = std::string(""), .r = 0,
       .p = {.i2 = 0, .i4 = 0, .i8 = 0, .f = 0.0f, .d = 0.0, .s = std::string(""), .bo = false}},
      {.gi2 = 0, .gi4 = 0, .gf = 0.0f, .gs = std::string("x"), .r = 0,
       .p = {.i2 = kI16Min, .i4 = kI32Min, .i8 = kI64Min, .f = -0.0f, .d = -1.0e10,
             .s = std::string("a"), .bo = true}},
      {.gi2 = 0, .gi4 = 0, .gf = 0.0f, .gs = std::string("x"), .r = 1,
       .p = {.i2 = kI16Max, .i4 = kI32Max, .i8 = kI64Max, .f = 1.0e10f, .d = 1.0e10,
             .s = std::string("z"), .bo = false}},
      {.gi2 = 0, .gi4 = 0, .gf = 1.0f, .gs = std::string(""), .r = 0, .p = {}},
      {.gi2 = 0, .gi4 = 0, .gf = 1.0f, .gs = std::string(""), .r = 1,
       .p = {.i2 = 5, .i4 = std::nullopt, .i8 = 8, .f = 1.5f, .d = std::nullopt,
             .s = std::string("m"), .bo = std::nullopt}},
      {.gi2 = 0, .gi4 = 0, .gf = 1.0f, .gs = std::string(""), .r = 2,
       .p = {.i2 = std::nullopt, .i4 = 7, .i8 = std::nullopt, .f = std::nullopt, .d = 2.5,
             .s = std::nullopt, .bo = true}},
      {.gi2 = 0, .gi4 = 1, .gf = 0.0f, .gs = std::string(""), .r = 0, .p = {}},
  };
  for (int16_t a = 1; a <= 10; ++a) {
    for (int32_t r = 0; r < 10; ++r) {
      input_rows.push_back(
          {.gi2 = a, .gi4 = 0, .gf = 0.0f, .gs = std::string(""), .r = r, .p = BulkPayload(a, r)});
    }
  }
  return input_rows;
}

// Target order: key_s, key_i2, key_i4, key_f, then SUM/MIN/MAX/COUNT as in AddAllTypesAggTargets.
struct AllTypesResultRow {
  std::optional<std::string> key_s;
  std::optional<int16_t> key_i2;
  std::optional<int32_t> key_i4;
  std::optional<float> key_f;
  std::optional<int64_t> sum_i2, sum_i4, sum_i8;
  std::optional<float> sum_f;
  std::optional<double> sum_d;
  std::optional<int16_t> min_i2, max_i2;
  std::optional<int32_t> min_i4, max_i4;
  std::optional<int64_t> min_i8, max_i8;
  std::optional<float> min_f, max_f;
  std::optional<double> min_d, max_d;
  std::optional<std::string> min_s, max_s;
  std::optional<int64_t> count_i4, count_s, count_bo, count_star;

  bool operator==(const AllTypesResultRow&) const = default;
};

AllTypesResultRow BulkExpected(int16_t a) {
  return AllTypesResultRow{
      .key_s = std::string(""), .key_i2 = a, .key_i4 = 0, .key_f = 0.0f,
      .sum_i2 = static_cast<int64_t>(a) * 10, .sum_i4 = 45,
      .sum_i8 = static_cast<int64_t>(a) * 10000 + 45, .sum_f = 10.0f, .sum_d = 20.0,
      .min_i2 = a, .max_i2 = a, .min_i4 = 0, .max_i4 = 9,
      .min_i8 = static_cast<int64_t>(a) * 1000, .max_i8 = static_cast<int64_t>(a) * 1000 + 9,
      .min_f = 1.0f, .max_f = 1.0f, .min_d = 2.0, .max_d = 2.0, .min_s = std::string("a"),
      .max_s = std::string("z"), .count_i4 = 10, .count_s = 10, .count_bo = 10, .count_star = 10};
}

std::vector<AllTypesResultRow> MakeAllTypesExpected() {
  const auto kI16Min = std::numeric_limits<int16_t>::min();
  const auto kI16Max = std::numeric_limits<int16_t>::max();
  const auto kI32Min = std::numeric_limits<int32_t>::min();
  const auto kI32Max = std::numeric_limits<int32_t>::max();
  const auto kI64Min = std::numeric_limits<int64_t>::min();
  const auto kI64Max = std::numeric_limits<int64_t>::max();

  std::vector<AllTypesResultRow> expected{
      {.key_s = std::string(""), .key_i2 = 0, .key_i4 = 0, .key_f = 0.0f, .sum_i2 = 0, .sum_i4 = 0,
       .sum_i8 = 0, .sum_f = 0.0f, .sum_d = 0.0, .min_i2 = 0,
       .max_i2 = 0, .min_i4 = 0, .max_i4 = 0, .min_i8 = 0, .max_i8 = 0, .min_f = 0.0f,
       .max_f = 0.0f, .min_d = 0.0, .max_d = 0.0, .min_s = std::string(""),
       .max_s = std::string(""), .count_i4 = 1, .count_s = 1, .count_bo = 1, .count_star = 1},
      {.key_s = std::string("x"), .key_i2 = 0, .key_i4 = 0, .key_f = 0.0f, .sum_i2 = -1,
       .sum_i4 = -1, .sum_i8 = -1, .sum_f = 1.0e10f, .sum_d = 0.0,
       .min_i2 = kI16Min, .max_i2 = kI16Max, .min_i4 = kI32Min, .max_i4 = kI32Max,
       .min_i8 = kI64Min, .max_i8 = kI64Max, .min_f = -0.0f, .max_f = 1.0e10f, .min_d = -1.0e10,
       .max_d = 1.0e10, .min_s = std::string("a"), .max_s = std::string("z"), .count_i4 = 2,
       .count_s = 2, .count_bo = 2, .count_star = 2},
      {.key_s = std::string(""), .key_i2 = 0, .key_i4 = 0, .key_f = 1.0f, .sum_i2 = 5, .sum_i4 = 7,
       .sum_i8 = 8, .sum_f = 1.5f, .sum_d = 2.5, .min_i2 = 5,
       .max_i2 = 5, .min_i4 = 7, .max_i4 = 7, .min_i8 = 8, .max_i8 = 8, .min_f = 1.5f,
       .max_f = 1.5f, .min_d = 2.5, .max_d = 2.5, .min_s = std::string("m"),
       .max_s = std::string("m"), .count_i4 = 1, .count_s = 1, .count_bo = 1, .count_star = 3},
      {.key_s = std::string(""), .key_i2 = 0, .key_i4 = 1, .key_f = 0.0f, .sum_i2 = std::nullopt,
       .sum_i4 = std::nullopt, .sum_i8 = std::nullopt, .sum_f = std::nullopt, .sum_d = std::nullopt,
       .min_i2 = std::nullopt, .max_i2 = std::nullopt, .min_i4 = std::nullopt,
       .max_i4 = std::nullopt, .min_i8 = std::nullopt,
       .max_i8 = std::nullopt, .min_f = std::nullopt, .max_f = std::nullopt, .min_d = std::nullopt,
       .max_d = std::nullopt, .min_s = std::nullopt, .max_s = std::nullopt,
       .count_i4 = std::nullopt, .count_s = std::nullopt, .count_bo = std::nullopt,
       .count_star = 1},
  };
  for (int16_t a = 1; a <= 10; ++a) {
    expected.push_back(BulkExpected(a));
  }
  return expected;
}

template <class T>
Result<std::optional<T>> ReadOptionalNumber(Slice* cursor) {
  if (pggate::PgDocData::ReadHeaderIsNull(cursor)) {
    return std::nullopt;
  }
  return pggate::PgDocData::ReadNumber<T>(cursor);
}

Result<std::optional<std::string>> ReadOptionalString(Slice* cursor) {
  if (pggate::PgDocData::ReadHeaderIsNull(cursor)) {
    return std::nullopt;
  }
  const auto encoded_length = VERIFY_RESULT(pggate::PgDocData::CheckedReadNumber<uint64_t>(cursor));
  SCHECK_GT(encoded_length, 0U, Corruption, "Empty string encoding");
  const auto string_length = encoded_length - 1;
  SCHECK_GE(cursor->size(), string_length + 1, Corruption, "Truncated string column");
  std::string value;
  pggate::PgDocData::ReadString(cursor, &value, string_length);
  SCHECK_EQ(cursor->consume_byte(), 0, Corruption, "Missing string null terminator");
  return value;
}

Result<std::vector<AllTypesResultRow>> ParseAllTypesResponse(
    size_t fetched_rows, const WriteBuffer& result_buffer) {
  const auto buffer = result_buffer.ToBuffer();
  Slice cursor(buffer);
  int64_t row_count = 0;
  pggate::PgDocData::LoadCache(cursor, &row_count, &cursor);
  SCHECK_EQ(
      row_count, static_cast<int64_t>(fetched_rows), Corruption,
      "Row count prefix mismatch in all-types grouped aggregate response");

  std::vector<AllTypesResultRow> rows;
  rows.reserve(fetched_rows);
  for (int64_t i = 0; i < row_count; ++i) {
    AllTypesResultRow row;
    row.key_s = VERIFY_RESULT(ReadOptionalString(&cursor));
    row.key_i2 = VERIFY_RESULT(ReadOptionalNumber<int16_t>(&cursor));
    row.key_i4 = VERIFY_RESULT(ReadOptionalNumber<int32_t>(&cursor));
    row.key_f = VERIFY_RESULT(ReadOptionalNumber<float>(&cursor));
    row.sum_i2 = VERIFY_RESULT(ReadOptionalNumber<int64_t>(&cursor));
    row.sum_i4 = VERIFY_RESULT(ReadOptionalNumber<int64_t>(&cursor));
    row.sum_i8 = VERIFY_RESULT(ReadOptionalNumber<int64_t>(&cursor));
    row.sum_f = VERIFY_RESULT(ReadOptionalNumber<float>(&cursor));
    row.sum_d = VERIFY_RESULT(ReadOptionalNumber<double>(&cursor));
    row.min_i2 = VERIFY_RESULT(ReadOptionalNumber<int16_t>(&cursor));
    row.max_i2 = VERIFY_RESULT(ReadOptionalNumber<int16_t>(&cursor));
    row.min_i4 = VERIFY_RESULT(ReadOptionalNumber<int32_t>(&cursor));
    row.max_i4 = VERIFY_RESULT(ReadOptionalNumber<int32_t>(&cursor));
    row.min_i8 = VERIFY_RESULT(ReadOptionalNumber<int64_t>(&cursor));
    row.max_i8 = VERIFY_RESULT(ReadOptionalNumber<int64_t>(&cursor));
    row.min_f = VERIFY_RESULT(ReadOptionalNumber<float>(&cursor));
    row.max_f = VERIFY_RESULT(ReadOptionalNumber<float>(&cursor));
    row.min_d = VERIFY_RESULT(ReadOptionalNumber<double>(&cursor));
    row.max_d = VERIFY_RESULT(ReadOptionalNumber<double>(&cursor));
    row.min_s = VERIFY_RESULT(ReadOptionalString(&cursor));
    row.max_s = VERIFY_RESULT(ReadOptionalString(&cursor));
    row.count_i4 = VERIFY_RESULT(ReadOptionalNumber<int64_t>(&cursor));
    row.count_s = VERIFY_RESULT(ReadOptionalNumber<int64_t>(&cursor));
    row.count_bo = VERIFY_RESULT(ReadOptionalNumber<int64_t>(&cursor));
    row.count_star = VERIFY_RESULT(ReadOptionalNumber<int64_t>(&cursor));
    rows.push_back(std::move(row));
  }
  SCHECK(cursor.empty(), Corruption, "Trailing bytes in all-types grouped aggregate response");
  return rows;
}

void AddAggTarget(LWPgsqlReadRequestPB* read_req, bfpg::TSOpcode opcode, ColumnId col) {
  auto* tscall = read_req->add_targets()->mutable_tscall();
  tscall->set_opcode(static_cast<int32_t>(opcode));
  tscall->add_operands()->set_column_id(col);
}

// COUNT(*): pggate sends a constant operand, not a column.
void AddCountStarTarget(LWPgsqlReadRequestPB* read_req) {
  auto* tscall = read_req->add_targets()->mutable_tscall();
  tscall->set_opcode(static_cast<int32_t>(bfpg::TSOpcode::kCount));
  tscall->add_operands()->mutable_value()->set_int32_value(1);
}

void AddAllTypesAggTargets(LWPgsqlReadRequestPB* read_req) {
  AddAggTarget(read_req, bfpg::TSOpcode::kSumInt16, kI2);
  AddAggTarget(read_req, bfpg::TSOpcode::kSumInt32, kI4);
  AddAggTarget(read_req, bfpg::TSOpcode::kSumInt64, kI8);
  AddAggTarget(read_req, bfpg::TSOpcode::kSumFloat, kF);
  AddAggTarget(read_req, bfpg::TSOpcode::kSumDouble, kD);
  AddAggTarget(read_req, bfpg::TSOpcode::kMin, kI2);
  AddAggTarget(read_req, bfpg::TSOpcode::kMax, kI2);
  AddAggTarget(read_req, bfpg::TSOpcode::kMin, kI4);
  AddAggTarget(read_req, bfpg::TSOpcode::kMax, kI4);
  AddAggTarget(read_req, bfpg::TSOpcode::kMin, kI8);
  AddAggTarget(read_req, bfpg::TSOpcode::kMax, kI8);
  AddAggTarget(read_req, bfpg::TSOpcode::kMin, kF);
  AddAggTarget(read_req, bfpg::TSOpcode::kMax, kF);
  AddAggTarget(read_req, bfpg::TSOpcode::kMin, kD);
  AddAggTarget(read_req, bfpg::TSOpcode::kMax, kD);
  AddAggTarget(read_req, bfpg::TSOpcode::kMin, kS);
  AddAggTarget(read_req, bfpg::TSOpcode::kMax, kS);
  AddAggTarget(read_req, bfpg::TSOpcode::kCount, kI4);
  AddAggTarget(read_req, bfpg::TSOpcode::kCount, kS);
  AddAggTarget(read_req, bfpg::TSOpcode::kCount, kBo);
  AddCountStarTarget(read_req);
}

Result<size_t> ExecuteRead(
    const PgsqlReadOperationData& op_data, LWPgsqlResponsePB& response,
    WriteBuffer* result_buffer) {
  ReadRestartData read_restart_data;
  PgsqlReadOperation read_op(op_data, response, result_buffer, &read_restart_data);
  return read_op.Execute();
}

}  // namespace

class GroupedAggregateAllTypesTest : public DocDBTestBase {
 protected:
  Schema CreateSchema() override {
    return Schema(
        {
            ColumnSchema("gi2", DataType::INT16, ColumnKind::RANGE_ASC_NULL_FIRST),
            ColumnSchema("gi4", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
            ColumnSchema("gf", DataType::FLOAT, ColumnKind::RANGE_ASC_NULL_FIRST),
            ColumnSchema("gs", DataType::STRING, ColumnKind::RANGE_ASC_NULL_FIRST),
            ColumnSchema("r", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
            ColumnSchema("i2", DataType::INT16, ColumnKind::VALUE, Nullable::kTrue),
            ColumnSchema("i4", DataType::INT32, ColumnKind::VALUE, Nullable::kTrue),
            ColumnSchema("i8", DataType::INT64, ColumnKind::VALUE, Nullable::kTrue),
            ColumnSchema("f", DataType::FLOAT, ColumnKind::VALUE, Nullable::kTrue),
            ColumnSchema("d", DataType::DOUBLE, ColumnKind::VALUE, Nullable::kTrue),
            ColumnSchema("s", DataType::STRING, ColumnKind::VALUE, Nullable::kTrue),
            ColumnSchema("bo", DataType::BOOL, ColumnKind::VALUE, Nullable::kTrue),
        },
        {kGi2, kGi4, kGf, kGs, kR, kI2, kI4, kI8, kF, kD, kS, kBo});
  }

  void SetUp() override {
    DocDBTestBase::SetUp();
    schema_ = CreateSchema();
    doc_read_context_.emplace(DocReadContext::TEST_Create(schema_));
    ql_storage_.emplace("", doc_db(), EncodedPartitionBounds{});
    read_operation_data_ = ReadOperationData::FromSingleReadTime(HybridTime::FromMicros(2'000'000));
  }

  Status WritePayload(
      Slice doc_key, const AggPayload& payload, HybridTime ht, ColumnId i2, ColumnId i4,
      ColumnId i8, ColumnId f, ColumnId d, ColumnId s, ColumnId bo) {
    auto write = [&](ColumnId col, const QLValuePB& value) -> Status {
      if (value.value_case() == QLValuePB::VALUE_NOT_SET) {
        return Status::OK();
      }
      return SetPrimitive(
          dockv::DocPath(doc_key, dockv::KeyEntryValue::MakeColumnId(col)), value, ht);
    };
    RETURN_NOT_OK(write(i2, ValuePb(payload.i2)));
    RETURN_NOT_OK(write(i4, ValuePb(payload.i4)));
    RETURN_NOT_OK(write(i8, ValuePb(payload.i8)));
    RETURN_NOT_OK(write(f, ValuePb(payload.f)));
    RETURN_NOT_OK(write(d, ValuePb(payload.d)));
    RETURN_NOT_OK(write(s, ValuePb(payload.s)));
    return write(bo, ValuePb(payload.bo));
  }

  Status WriteTableRow(const AllTypesRow& row, int32_t ordinal) {
    const auto ht = HybridTime::FromMicros(1000 + ordinal);
    const dockv::DocKey doc_key(schema_, TableKey(row));
    const auto encoded = doc_key.Encode();
    // The liveness column keeps a row whose value columns are all null.
    RETURN_NOT_OK(SetPrimitive(
        dockv::DocPath(encoded, dockv::KeyEntryValue::kLivenessColumn),
        ValueRef(dockv::ValueEntryType::kNullLow), ht));
    return WritePayload(encoded, row.p, ht, kI2, kI4, kI8, kF, kD, kS, kBo);
  }

  std::string YbctidOf(const AllTypesRow& row) const {
    return dockv::DocKey(schema_, TableKey(row)).Encode().ToStringBuffer();
  }

  void WriteTableData(const std::vector<AllTypesRow>& rows) {
    for (size_t i = 0; i < rows.size(); ++i) {
      ASSERT_OK(WriteTableRow(rows[i], static_cast<int32_t>(i)));
    }
    ASSERT_OK(FlushRocksDbAndWait(rocksdb::FlushReason::kTestOnly));
  }

  void AddAllTypesRequestFields(LWPgsqlReadRequestPB* read_req) {
    read_req->set_is_aggregate(true);
    read_req->dup_table_id(kAllTypesTableId);
    read_req->set_schema_version(0);
    for (const auto& id : {kGs, kGi2, kGi4, kGf, kI2, kI4, kI8, kF, kD, kS, kBo}) {
      read_req->add_col_refs()->set_column_id(id);
    }
    // 4 out of 5 primary key columns are used as group keys.
    read_req->add_targets()->set_column_id(kGs);
    read_req->add_targets()->set_column_id(kGi2);
    read_req->add_targets()->set_column_id(kGi4);
    read_req->add_targets()->set_column_id(kGf);
    AddAllTypesAggTargets(read_req);
  }

  PgsqlReadOperationData ReadOpData(
      const PgsqlReadRequestMsg& request,
      const DocReadContext* index_doc_read_context = nullptr) const {
    return PgsqlReadOperationData{
        .read_operation_data = read_operation_data_, .is_explicit_request_read_time = false,
        .request = request, .doc_read_context = *doc_read_context_,
        .index_doc_read_context = index_doc_read_context,
        .txn_op_context = kNonTransactionalOperationContext, .ql_storage = *ql_storage_,
        .pending_op = pending_op_, .vector_index = nullptr,
    };
  }

  Schema schema_;
  std::optional<DocReadContext> doc_read_context_;
  ReadOperationData read_operation_data_;
  std::optional<QLRocksDBStorage> ql_storage_;
  ScopedRWOperation pending_op_{ScopedRWOperation::TEST_Create()};
};

// The four leading key columns, in primary-key order. Two rows of one group fold.
TEST_F(GroupedAggregateAllTypesTest, YbctidBatchGroupsLeadingPrimaryKey) {
  const AllTypesRow first{
      .gi2 = 1, .gi4 = 2, .gf = 3.0f, .gs = std::string("g"), .r = 0,
      .p = {.i2 = 4, .i4 = 5, .i8 = 6, .f = 1.0f, .d = 1.0, .s = std::string("a"), .bo = true}};
  auto second = first;
  second.r = 1;
  second.p.i2 = 7;
  WriteTableData({first, second});

  ThreadSafeArena arena;
  LWPgsqlReadRequestPB read_req(&arena);
  LWPgsqlResponsePB response(&arena);
  AddAllTypesRequestFields(&read_req);
  for (const auto& row : {first, second}) {
    read_req.add_batch_arguments()->mutable_ybctid()->mutable_value()->dup_binary_value(
        YbctidOf(row));
  }

  WriteBuffer result_buffer(8192);
  const auto rows = ASSERT_RESULT(
      ParseAllTypesResponse(
          ASSERT_RESULT(ExecuteRead(ReadOpData(read_req), response, &result_buffer)),
          result_buffer));
  ASSERT_EQ(rows.size(), 1U);
  EXPECT_EQ(rows[0].count_star, std::optional<int64_t>(2));
  EXPECT_EQ(rows[0].key_i2, std::optional<int16_t>(1));
  EXPECT_EQ(rows[0].sum_i2, std::optional<int64_t>(11));
}

// An order field is the secondary index's row order. Grouping by r still runs: DocDB does
// not check that the batch order matches the group keys.
TEST_F(GroupedAggregateAllTypesTest, YbctidBatchTrustsIndexOrder) {
  const AllTypesRow row{
      .gi2 = 1, .gi4 = 2, .gf = 3.0f, .gs = std::string("g"), .r = 0,
      .p = {.i2 = 4, .i4 = 5, .i8 = 6, .f = 1.0f, .d = 1.0, .s = std::string("a"), .bo = true}};
  WriteTableData({row});

  ThreadSafeArena arena;
  LWPgsqlReadRequestPB read_req(&arena);
  LWPgsqlResponsePB response(&arena);
  read_req.set_is_aggregate(true);
  read_req.dup_table_id(kAllTypesTableId);
  read_req.set_schema_version(0);
  read_req.add_col_refs()->set_column_id(kR);
  read_req.add_targets()->set_column_id(kR);
  AddCountStarTarget(&read_req);
  auto* arg = read_req.add_batch_arguments();
  arg->set_order(0);
  arg->mutable_ybctid()->mutable_value()->dup_binary_value(YbctidOf(row));

  WriteBuffer result_buffer(256);
  EXPECT_EQ(ASSERT_RESULT(ExecuteRead(ReadOpData(read_req), response, &result_buffer)), 1U);
}

// Three groups of two rows, read as one ybctid batch with a 1-byte size limit. Each page folds
// one group and stops at the next group's first row. The ybctid provider sets batch_arg_count to
// that row's position, and the next page resends the batch from there, as pggate does. Every
// group comes back once, with both its rows.
TEST_F(GroupedAggregateAllTypesTest, YbctidBatchPagesOnSizeLimit) {
  std::vector<AllTypesRow> input_rows;
  for (int16_t a = 1; a <= 3; ++a) {
    for (int32_t r = 0; r < 2; ++r) {
      input_rows.push_back(
          {.gi2 = a, .gi4 = 0, .gf = 0.0f, .gs = std::string(""), .r = r, .p = BulkPayload(a, r)});
    }
  }
  WriteTableData(input_rows);

  std::vector<std::optional<int16_t>> keys;
  std::vector<std::optional<int64_t>> counts;
  std::vector<size_t> batch_arg_counts;
  size_t start = 0;
  for (int page = 0; page < 10 && start < input_rows.size(); ++page) {
    ThreadSafeArena arena;
    LWPgsqlReadRequestPB read_req(&arena);
    LWPgsqlResponsePB response(&arena);
    AddAllTypesRequestFields(&read_req);
    read_req.set_size_limit(1);
    for (size_t i = start; i < input_rows.size(); ++i) {
      read_req.add_batch_arguments()->mutable_ybctid()->mutable_value()->dup_binary_value(
          YbctidOf(input_rows[i]));
    }

    WriteBuffer result_buffer(8192);
    const auto rows = ASSERT_RESULT(ParseAllTypesResponse(
        ASSERT_RESULT(ExecuteRead(ReadOpData(read_req), response, &result_buffer)),
        result_buffer));
    ASSERT_EQ(rows.size(), 1U);
    keys.push_back(rows[0].key_i2);
    counts.push_back(rows[0].count_star);
    if (!response.has_paging_state()) {
      break;
    }
    batch_arg_counts.push_back(response.batch_arg_count());
    start += response.batch_arg_count();
  }
  EXPECT_EQ(keys, (std::vector<std::optional<int16_t>>{1, 2, 3}));
  EXPECT_EQ(counts, (std::vector<std::optional<int64_t>>{2, 2, 2}));
  EXPECT_EQ(batch_arg_counts, (std::vector<size_t>{2, 2}));
}

TEST_F(GroupedAggregateAllTypesTest, VectorSearchIsRejected) {
  ThreadSafeArena arena;
  LWPgsqlReadRequestPB read_req(&arena);
  LWPgsqlResponsePB response(&arena);
  AddAllTypesRequestFields(&read_req);
  read_req.mutable_index_request()->mutable_vector_idx_options();

  WriteBuffer result_buffer(64);
  ASSERT_NOK_STR_CONTAINS(
      ExecuteRead(ReadOpData(read_req), response, &result_buffer),
      "Grouped aggregate read cannot use vector search");
}

// Plain SUM(i2) with i2 missing from col_refs. Plain aggregates used to read the operand through
// PgTableRow::GetQLValuePB, which passed kNotFoundIndex to GetValueByIndex: a DCHECK in debug and
// an out-of-bounds read in release. Init now resolves every operand against the projection and
// fails the read.
TEST_F(GroupedAggregateAllTypesTest, PlainAggregateOperandOutsideProjectionIsRejected) {
  const AllTypesRow row{
      .gi2 = 1, .gi4 = 2, .gf = 3.0f, .gs = std::string("g"), .r = 0,
      .p = {.i2 = 4, .i4 = 5, .i8 = 6, .f = 1.0f, .d = 1.0, .s = std::string("a"), .bo = true}};
  WriteTableData({row});

  ThreadSafeArena arena;
  LWPgsqlReadRequestPB read_req(&arena);
  LWPgsqlResponsePB response(&arena);
  read_req.set_is_aggregate(true);
  read_req.dup_table_id(kAllTypesTableId);
  read_req.set_schema_version(0);
  read_req.add_col_refs()->set_column_id(kI4);
  AddAggTarget(&read_req, bfpg::TSOpcode::kSumInt16, kI2);

  WriteBuffer result_buffer(256);
  ASSERT_NOK_STR_CONTAINS(
      ExecuteRead(ReadOpData(read_req), response, &result_buffer),
      Format("Column $0 is not present in row projection", kI2));
}

// ------------------------------------------------------------------------------------------------
// PkAllTypes: GROUP BY gs, gi2, gi4, gf on PK (gi2, gi4, gf, gs, r).
// Group keys: INT16, INT32, FLOAT, STRING. r is the within-group PK suffix.
//
//            SUM     MIN     MAX     COUNT
//   INT16     y       y       y       x
//   INT32     y       y       y       y
//   INT64     y       y       y       x
//   FLOAT     y       y       y       x
//   DOUBLE    y       y       y       x
//   STRING    x       y       y       y
//   BOOL      x       x       x       y
//   *         x       x       x       y   (COUNT(*))
//
// Also: null agg values, zeros, type min/max, empty string, and all-null payload.
// ------------------------------------------------------------------------------------------------

class GroupedAggregatePkAllTypesTest : public GroupedAggregateAllTypesTest {
 protected:
  Result<std::vector<AllTypesResultRow>> ExecutePkRead(
      bool return_paging_state, LWPgsqlResponsePB* response, uint64_t size_limit = 1_MB) {
    ThreadSafeArena arena;
    LWPgsqlReadRequestPB read_req(&arena);
    AddAllTypesRequestFields(&read_req);
    read_req.set_limit(200);
    read_req.set_size_limit(size_limit);
    read_req.set_return_paging_state(return_paging_state);

    WriteBuffer result_buffer(8192);
    const auto fetched_rows =
        VERIFY_RESULT(ExecuteRead(ReadOpData(read_req), *response, &result_buffer));
    return ParseAllTypesResponse(fetched_rows, result_buffer);
  }
};

TEST_F(GroupedAggregatePkAllTypesTest, PkAllTypes) {
  const auto input_rows = MakeAllTypesInputRows();
  ASSERT_GE(input_rows.size(), 100U);
  WriteTableData(input_rows);

  ThreadSafeArena arena;
  LWPgsqlResponsePB response(&arena);
  const auto rows = ASSERT_RESULT(ExecutePkRead(/*return_paging_state=*/false, &response));
  EXPECT_EQ(rows, MakeAllTypesExpected());
}

// Fourteen groups and a limit of three. Each full page returns three groups, and the resume
// key walks the rest. The input rows are wide enough that a separate size limit also pages.
TEST_F(GroupedAggregatePkAllTypesTest, RowLimitPagesThroughAllGroups) {
  WriteTableData(MakeAllTypesInputRows());
  const auto expected = MakeAllTypesExpected();
  ASSERT_GT(expected.size(), 3U);

  std::vector<AllTypesResultRow> got;
  std::string next_row_key;
  for (int page = 0; page < 20; ++page) {
    ThreadSafeArena arena;
    LWPgsqlReadRequestPB read_req(&arena);
    LWPgsqlResponsePB response(&arena);
    AddAllTypesRequestFields(&read_req);
    read_req.set_limit(3);
    read_req.set_size_limit(1_MB);
    read_req.set_return_paging_state(true);
    if (!next_row_key.empty()) {
      read_req.mutable_paging_state()->dup_next_row_key(next_row_key);
    }

    WriteBuffer result_buffer(8192);
    const auto fetched_rows =
        ASSERT_RESULT(ExecuteRead(ReadOpData(read_req), response, &result_buffer));
    const auto rows = ASSERT_RESULT(ParseAllTypesResponse(fetched_rows, result_buffer));
    ASSERT_FALSE(rows.empty());
    got.insert(got.end(), rows.begin(), rows.end());
    if (!response.has_paging_state() || response.paging_state().next_row_key().empty()) {
      break;
    }
    EXPECT_EQ(rows.size(), 3U);
    const auto key = response.paging_state().next_row_key();
    next_row_key.assign(key.cdata(), key.size());
  }
  EXPECT_EQ(got, expected);
}

// size_limit 512 returns a prefix of the groups and a resume key. Later pages are not read.
TEST_F(GroupedAggregatePkAllTypesTest, SizeLimit) {
  WriteTableData(MakeAllTypesInputRows());

  ThreadSafeArena arena;
  LWPgsqlResponsePB response(&arena);
  const auto rows = ASSERT_RESULT(ExecutePkRead(
      /*return_paging_state=*/true, &response, /*size_limit=*/512));
  const auto expected = MakeAllTypesExpected();
  ASSERT_GE(rows.size(), 1U);
  ASSERT_LT(rows.size(), expected.size());
  EXPECT_EQ(
      std::vector<AllTypesResultRow>(expected.begin(), expected.begin() + rows.size()), rows);
  ASSERT_TRUE(response.has_paging_state());
  EXPECT_FALSE(response.paging_state().next_row_key().empty());
}

// Past deadline folds one row, emits one group, and sets paging_state.
TEST_F(GroupedAggregatePkAllTypesTest, ScanDeadline) {
  WriteTableData(MakeAllTypesInputRows());
  read_operation_data_.deadline = CoarseMonoClock::now() - MonoDelta::FromSeconds(10);

  ThreadSafeArena arena;
  LWPgsqlResponsePB response(&arena);
  const auto rows = ASSERT_RESULT(ExecutePkRead(/*return_paging_state=*/true, &response));
  const auto expected = MakeAllTypesExpected().front();
  ASSERT_EQ(rows, std::vector<AllTypesResultRow>{expected});
  ASSERT_TRUE(response.has_paging_state());
  EXPECT_FALSE(response.paging_state().next_row_key().empty());
}

// ------------------------------------------------------------------------------------------------
// IndexAllTypes: GROUP BY gs, gi2, gi4, gf on a covering index
// (gi2, gi4, gf, gs, ybidxbasectid) INCLUDE i2, i4, i8, f, d, s, bo.
// Writes the PkAllTypes rows plus the index and expects the same groups.
// ------------------------------------------------------------------------------------------------

namespace {

const TableId kAllTypesIndexId = "grouped_agg_all_types_index";
const ColumnId kIndexGi2 = 0_ColId, kIndexGi4 = 1_ColId, kIndexGf = 2_ColId, kIndexGs = 3_ColId;
const ColumnId kIndexCtid = 4_ColId, kIndexI2 = 5_ColId, kIndexI4 = 6_ColId, kIndexI8 = 7_ColId;
const ColumnId kIndexF = 8_ColId, kIndexD = 9_ColId, kIndexS = 10_ColId, kIndexBo = 11_ColId;

}  // namespace

class GroupedAggregateIndexAllTypesTest : public GroupedAggregateAllTypesTest {
 protected:
  Schema CreateIndexSchema() {
    // 4 columns from the primary key are used as indexed columns.
    // 7 are stored in the index as covering columns.
    return Schema(
        {
            ColumnSchema("gi2", DataType::INT16, ColumnKind::RANGE_ASC_NULL_FIRST),
            ColumnSchema("gi4", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
            ColumnSchema("gf", DataType::FLOAT, ColumnKind::RANGE_ASC_NULL_FIRST),
            ColumnSchema("gs", DataType::STRING, ColumnKind::RANGE_ASC_NULL_FIRST),
            ColumnSchema("ybidxbasectid", DataType::BINARY, ColumnKind::RANGE_ASC_NULL_FIRST),
            ColumnSchema("i2", DataType::INT16, ColumnKind::VALUE, Nullable::kTrue),
            ColumnSchema("i4", DataType::INT32, ColumnKind::VALUE, Nullable::kTrue),
            ColumnSchema("i8", DataType::INT64, ColumnKind::VALUE, Nullable::kTrue),
            ColumnSchema("f", DataType::FLOAT, ColumnKind::VALUE, Nullable::kTrue),
            ColumnSchema("d", DataType::DOUBLE, ColumnKind::VALUE, Nullable::kTrue),
            ColumnSchema("s", DataType::STRING, ColumnKind::VALUE, Nullable::kTrue),
            ColumnSchema("bo", DataType::BOOL, ColumnKind::VALUE, Nullable::kTrue),
        },
        {kIndexGi2, kIndexGi4, kIndexGf, kIndexGs, kIndexCtid, kIndexI2, kIndexI4, kIndexI8,
         kIndexF, kIndexD, kIndexS, kIndexBo});
  }

  void SetUp() override {
    GroupedAggregateAllTypesTest::SetUp();
    const auto index_schema = CreateIndexSchema();
    static const auto kRegistry = std::make_shared<dockv::SchemaPackingRegistry>("TEST: ");
    index_doc_read_context_.emplace(
        "TEST: ", TableType::YQL_TABLE_TYPE, Index::kTrue, kRegistry, index_schema, 0);
    index_doc_read_context_->SetCotableId(Uuid::Generate());
  }

  Status WriteIndexRow(const AllTypesRow& row, int32_t ordinal) {
    const auto ht = HybridTime::FromMicros(1000 + ordinal);
    const auto ybctid = dockv::DocKey(schema_, TableKey(row)).Encode().ToStringBuffer();
    dockv::KeyEntryValues key_components{
        KeyFrom(ValuePb(row.gi2)), KeyFrom(ValuePb(row.gi4)), KeyFrom(ValuePb(row.gf)),
        KeyFrom(ValuePb(row.gs)), KeyFrom(BinaryPb(ybctid))};
    const dockv::DocKey index_doc_key(index_doc_read_context_->schema(), key_components);
    const auto encoded = index_doc_key.Encode();
    // The liveness column keeps a row whose covering columns are all null.
    RETURN_NOT_OK(SetPrimitive(
        dockv::DocPath(encoded, dockv::KeyEntryValue::kLivenessColumn),
        ValueRef(dockv::ValueEntryType::kNullLow), ht));
    return WritePayload(
        encoded, row.p, ht, kIndexI2, kIndexI4, kIndexI8, kIndexF, kIndexD, kIndexS, kIndexBo);
  }

  void WriteIndexData(const std::vector<AllTypesRow>& rows) {
    for (size_t i = 0; i < rows.size(); ++i) {
      ASSERT_OK(WriteIndexRow(rows[i], static_cast<int32_t>(i)));
    }
    ASSERT_OK(FlushRocksDbAndWait(rocksdb::FlushReason::kTestOnly));
  }

  Result<std::vector<AllTypesResultRow>> ExecuteIndexRead() {
    ThreadSafeArena arena;
    LWPgsqlReadRequestPB read_req(&arena);
    LWPgsqlResponsePB response(&arena);
    AddAllTypesRequestFields(&read_req);
    read_req.set_limit(200);
    read_req.set_size_limit(1_MB);

    auto* index_req = read_req.mutable_index_request();
    index_req->dup_table_id(kAllTypesIndexId);
    index_req->set_schema_version(0);
    for (const auto& id : {kIndexGi2, kIndexGi4, kIndexGf, kIndexGs, kIndexCtid}) {
      index_req->add_col_refs()->set_column_id(id);
    }
    index_req->add_targets()->set_column_id(kIndexCtid);

    WriteBuffer result_buffer(8192);
    const auto fetched_rows = VERIFY_RESULT(
        ExecuteRead(ReadOpData(read_req, &*index_doc_read_context_), response, &result_buffer));
    return ParseAllTypesResponse(fetched_rows, result_buffer);
  }

  std::optional<DocReadContext> index_doc_read_context_;
};

TEST_F(GroupedAggregateIndexAllTypesTest, IndexAllTypes) {
  const auto input_rows = MakeAllTypesInputRows();
  ASSERT_GE(input_rows.size(), 100U);
  WriteTableData(input_rows);
  WriteIndexData(input_rows);
  EXPECT_EQ(ASSERT_RESULT(ExecuteIndexRead()), MakeAllTypesExpected());
}

}  // namespace yb::docdb
