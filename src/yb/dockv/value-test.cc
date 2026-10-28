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

#include <string>

#include <gtest/gtest.h>

#include "yb/common/hybrid_time.h"

#include "yb/dockv/value.h"
#include "yb/dockv/value_type.h"

#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/test_macros.h"

namespace yb::dockv {

namespace {

std::string Encode(const ValueControlFields& fields) {
  std::string out;
  fields.AppendEncoded(&out);
  return out;
}

Result<ValueControlFields> Decode(const std::string& encoded) {
  Slice slice(encoded);
  auto result = VERIFY_RESULT(ValueControlFields::Decode(&slice));
  // Every byte the encoder produced must be consumed by the decoder, otherwise the trailing
  // primitive value would be misread.
  SCHECK(slice.empty(), Corruption, "Control fields decode left unconsumed bytes");
  return result;
}

Result<ValueControlFields> RoundTrip(const ValueControlFields& fields) {
  return Decode(Encode(fields));
}

constexpr uint32_t kOrigin = 42;
const HybridTime kSourceHt = HybridTime::FromMicros(1753351200000000);

} // namespace

class ValueControlFieldsTest : public ::testing::Test {};

// Absence is what marks a row version as locally written, so it has to cost nothing on disk.
TEST_F(ValueControlFieldsTest, LocalWriteEncodesNothing) {
  ValueControlFields fields;
  ASSERT_FALSE(fields.has_cdr_metadata());
  ASSERT_TRUE(Encode(fields).empty());
}

TEST_F(ValueControlFieldsTest, RoundTripBothFields) {
  ValueControlFields fields;
  fields.cdr_source_origin = kOrigin;
  fields.cdr_source_write_ht = kSourceHt;

  auto decoded = ASSERT_RESULT(RoundTrip(fields));
  ASSERT_EQ(decoded.cdr_source_origin, kOrigin);
  ASSERT_EQ(decoded.cdr_source_write_ht, kSourceHt);
  ASSERT_TRUE(decoded.has_cdr_metadata());
}

// Origin 0 is PostgreSQL's InvalidRepOriginId, so it must encode as absent rather than as a
// present-but-zero origin.
TEST_F(ValueControlFieldsTest, ZeroOriginIsAbsent) {
  ValueControlFields fields;
  fields.cdr_source_origin = 0;
  fields.cdr_source_write_ht = kSourceHt;

  ASSERT_TRUE(Encode(fields).empty());
}

// The decoder tests markers in a fixed order, so the new fields must not disturb the existing
// ones when they appear together.
TEST_F(ValueControlFieldsTest, CoexistsWithTtlAndUserTimestamp) {
  ValueControlFields fields;
  fields.merge_flags = 1;
  fields.ttl = MonoDelta::FromMilliseconds(5000);
  fields.timestamp = 12345;
  fields.cdr_source_origin = kOrigin;
  fields.cdr_source_write_ht = kSourceHt;

  auto decoded = ASSERT_RESULT(RoundTrip(fields));
  ASSERT_EQ(decoded.merge_flags, 1);
  ASSERT_EQ(decoded.ttl, MonoDelta::FromMilliseconds(5000));
  ASSERT_EQ(decoded.timestamp, 12345);
  ASSERT_EQ(decoded.cdr_source_origin, kOrigin);
  ASSERT_EQ(decoded.cdr_source_write_ht, kSourceHt);
}

// A value written before these fields existed has no CDR markers, and must decode unchanged.
TEST_F(ValueControlFieldsTest, DecodesPreExistingEncoding) {
  ValueControlFields old_fields;
  old_fields.ttl = MonoDelta::FromMilliseconds(1000);
  old_fields.timestamp = 999;
  auto legacy_encoding = Encode(old_fields);

  auto decoded = ASSERT_RESULT(Decode(legacy_encoding));
  ASSERT_EQ(decoded.ttl, MonoDelta::FromMilliseconds(1000));
  ASSERT_EQ(decoded.timestamp, 999);
  ASSERT_FALSE(decoded.has_cdr_metadata());
}

// The control fields are a prefix on the value, so they ride on a tombstone just as they do on a
// packed row. That is what makes delete_origin_differs expressible.
TEST_F(ValueControlFieldsTest, StampsATombstone) {
  ValueControlFields fields;
  fields.cdr_source_origin = kOrigin;
  fields.cdr_source_write_ht = kSourceHt;

  std::string encoded;
  fields.AppendEncoded(&encoded);
  encoded.push_back(ValueEntryTypeAsChar::kTombstone);

  Value value;
  ASSERT_OK(value.Decode(encoded));
  ASSERT_EQ(value.value_type(), ValueEntryType::kTombstone);
  ASSERT_EQ(value.control_fields().cdr_source_origin, kOrigin);
  ASSERT_EQ(value.control_fields().cdr_source_write_ht, kSourceHt);
}

// Only the varint field is exercised here. The fixed-width source write hybrid time is read
// through the same RSTATUS_DCHECK_GE path as the pre-existing TTL and user timestamp fields,
// which aborts rather than returning Corruption in a debug build, so it cannot be asserted on.
TEST_F(ValueControlFieldsTest, TruncatedSourceOriginIsCorruption) {
  std::string encoded;
  encoded.push_back(KeyEntryTypeAsChar::kCdrMetadata);

  Slice slice(encoded);
  ASSERT_NOK(ValueControlFields::Decode(&slice));
}

// Callers skip the decode entirely when the first byte is not a control-field marker, so the
// predicate has to recognise every field the encoder can put first. Encoding one field at a time
// is what makes each of them first.
TEST_F(ValueControlFieldsTest, EveryFieldIsRecognisedAsAControlFieldMarker) {
  const std::pair<const char*, ValueControlFields> cases[] = {
      {"merge flags", [] { ValueControlFields f; f.merge_flags = 1; return f; }()},
      {"ttl", [] { ValueControlFields f; f.ttl = MonoDelta::FromMilliseconds(1); return f; }()},
      {"user timestamp", [] { ValueControlFields f; f.timestamp = 1; return f; }()},
      {"cdr metadata", [] {
        ValueControlFields f;
        f.cdr_source_origin = kOrigin;
        f.cdr_source_write_ht = kSourceHt;
        return f;
      }()},
  };
  for (const auto& [name, fields] : cases) {
    auto encoded = Encode(fields);
    ASSERT_FALSE(encoded.empty()) << name;
    ASSERT_TRUE(StartsWithControlField(Slice(encoded))) << name;
  }

  // Written by the intent path rather than by AppendEncoded, so it is checked directly.
  ASSERT_TRUE(IsControlFieldMarker(KeyEntryType::kHybridTime));

  // A plain value must not be mistaken for one, or the decode would be skipped where it matters
  // and paid for everywhere it does not.
  ASSERT_FALSE(StartsWithControlField(Slice()));
  ASSERT_FALSE(StartsWithControlField(Value::EncodedTombstone()));
}

TEST_F(ValueControlFieldsTest, ToStringReportsCdrMetadata) {
  ValueControlFields fields;
  fields.cdr_source_origin = kOrigin;
  fields.cdr_source_write_ht = kSourceHt;

  auto str = fields.ToString();
  ASSERT_STR_CONTAINS(str, "source origin: 42");
  ASSERT_STR_CONTAINS(str, "source write ht: " + kSourceHt.ToString());

  ASSERT_EQ(ValueControlFields().ToString(), "");
}

// Compaction, repacking and schema-version rewrites decode a value and encode it straight back,
// so the encoder must reproduce a stamp exactly. Gating AppendEncoded on a default-off feature
// flag, rather than the path that sets these fields, would fail this.
TEST_F(ValueControlFieldsTest, ReEncodingKeepsStamp) {
  ValueControlFields fields;
  fields.ttl = MonoDelta::FromMilliseconds(1000);
  fields.timestamp = 999;
  fields.cdr_source_origin = kOrigin;
  fields.cdr_source_write_ht = kSourceHt;

  auto stamped = Encode(fields);
  auto decoded = ASSERT_RESULT(Decode(stamped));
  ASSERT_EQ(Encode(decoded), stamped);
}

} // namespace yb::dockv
