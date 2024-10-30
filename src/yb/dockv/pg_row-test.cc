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

#include <gtest/gtest.h>

#include "yb/common/ql_value.h"
#include "yb/common/schema.h"
#include "yb/common/ql_type.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/doc_kv_util.h"
#include "yb/dockv/dockv_test_util.h"
#include "yb/dockv/packed_row.h"
#include "yb/dockv/packed_value.h"
#include "yb/dockv/pg_key_decoder.h"
#include "yb/dockv/pg_row.h"
#include "yb/dockv/reader_projection.h"
#include "yb/dockv/value_packing.h"

#include "yb/util/decimal.h"
#include "yb/util/fast_varint.h"
#include "yb/util/format.h"
#include "yb/util/random_util.h"
#include "yb/util/test_macros.h"

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
    Packer packer(0, schema_packing, std::numeric_limits<ssize_t>::max(), Slice(), schema);
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

}  // namespace yb::dockv
