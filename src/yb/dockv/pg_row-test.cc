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

#include <gtest/gtest.h>

#include "yb/common/schema.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/dockv_test_util.h"
#include "yb/dockv/packed_row.h"
#include "yb/dockv/pg_key_decoder.h"
#include "yb/dockv/pg_row.h"
#include "yb/dockv/reader_projection.h"

#include "yb/util/format.h"
#include "yb/util/test_macros.h"

#include "yb/yql/pggate/util/pg_doc_data.h"

namespace yb::dockv {

template <class Decoder>
void RunKeyDecode(const Schema& schema, const std::vector<KeyBytes>& entries, const char* name) {
  constexpr auto kNumIterations = 4000;
  auto start = MonoTime::Now();
  ReaderProjection projection(schema);
  PgTableRow row(projection);
  Decoder decoder(schema, projection);
  for (int i = 0; i != kNumIterations; ++i) {
    for (const auto& entry : entries) {
      ASSERT_OK_FAST(decoder.Decode(entry.AsSlice(), &row));
    }
  }
  auto finish = MonoTime::Now();
  LOG(INFO) << name << " time: " << finish - start;
}

void TestKeyDecode(const std::vector<DataType>& types) {
  constexpr auto kNumEntries = 4000;

  SchemaBuilder builder;
  for (DataType type : types) {
    ASSERT_OK(builder.AddKeyColumn(Format("r$0", builder.next_column_id()), type));
  }
  auto schema = builder.Build();
  std::vector<KeyBytes> entries;
  for (int i = 0; i != kNumEntries; ++i) {
    KeyEntryValues values;
    for (DataType type : types) {
      values.push_back(KeyEntryValue::FromQLValuePB(RandomQLValue(type), SortingType::kAscending));
    }
    DocKey doc_key(values);
    entries.push_back(doc_key.Encode());
  }

  RunKeyDecode<PgKeyDecoder>(schema, entries, "v1");
}

class ColumnDecoderFactory : public PackedRowDecoderFactory {
 public:
  explicit ColumnDecoderFactory(const ReaderProjection& projection)
      : projection_(projection) {
  }

  PackedColumnDecoderEntry GetColumnDecoderV1(
      size_t projection_index, ssize_t packed_index, bool last) const override {
    return PgTableRow::GetPackedColumnDecoderV1(
        last, projection_.columns[projection_index].data_type, packed_index);
  }

  PackedColumnDecoderEntry GetColumnDecoderV2(
      size_t projection_index, ssize_t packed_index, bool last) const override {
    return PgTableRow::GetPackedColumnDecoderV2(
        last, projection_.columns[projection_index].data_type, packed_index);
  }

