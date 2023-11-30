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
#include "yb/dockv/pg_key_decoder.h"
#include "yb/dockv/pg_row.h"
#include "yb/dockv/reader_projection.h"

#include "yb/util/decimal.h"
#include "yb/util/format.h"
#include "yb/util/random_util.h"
#include "yb/util/test_macros.h"

namespace yb::dockv {

template <class Decoder>
void RunDecode(const Schema& schema, const std::vector<KeyBytes>& entries, const char* name) {
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

void TestDecode(const std::vector<DataType>& types) {
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

  RunDecode<PgKeyDecoder>(schema, entries, "v1");
}

TEST(PgRowTest, KeyDecoderPerformance) {
  TestDecode(std::vector<DataType>(10, DataType::INT32));
}

}  // namespace yb::dockv
