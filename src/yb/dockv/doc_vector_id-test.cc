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

#include "yb/common/column_id.h"
#include "yb/common/value.messages.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/doc_vector_id.h"
#include "yb/dockv/key_entry_value.h"
#include "yb/dockv/primitive_value.h"
#include "yb/dockv/value_type.h"

#include "yb/vector_index/vector_index_fwd.h"

#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb::dockv {

namespace {

constexpr uint8_t kRawVectorWithoutId[] = {
    0x03, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x79, 0xc4, 0x00, 0xc0, 0x79, 0xc4, 0x00, 0xc0, 0x79,
    0xc4,
};

constexpr uint8_t kVectorData[] = {
    0x03, 0x00, 0x00, 0x00, 0x00, 0x40, 0x40, 0x40, 0x00, 0x40, 0x40, 0x40,
};

const ColumnId kTestColumnId{42};
constexpr ColocationId kTestColocationId = 16385;

std::string EncodeVectorWithId(const vector_index::VectorId& id, Slice vector_data) {
  LWQLValuePB ql_value(nullptr);
  ql_value.ref_binary_value(vector_data);
  DocVectorValue doc_vector_value(VectorValueFormat::kTyped, ql_value, id);
  std::string out;
  doc_vector_value.EncodeTo(&out);
  return out;
}

KeyBytes MakeYbctid(int32_t pk) {
  return DocKey({KeyEntryValue::Int32(pk)}).Encode();
}

KeyBytes MakeColocationPrefix(ColocationId colocation_id) {
  DocKey key;
  key.set_colocation_id(colocation_id);
  key.range_group().push_back(KeyEntryValue::Int32(0));
  auto encoded = key.Encode();
  auto sizes = CHECK_RESULT(DocKey::EncodedPrefixAndDocKeySizes(encoded.AsSlice()));
  KeyBytes prefix;
  prefix.AppendRawBytes(encoded.AsSlice().Prefix(sizes.prefix_size));
  return prefix;
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

TEST(DocVectorIdTest, MetaValueLegacyDecode) {
  auto ybctid = MakeYbctid(7);
  auto decoded = ASSERT_RESULT(EncodedDocVectorMetaValue::Decode(ybctid.AsSlice()));
  ASSERT_TRUE(decoded.table_key_prefix.empty());
  ASSERT_EQ(decoded.ybctid, ybctid.AsSlice());
  ASSERT_EQ(decoded.column_id, kInvalidColumnId);
  ASSERT_FALSE(decoded.IsTombstone());
}

TEST(DocVectorIdTest, MetaValueTombstoneDecode) {
  char tombstone = ValueEntryTypeAsChar::kTombstone;
  Slice value(&tombstone, 1);
  auto decoded = ASSERT_RESULT(EncodedDocVectorMetaValue::Decode(value));
  ASSERT_TRUE(decoded.IsTombstone());
  ASSERT_EQ(decoded.column_id, kInvalidColumnId);
  ASSERT_TRUE(decoded.table_key_prefix.empty());
}

TEST(DocVectorIdTest, MetaValueV1RoundTrip) {
  auto ybctid = MakeYbctid(11);
  auto encoded = DocVectorMetaValue(/* table_key_prefix */ {}, ybctid.AsSlice(), kTestColumnId);
  auto decoded = ASSERT_RESULT(EncodedDocVectorMetaValue::Decode(encoded.AsSlice()));
  ASSERT_TRUE(decoded.table_key_prefix.empty());
  ASSERT_EQ(decoded.ybctid, ybctid.AsSlice());
  ASSERT_EQ(decoded.column_id, kTestColumnId);
  ASSERT_FALSE(decoded.IsTombstone());
}

TEST(DocVectorIdTest, MetaValueV1WithColocationPrefix) {
  auto prefix = MakeColocationPrefix(kTestColocationId);
  auto ybctid = MakeYbctid(22);
  auto encoded = DocVectorMetaValue(prefix.AsSlice(), ybctid.AsSlice(), kTestColumnId);
  auto decoded = ASSERT_RESULT(EncodedDocVectorMetaValue::Decode(encoded.AsSlice()));
  ASSERT_EQ(decoded.table_key_prefix, prefix.AsSlice());
  ASSERT_EQ(decoded.ybctid, ybctid.AsSlice());
  ASSERT_EQ(decoded.column_id, kTestColumnId);
}

TEST(DocVectorIdTest, MetaValueBadVersion) {
  auto ybctid = MakeYbctid(1);
  auto encoded = DocVectorMetaValue({}, ybctid.AsSlice(), kTestColumnId);
  std::string corrupted(encoded.AsSlice().cdata(), encoded.size());
  corrupted[1] = static_cast<char>(0x7f);  // unknown version
  auto result = EncodedDocVectorMetaValue::Decode(corrupted);
  ASSERT_NOK(result);
  ASSERT_TRUE(result.status().IsCorruption()) << result.status();
}

TEST(DocVectorIdTest, MetaValueTruncated) {
  char header[] = {
      KeyEntryTypeAsChar::kVectorIndexMetadata,
      0x01,
  };
  auto result = EncodedDocVectorMetaValue::Decode(Slice(header, 2));
  ASSERT_NOK(result);
}

TEST(DocVectorIdTest, MetaValueToStringLegacy) {
  auto ybctid = MakeYbctid(3);
  auto str = ASSERT_RESULT(DocVectorMetaValueToString(ybctid.AsSlice()));
  ASSERT_TRUE(str.find("DocKey(") == 0) << str;
  ASSERT_EQ(str.find("SubDocKey("), std::string::npos) << str;
}

TEST(DocVectorIdTest, MetaValueToStringV1) {
  auto ybctid = MakeYbctid(4);
  auto encoded = DocVectorMetaValue({}, ybctid.AsSlice(), kTestColumnId);
  auto str = ASSERT_RESULT(DocVectorMetaValueToString(encoded.AsSlice()));
  ASSERT_NE(str.find("SubDocKey("), std::string::npos) << str;
  ASSERT_NE(str.find("ColumnId(42)"), std::string::npos) << str;
}

TEST(DocVectorIdTest, MetaValueToStringTombstone) {
  char tombstone = ValueEntryTypeAsChar::kTombstone;
  auto str = ASSERT_RESULT(DocVectorMetaValueToString(Slice(&tombstone, 1)));
  ASSERT_EQ(str, PrimitiveValue::kTombstone.ToString());
}

} // namespace yb::dockv