 private:
  const ReaderProjection& projection_;
};

void RunPackedRowDecode(
    PackedRowVersion version, const Schema& schema, const std::vector<ValueBuffer>& entries) {
  constexpr auto kNumIterations = 4000;
  auto start = MonoTime::Now();
  SchemaPacking schema_packing(TableType::PGSQL_TABLE_TYPE, schema);
  ReaderProjection projection(schema);
  PgTableRow row(projection);
  PackedRowDecoder decoder;
  {
    ColumnDecoderFactory factory(projection);
    decoder.Init(version, projection, schema_packing, factory, schema);
  }

  for (int i = 0; i != kNumIterations; ++i) {
    for (const auto& entry : entries) {
      row.Reset();
      ASSERT_OK_FAST(decoder.Apply(entry.AsSlice(), &row));
    }
  }
  auto finish = MonoTime::Now();
  LOG(INFO) << "Time: " << finish - start;
}

template <class Packer>
void TestPackedRowDecode(const std::vector<DataType>& types) {
  constexpr auto kNumEntries = 4000;

  SchemaBuilder builder;
  for (DataType type : types) {
    ASSERT_OK(builder.AddColumn(Format("v$0", builder.next_column_id()), type));
  }
  auto schema = builder.Build();
  SchemaPacking schema_packing(TableType::PGSQL_TABLE_TYPE, schema);

  std::vector<ValueBuffer> entries;
  for (int i = 0; i != kNumEntries; ++i) {
    Packer packer(0, schema_packing, std::numeric_limits<ssize_t>::max(), Slice());
    for (size_t idx = 0; idx != types.size(); ++idx) {
      ASSERT_OK(packer.AddValue(schema.column_id(idx), RandomQLValue(types[idx])));
    }
    entries.emplace_back(ASSERT_RESULT(packer.Complete()).WithoutPrefix(2));
  }

  RunPackedRowDecode(Packer::kVersion, schema, entries);
}

TEST(PgRowTest, KeyDecoderPerformance) {
  TestKeyDecode(std::vector<DataType>(10, DataType::INT32));
}

TEST(PgRowTest, PackedRowDecoderPerformanceV1) {
  TestPackedRowDecode<RowPackerV1>(std::vector<DataType>(10, DataType::INT32));
}

TEST(PgRowTest, PackedRowDecoderPerformanceV2) {
  TestPackedRowDecode<RowPackerV2>(std::vector<DataType>(10, DataType::INT32));
}

TEST(PgRowTest, ColumnWireSize) {
  SchemaBuilder builder;
  ASSERT_OK(builder.AddColumn("value", DataType::STRING));
  const auto schema = builder.Build();
  ReaderProjection projection(schema);
  PgTableRow row(projection);

  QLValuePB value;
  value.set_string_value("test value");
  ASSERT_OK(row.SetValue(schema.column_id(0), value));

  ASSERT_EQ(ASSERT_RESULT(row.ColumnWireSize(0)), ASSERT_RESULT(pggate::ColumnWireSize(value)));

  row.SetNull(0);
  ASSERT_EQ(
      ASSERT_RESULT(row.ColumnWireSize(0)), ASSERT_RESULT(pggate::ColumnWireSize(QLValuePB())));
}

// Covers the null-safe, allocation-free compare used by grouped aggregate pushdown
// (GroupKeyChanged) to detect group boundaries without building temporary QLValuePBs.
TEST(PgRowTest, EqualsQLValuePB) {
  SchemaBuilder builder;
  ASSERT_OK(builder.AddColumn("i", DataType::INT32));
  ASSERT_OK(builder.AddColumn("s", DataType::STRING));
  const auto schema = builder.Build();
  ReaderProjection projection(schema);
  PgTableRow row(projection);

  const size_t i_idx = 0;
  const size_t s_idx = 1;

  QLValuePB i_val;
  i_val.set_int32_value(42);
  QLValuePB s_val;
  s_val.set_string_value("abc");
  ASSERT_OK(row.SetValue(schema.column_id(i_idx), i_val));
  ASSERT_OK(row.SetValue(schema.column_id(s_idx), s_val));

  // Equal non-null values.
  ASSERT_TRUE(row.EqualsQLValuePB(i_idx, i_val));
  ASSERT_TRUE(row.EqualsQLValuePB(s_idx, s_val));
  ASSERT_TRUE(row.EqualsQLValuePB(i_idx, row.GetQLValuePB(schema.column_id(i_idx).rep())));
  ASSERT_TRUE(row.EqualsQLValuePB(s_idx, row.GetQLValuePB(schema.column_id(s_idx).rep())));

  // Unequal non-null values.
  QLValuePB other_i;
  other_i.set_int32_value(7);
  QLValuePB other_s;
  other_s.set_string_value("xyz");
  ASSERT_FALSE(row.EqualsQLValuePB(i_idx, other_i));
  ASSERT_FALSE(row.EqualsQLValuePB(s_idx, other_s));

  // Null vs non-null (either side).
  QLValuePB null_pb;
  ASSERT_FALSE(row.EqualsQLValuePB(i_idx, null_pb));
  row.SetNull(i_idx);
  row.SetNull(s_idx);
  ASSERT_TRUE(row.IsNull(i_idx));
  ASSERT_TRUE(row.IsNull(s_idx));
  ASSERT_TRUE(row.EqualsQLValuePB(i_idx, null_pb));
  ASSERT_TRUE(row.EqualsQLValuePB(s_idx, null_pb));
  ASSERT_FALSE(row.EqualsQLValuePB(i_idx, i_val));
  ASSERT_FALSE(row.EqualsQLValuePB(s_idx, s_val));
}

TEST(PgRowTest, EqualsQLValuePBNaNAndSignedZero) {
  SchemaBuilder builder;
  ASSERT_OK(builder.AddColumn("f", DataType::FLOAT));
  ASSERT_OK(builder.AddColumn("d", DataType::DOUBLE));
  const auto schema = builder.Build();
  ReaderProjection projection(schema);
  PgTableRow row(projection);

  const size_t f_idx = 0;
  const size_t d_idx = 1;
  auto float_pb = [](float value) {
    QLValuePB pb;
    pb.set_float_value(value);
    return pb;
  };
  auto double_pb = [](double value) {
    QLValuePB pb;
    pb.set_double_value(value);
    return pb;
  };
  const auto float_nan = std::numeric_limits<float>::quiet_NaN();
  const auto double_nan = std::numeric_limits<double>::quiet_NaN();

  ASSERT_OK(row.SetValue(schema.column_id(f_idx), float_pb(float_nan)));
  ASSERT_OK(row.SetValue(schema.column_id(d_idx), double_pb(double_nan)));
  ASSERT_TRUE(row.EqualsQLValuePB(f_idx, float_pb(float_nan)));
  ASSERT_TRUE(row.EqualsQLValuePB(d_idx, double_pb(double_nan)));
  ASSERT_FALSE(row.EqualsQLValuePB(f_idx, float_pb(1.0f)));
  ASSERT_FALSE(row.EqualsQLValuePB(d_idx, double_pb(1.0)));

  ASSERT_OK(row.SetValue(schema.column_id(f_idx), float_pb(1.0f)));
  ASSERT_OK(row.SetValue(schema.column_id(d_idx), double_pb(1.0)));
  ASSERT_FALSE(row.EqualsQLValuePB(f_idx, float_pb(float_nan)));
  ASSERT_FALSE(row.EqualsQLValuePB(d_idx, double_pb(double_nan)));

  ASSERT_OK(row.SetValue(schema.column_id(f_idx), float_pb(-0.0f)));
  ASSERT_OK(row.SetValue(schema.column_id(d_idx), double_pb(-0.0)));
  ASSERT_TRUE(row.EqualsQLValuePB(f_idx, float_pb(0.0f)));
  ASSERT_TRUE(row.EqualsQLValuePB(d_idx, double_pb(0.0)));
}

}  // namespace yb::dockv
