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

#include <cstdint>
#include <string>

#include "yb/common/value.messages.h"

#include "yb/dockv/doc_vector_id.h"
#include "yb/dockv/value_type.h"

#include "yb/vector_index/vector_index_fwd.h"

#include "yb/util/test_macros.h"

namespace yb::dockv {

namespace {

constexpr uint8_t kRawVectorWithoutId[] = {
    0x03, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x79, 0xc4, 0x00, 0xc0, 0x79, 0xc4, 0x00, 0xc0, 0x79,
    0xc4,
};

constexpr uint8_t kVectorData[] = {
    0x03, 0x00, 0x00, 0x00, 0x00, 0x40, 0x40, 0x40, 0x00, 0x40, 0x40, 0x40,
};

std::string EncodeVectorWithId(const vector_index::VectorId& id, Slice vector_data) {
  LWQLValuePB ql_value(nullptr);
  ql_value.ref_binary_value(vector_data);
  DocVectorValue doc_vector_value(VectorValueFormat::kTyped, ql_value, id);
  std::string out;
  doc_vector_value.EncodeTo(&out);
  return out;
}

} // namespace

TEST(DocVectorIdTest, EncodeVectorSchemaMissingValue) {
  QLValuePB raw_value;
  raw_value.set_binary_value(
      std::string(reinterpret_cast<const char*>(kRawVectorWithoutId), sizeof(kRawVectorWithoutId)));

  const auto encoded_value =
      ASSERT_RESULT(EncodeVectorSchemaMissingValue(raw_value, VectorValueFormat::kTyped));
  ASSERT_EQ(encoded_value.value_case(), QLValuePB::kBinaryValue);

  const auto encoded = EncodedDocVectorValue::FromSlice(encoded_value.binary_value());
  ASSERT_EQ(encoded.data[0], ValueEntryTypeAsChar::kVector);
  ASSERT_TRUE(encoded.id.empty());
  ASSERT_EQ(DocVectorValue::SanitizeValue(encoded_value.binary_value()), encoded.data);

  const auto pg_row_value = ASSERT_RESULT(DecodeVectorSchemaMissingValueForPgRow(encoded_value));
  ASSERT_EQ(pg_row_value.binary_value().size(), sizeof(kRawVectorWithoutId) + 1);
  ASSERT_EQ(
      DocVectorValue::SanitizeValue(pg_row_value.binary_value()),
      Slice(reinterpret_cast<const char*>(kRawVectorWithoutId), sizeof(kRawVectorWithoutId)));
}

TEST(DocVectorIdTest, FromSliceVectorWithIdSuffix) {
  const auto vector_id = vector_index::VectorId::GenerateRandom();
  const Slice vector_data(reinterpret_cast<const char*>(kVectorData), sizeof(kVectorData));
  const auto encoded_value = EncodeVectorWithId(vector_id, vector_data);

  const auto encoded = EncodedDocVectorValue::FromSlice(encoded_value);
  ASSERT_EQ(ASSERT_RESULT(encoded.DecodeId()), vector_id);
  ASSERT_FALSE(encoded.id.empty());
  ASSERT_EQ(DocVectorValue::SanitizeValue(encoded_value), encoded.data);
  // encoded.id is the UUID only; the suffix also includes the VectorId type byte and size byte.
  ASSERT_EQ(encoded_value.size(), encoded.data.size() + encoded.id.size() + 2);
  ASSERT_GT(encoded.data.size(), vector_data.size());
  ASSERT_EQ(encoded.data[0], ValueEntryTypeAsChar::kVector);
}

TEST(DocVectorIdTest, FromSliceExplicitNoIdSuffix) {
  const Slice vector_data(reinterpret_cast<const char*>(kVectorData), sizeof(kVectorData));
  std::string encoded_value(vector_data.cdata(), vector_data.size());
  encoded_value.push_back(0);

  const auto encoded = EncodedDocVectorValue::FromSlice(encoded_value);
  ASSERT_EQ(encoded.data, vector_data);
  ASSERT_TRUE(encoded.id.empty());
}

} // namespace yb::dockv
