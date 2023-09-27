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

#include <gtest/gtest.h>

#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/dockv/dockv_test_util.h"
#include "yb/dockv/packed_row.h"
#include "yb/dockv/packed_value.h"
#include "yb/dockv/primitive_value.h"
#include "yb/dockv/schema_packing.h"
#include "yb/dockv/value.h"
#include "yb/dockv/value_packing_v2.h"
#include "yb/dockv/value_type.h"

#include "yb/util/fast_varint.h"
#include "yb/util/random_util.h"
#include "yb/util/test_macros.h"

namespace yb::dockv {

template <class Packer, class Decoder>
void TestRowPacking(
    const Schema& schema, const std::vector<QLValuePB>& values) {
  auto value_type = Decoder::kValueEntryType;
  ASSERT_EQ(schema.num_columns() - schema.num_key_columns(), values.size());
  constexpr int kVersion = 1;
  SchemaPacking schema_packing(TableType::PGSQL_TABLE_TYPE, schema);
  Packer packer(
      kVersion, schema_packing, /* packed_size_limit= */ std::numeric_limits<int64_t>::max(),
      /* value_control_fields= */ Slice(), schema);
  size_t idx = schema.num_key_columns();
  for (const auto& value : values) {
    auto column_id = schema.column_id(idx);
    ASSERT_OK(packer.AddValue(column_id, value));
    ++idx;
  }
  auto packed = ASSERT_RESULT(packer.Complete());
  ASSERT_EQ(static_cast<ValueEntryType>(packed.consume_byte()), value_type);
  auto version = ASSERT_RESULT(FastDecodeUnsignedVarInt(&packed));
  ASSERT_EQ(version, kVersion);
  Decoder decoder(schema_packing, packed.data());
  ValueBuffer buffer;
  for (size_t i = schema.num_key_columns(); i != schema.num_columns(); ++i) {
    auto value_column_index = i - schema.num_key_columns();
    auto value_slice = decoder.FetchValue(value_column_index);
    const auto& value = values[value_column_index];
    ASSERT_EQ(IsNull(value), value_slice.IsNull());
    if (IsNull(value)) {
      VLOG(1) << i << ": <NULL>";
      continue;
    }
    buffer.Clear();
    switch (value_type) {
      case ValueEntryType::kPackedRowV1:
        AppendEncodedValue(value, &buffer);
        break;
      case ValueEntryType::kPackedRowV2:
        PackQLValueV2(value, schema.column(i).type_info()->type, &buffer);
        break;
      default:
        FAIL();
    }
    ASSERT_EQ(buffer.AsSlice().ToDebugHexString(), value_slice->ToDebugHexString());
    auto decoded_value = ASSERT_RESULT(UnpackPrimitiveValue(
        value_slice, schema.column(i).type_info()->type));
    auto expected = PrimitiveValue::FromQLValuePB(value);
    ASSERT_EQ(decoded_value, expected);
    VLOG(1) << i << ": " << value_slice->ToDebugHexString() << ", " << decoded_value;
  }
}

Result<Schema> BuildSchema(const std::vector<DataType>& types, bool allow_nullable = true) {
  SchemaBuilder builder;
  RETURN_NOT_OK(builder.AddHashKeyColumn("h1", DataType::INT32));
  RETURN_NOT_OK(builder.AddKeyColumn("r1", DataType::STRING));
  for (DataType type : types) {
    auto name = "v_" + std::to_string(builder.next_column_id());
    if (!allow_nullable || RandomUniformBool()) {
      RETURN_NOT_OK(builder.AddColumn(name, type));
    } else {
      RETURN_NOT_OK(builder.AddNullableColumn(name, type));
    }
  }
  return builder.Build();
}

template <class Packer, class Decoder>
void TestPacking(const std::vector<DataType>& types) {
  constexpr auto kNumIterations = 1000;

  auto schema = ASSERT_RESULT(BuildSchema(types));
  std::vector<QLValuePB> values;
  values.reserve(types.size());
  for (int i = 0; i != kNumIterations; ++i) {
    values.clear();
    size_t idx = schema.num_key_columns();
    for (DataType type : types) {
      // Generate slightly less than one null per row on average.
      if (schema.column(idx).is_nullable() && RandomUniformInt<size_t>(0, types.size()) == 0) {
        values.emplace_back();
      } else {
        values.push_back(RandomQLValue(type));
      }
      ++idx;
    }
    ASSERT_NO_FATALS((TestRowPacking<Packer, Decoder>(schema, values)));
  }
}

template <class Packer, class Decoder>
void TestRandom() {
  ThreadLocalRandom().seed(42);
  std::vector<DataType> supported_types = {
      DataType::INT32, DataType::INT64, DataType::STRING, DataType::BOOL, DataType::INT8,
      DataType::INT16, DataType::DOUBLE};
  for (int i = 1; i != 100; ++i) {
    std::vector<DataType> types;
    for (int j = 0; j != i; ++j) {
      types.push_back(RandomElement(supported_types));
    }
    ASSERT_NO_FATALS((TestPacking<Packer, Decoder>(types)));
  }
}

TEST(PackedRowTest, Random) {
  TestRandom<RowPackerV1, PackedRowDecoderV1>();
}

TEST(PackedRowTest, RandomV2) {
  TestRandom<RowPackerV2, PackedRowDecoderV2>();
}

TEST(PackedRowTest, PackWithLimitV2) {
  auto schema = ASSERT_RESULT(BuildSchema({DataType::STRING, DataType::INT64}));
  SchemaPacking schema_packing(TableType::PGSQL_TABLE_TYPE, schema);
  RowPackerV2 packer(0, schema_packing, 20, Slice(), schema);
  QLValuePB value;
  value.set_string_value(RandomHumanReadableString(42));
  // Packed row does not provide strict limit for the data.
  // It just tries to fit encoded row into the number of bytes close to the specified limit.
  ASSERT_FALSE(ASSERT_RESULT(packer.AddValue(schema_packing.column_packing_data(0).id, value)));
  value.set_int64_value(0x39);
  ASSERT_TRUE(ASSERT_RESULT(packer.AddValue(schema_packing.column_packing_data(1).id, value)));
  ASSERT_EQ(ASSERT_RESULT(packer.Complete()).ToDebugHexString(), "7C0001013900000000000000");
}

TEST(PackedRowTest, LongStringV2) {
  auto schema = ASSERT_RESULT(BuildSchema({DataType::STRING, DataType::STRING}));
  SchemaPacking schema_packing(TableType::PGSQL_TABLE_TYPE, schema);
  QLValuePB value1;
  value1.set_string_value(RandomHumanReadableString(0xffff));
  QLValuePB value2;
  value2.set_string_value(RandomHumanReadableString(32));
  TestRowPacking<RowPackerV2, PackedRowDecoderV2>(schema, {value1, value2});
  TestRowPacking<RowPackerV2, PackedRowDecoderV2>(schema, {value2, value1});
}

template <class Packer>
std::string TestPackWithControlFields() {
  auto schema = EXPECT_RESULT(BuildSchema(
      {DataType::STRING, DataType::INT8}, /* allow_nullable= */ false));
  SchemaPacking schema_packing(TableType::PGSQL_TABLE_TYPE, schema);
  dockv::ValueControlFields control_fields;
  control_fields.ttl = MonoDelta::FromMicroseconds(1000);
  Packer packer(
      /* schema_version= */ 0, schema_packing, std::numeric_limits<size_t>::max(), control_fields,
      schema);
  QLValuePB value;
  value.set_string_value("privet");
  EXPECT_TRUE(EXPECT_RESULT(packer.AddValue(schema_packing.column_packing_data(0).id, value)));
  value.set_int8_value(0x39);
  EXPECT_TRUE(EXPECT_RESULT(packer.AddValue(schema_packing.column_packing_data(1).id, value)));
  return EXPECT_RESULT(packer.Complete()).ToDebugHexString();
}

TEST(PackedRowTest, ControlFields) {
  ASSERT_EQ(TestPackWithControlFields<RowPackerV1>(), "74817A0007000000537072697665744800000039");
  ASSERT_EQ(TestPackWithControlFields<RowPackerV2>(), "74817C00000C70726976657439");
}

} // namespace yb::dockv
