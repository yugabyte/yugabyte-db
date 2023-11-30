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
#include "yb/dockv/primitive_value.h"
#include "yb/dockv/schema_packing.h"
#include "yb/dockv/value_type.h"

#include "yb/util/fast_varint.h"
#include "yb/util/random_util.h"
#include "yb/util/test_macros.h"

namespace yb::dockv {

void TestRowPacking(const Schema& schema, const std::vector<QLValuePB>& values) {
  ASSERT_EQ(schema.num_columns() - schema.num_key_columns(), values.size());
  constexpr int kVersion = 1;
  SchemaPacking schema_packing(TableType::PGSQL_TABLE_TYPE, schema);
  RowPacker packer(
      kVersion, schema_packing, /* packed_size_limit= */ std::numeric_limits<int64_t>::max(),
      /* value_control_fields= */ Slice());
  size_t idx = schema.num_key_columns();
  for (const auto& value : values) {
    auto column_id = schema.column_id(idx);
    ASSERT_OK(packer.AddValue(column_id, value));
    ++idx;
  }
  auto packed = ASSERT_RESULT(packer.Complete());
  LOG(INFO) << "Packed: " << packed.ToDebugHexString();
  ASSERT_EQ(static_cast<ValueEntryType>(packed.consume_byte()), ValueEntryType::kPackedRow);
  auto version = ASSERT_RESULT(util::FastDecodeUnsignedVarInt(&packed));
  ASSERT_EQ(version, kVersion);
  for (size_t i = schema.num_key_columns(); i != schema.num_columns(); ++i) {
    auto value_slice = *schema_packing.GetValue(schema.column_id(i), packed);
    const auto& value = values[i - schema.num_key_columns()];
    PrimitiveValue decoded_value;
    if (IsNull(value)) {
      ASSERT_TRUE(value_slice.empty());
    } else {
      ASSERT_OK(decoded_value.DecodeFromValue(value_slice));
      auto expected = PrimitiveValue::FromQLValuePB(value);
      ASSERT_EQ(decoded_value, expected);
    }
    LOG(INFO) << i << ": " << value_slice.ToDebugHexString() << ", " << decoded_value;
  }
}

void TestPacking(const std::vector<DataType>& types) {
  constexpr auto kNumIterations = 1000;

  SchemaBuilder builder;
  ASSERT_OK(builder.AddHashKeyColumn("h1", DataType::INT32));
  ASSERT_OK(builder.AddKeyColumn("r1", DataType::STRING));
  for (DataType type : types) {
    auto name = "v_" + std::to_string(builder.next_column_id());
    if (RandomUniformBool()) {
      ASSERT_OK(builder.AddColumn(name, type));
    } else {
      ASSERT_OK(builder.AddNullableColumn(name, type));
    }
  }
  auto schema = builder.Build();
  std::vector<QLValuePB> values;
  values.reserve(types.size());
  for (int i = 0; i != kNumIterations; ++i) {
    values.clear();
    size_t idx = schema.num_key_columns();
    for (DataType type : types) {
      if (schema.column(idx).is_nullable() && RandomUniformBool()) {
        values.emplace_back();
      } else {
        values.push_back(RandomQLValue(type));
      }
      ++idx;
    }
    TestRowPacking(schema, values);
  }
}

TEST(PackedRowTest, Random) {
  std::vector<DataType> supported_types = {DataType::INT32, DataType::INT64, DataType::STRING};
  for (int i = 1; i != 10; ++i) {
    std::vector<DataType> types;
    for (int j = 0; j != i; ++j) {
      types.push_back(RandomElement(supported_types));
    }
    TestPacking(types);
  }
}

} // namespace yb::dockv
