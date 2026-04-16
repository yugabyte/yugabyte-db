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
#include <string>

#include <gtest/gtest.h>

#include "yb/common/constants.h"
#include "yb/common/ql_type.h"
#include "yb/common/ql_value.h"
#include "yb/common/types.h"

#include "yb/dockv/key_bytes.h"
#include "yb/dockv/key_entry_value.h"
#include "yb/dockv/primitive_value.h"
#include "yb/dockv/value_type.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/util/net/net_util.h"
#include "yb/util/random.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/string_trim.h"
#include "yb/util/test_macros.h"
#include "yb/util/tsan_util.h"

using std::map;
using std::string;
using std::numeric_limits;
using strings::Substitute;

namespace yb::dockv {

namespace {

QLValuePB RandomQLValuePB(std::mt19937_64& rng);

QLValuePB::ValueCase RandomValueCaseKeyType(std::mt19937_64& rng) {
  for (;;) {
    auto result = static_cast<QLValuePB::ValueCase>(
        RandomUniformInt(0, static_cast<int>(QLValuePB::kBsonValue), &rng));
    if (result != QLValuePB::kJsonbValue && result != QLValuePB::kMapValue &&
        result != QLValuePB::kSetValue && result != QLValuePB::kListValue &&
        result != QLValuePB::kTupleValue) {
      return result;
    }
  }
}

QLValuePB RandomQLValuePB(QLValuePB::ValueCase value_case, std::mt19937_64& rng) {
  QLValuePB result;
  switch (value_case) {
    case QLValuePB::kInt8Value:
      result.set_int8_value(RandomUniformInt<int8_t>(&rng));
      break;
    case QLValuePB::kInt16Value:
      result.set_int16_value(RandomUniformInt<int16_t>(&rng));
      break;
    case QLValuePB::kInt32Value:
      result.set_int32_value(RandomUniformInt<int32_t>(&rng));
      break;
    case QLValuePB::kInt64Value:
      result.set_int64_value(RandomUniformInt<int64_t>(&rng));
      break;
    case QLValuePB::kFloatValue:
      result.set_float_value(RandomUniformReal<float>(&rng));
      break;
    case QLValuePB::kDoubleValue:
      result.set_double_value(RandomUniformReal<double>(&rng));
      break;
    case QLValuePB::kStringValue:
      result.set_string_value(RandomHumanReadableString(
          RandomUniformInt<size_t>(0, 10000, &rng), &rng));
      break;
    case QLValuePB::kBoolValue:
      result.set_bool_value(RandomUniformBool(&rng));
      break;
    case QLValuePB::kTimestampValue:
      result.set_timestamp_value(RandomUniformInt<int64_t>(&rng));
      break;
    case QLValuePB::kBinaryValue:
      result.set_binary_value(RandomString(RandomUniformInt<size_t>(0, 10000, &rng), &rng));
      break;
    case QLValuePB::kInetaddressValue:
      result.set_inetaddress_value(RandomString(RandomUniformBool(&rng) ? 4 : 16, &rng));
      break;
    case QLValuePB::kDecimalValue:
      result.set_decimal_value(RandomString(RandomUniformInt<size_t>(1, 10000, &rng), &rng));
      break;
    case QLValuePB::kVarintValue:
      result.set_varint_value(RandomString(RandomUniformInt<size_t>(1, 10000, &rng), &rng));
      break;
    case QLValuePB::kFrozenValue: {
        auto len = RandomUniformInt<size_t>(0, 16, &rng);
        auto& elems = *result.mutable_frozen_value()->mutable_elems();
        for (size_t i = 0; i != len; ++i) {
          elems.Add(RandomQLValuePB(rng));
        }
      }
      break;
    case QLValuePB::kUuidValue:
      result.set_uuid_value(Uuid::Generate().AsSlice().ToBuffer());
      break;
    case QLValuePB::kTimeuuidValue: {
        auto uuid = Uuid::Generate();
        uuid.data()[6] = (boost::uuids::uuid::version_time_based << 4);
        result.set_timeuuid_value(uuid.AsSlice().ToBuffer());
      } break;
    case QLValuePB::kDateValue:
      result.set_date_value(RandomUniformInt<uint32_t>(&rng));
      break;
    case QLValuePB::kTimeValue:
      result.set_time_value(RandomUniformInt<int64_t>(&rng));
      break;
    case QLValuePB::kUint32Value:
      result.set_uint32_value(RandomUniformInt<uint32_t>(&rng));
      break;
    case QLValuePB::kUint64Value:
      result.set_uint64_value(RandomUniformInt<uint64_t>(&rng));
      break;
    case QLValuePB::kVirtualValue: {
        auto value = static_cast<QLVirtualValuePB>(RandomUniformInt(1, 6, &rng));
        if (value == QLVirtualValuePB::TOMBSTONE) {
          value = QLVirtualValuePB::NULL_LOW;
        }
        result.set_virtual_value(value);
      } break;
    case QLValuePB::kGinNullValue:
      result.set_gin_null_value(RandomUniformInt<uint32_t>(1, 3, &rng));
      break;
    case QLValuePB::kBsonValue:
      result.set_bson_value(RandomString(RandomUniformInt<size_t>(0, 10000, &rng), &rng));
      break;
    case QLValuePB::VALUE_NOT_SET:
      break;
    case QLValuePB::kMapValue: [[fallthrough]];
    case QLValuePB::kSetValue: [[fallthrough]];
    case QLValuePB::kListValue: [[fallthrough]];
    case QLValuePB::kJsonbValue: [[fallthrough]];
    case QLValuePB::kTupleValue:
      break;
  }
  CHECK_EQ(result.value_case(), value_case);
  return result;
}

QLValuePB RandomQLValuePB(std::mt19937_64& rng) {
  return RandomQLValuePB(RandomValueCaseKeyType(rng), rng);
}

void EncodeAndDecode(const KeyEntryValue& primitive_value) {
  KeyBytes key_bytes = primitive_value.ToKeyBytes();
  KeyEntryValue decoded;
  auto slice = key_bytes.AsSlice();
  ASSERT_OK_PREPEND(
      decoded.DecodeFromKey(&slice),
      Substitute(
          "Could not decode key bytes obtained by encoding primitive value $0: $1",
          primitive_value.ToString(), key_bytes.ToString()));
  ASSERT_TRUE(slice.empty())
      << "Not all bytes consumed when encoding/decoding primitive value "
      << primitive_value.ToString() << ": "
      << slice.size() << " bytes left."
      << "Key bytes: " << key_bytes.ToString() << ".";
  ASSERT_EQ(primitive_value.ToString(), decoded.ToString())
      << "String representation of decoded value is different from that of the original value.";
}

void TestEncoding(const char* expected_str, const KeyEntryValue& primitive_value) {
  ASSERT_STR_EQ_VERBOSE_TRIMMED(expected_str, primitive_value.ToKeyBytes().ToString());
}

template <typename T>
void CompareSlices(
    KeyBytes key_bytes1, KeyBytes key_bytes2, T val1, T val2, string str1, string str2) {
  rocksdb::Slice slice1 = key_bytes1.AsSlice();
  rocksdb::Slice slice2 = key_bytes2.AsSlice();
  if (val1 > val2) {
    ASSERT_LT(0, slice1.compare(slice2)) << strings::Substitute("Failed for values $0, $1",
              str1, str2);
  } else if (val1 < val2) {
    ASSERT_GT(0, slice1.compare(slice2)) << strings::Substitute("Failed for values $0, $1",
              str1, str2);
  } else {
    ASSERT_EQ(0, slice1.compare(slice2)) << strings::Substitute("Failed for values $0, $1",
              str1, str2);
  }
}

void TestCopy(PrimitiveValue&& v1, int64_t ttl, int64_t write_time) {
  v1.SetTtl(ttl);
  v1.SetWriteTime(write_time);
  // Uses copy constructor.
  PrimitiveValue v2 = v1;
  ASSERT_EQ(v1, v2);
  ASSERT_EQ(v1.GetTtl(), v2.GetTtl());
  ASSERT_EQ(v1.GetWriteTime(), v2.GetWriteTime());

  // Uses copy assignment operator.
  PrimitiveValue v3;
  v3 = v1;
  ASSERT_EQ(v1, v3);
  ASSERT_EQ(v1.GetTtl(), v3.GetTtl());
  ASSERT_EQ(v1.GetWriteTime(), v3.GetWriteTime());
}

void TestMove(PrimitiveValue&& v1, int64_t ttl, int64_t write_time) {
  v1.SetTtl(ttl);
  v1.SetWriteTime(write_time);
  PrimitiveValue vtemp = v1;
  // Uses move constructor.
  PrimitiveValue v2 = std::move(v1);
  ASSERT_EQ(vtemp, v2);
  ASSERT_EQ(vtemp.GetTtl(), v2.GetTtl());
  ASSERT_EQ(vtemp.GetWriteTime(), v2.GetWriteTime());

  // Uses move assignment operator.
  PrimitiveValue v3;
  v3 = std::move(v2);
  ASSERT_EQ(vtemp, v3);
  ASSERT_EQ(vtemp.GetTtl(), v3.GetTtl());
  ASSERT_EQ(vtemp.GetWriteTime(), v3.GetWriteTime());
}

}  // unnamed namespace

TEST(PrimitiveValueTest, TestToString) {
  ASSERT_EQ("\"foo\"", PrimitiveValue("foo").ToString());
  ASSERT_EQ("\"foo\\\"\\x00\\x01\\x02\\\"bar\"",
      PrimitiveValue(string("foo\"\x00\x01\x02\"bar", 11)).ToString());

  ASSERT_EQ("123456789000", PrimitiveValue::Int64(123456789000l).ToString());
  ASSERT_EQ("-123456789000", PrimitiveValue::Int64(-123456789000l).ToString());
  ASSERT_EQ("9223372036854775807",
      PrimitiveValue::Int64(numeric_limits<int64_t>::max()).ToString());
  ASSERT_EQ("-9223372036854775808",
      PrimitiveValue::Int64(numeric_limits<int64_t>::min()).ToString());

  ASSERT_EQ("123456789", PrimitiveValue::Int32(123456789).ToString());
  ASSERT_EQ("-123456789", PrimitiveValue::Int32(-123456789).ToString());
  ASSERT_EQ("2147483647",
            PrimitiveValue::Int32(numeric_limits<int32_t>::max()).ToString());
  ASSERT_EQ("-2147483648",
            PrimitiveValue::Int32(numeric_limits<int32_t>::min()).ToString());

  ASSERT_EQ("0", PrimitiveValue::UInt64(numeric_limits<uint64_t>::min()).ToString());
  ASSERT_EQ("18446744073709551615",
            PrimitiveValue::UInt64(numeric_limits<uint64_t>::max()).ToString());

  ASSERT_EQ("3.1415", PrimitiveValue::Double(3.1415).ToString());
  ASSERT_EQ("100.0", PrimitiveValue::Double(100.0).ToString());
  ASSERT_EQ("1.000000E-100", PrimitiveValue::Double(1e-100).ToString());

  ASSERT_EQ("3.1415", PrimitiveValue::Float(3.1415).ToString());
  ASSERT_EQ("100.0", PrimitiveValue::Float(100.0).ToString());
  ASSERT_EQ("1.000000E-37", PrimitiveValue::Float(1e-37).ToString());

  ASSERT_EQ("ArrayIndex(123)", KeyEntryValue::ArrayIndex(123).ToString());
  ASSERT_EQ("ArrayIndex(-123)", KeyEntryValue::ArrayIndex(-123).ToString());

  ASSERT_EQ("HT{ physical: 100200300400500 logical: 1234 }",
      KeyEntryValue(HybridTime(100200300400500l * 4096 + 1234)).ToString());

  // HybridTimes use an unsigned 64-bit integer as an internal representation.
  ASSERT_EQ("HT<min>", KeyEntryValue(HybridTime(0)).ToString());
  ASSERT_EQ("HT<initial>", KeyEntryValue(HybridTime(1)).ToString());
  ASSERT_EQ("HT<max>", KeyEntryValue(HybridTime(numeric_limits<uint64_t>::max())).ToString());
  ASSERT_EQ("HT<max>", KeyEntryValue(HybridTime(-1)).ToString());

  ASSERT_EQ("UInt16Hash(65535)",
            KeyEntryValue::UInt16Hash(numeric_limits<uint16_t>::max()).ToString());
  ASSERT_EQ("UInt16Hash(65535)", KeyEntryValue::UInt16Hash(-1).ToString());
  ASSERT_EQ("UInt16Hash(0)", KeyEntryValue::UInt16Hash(0).ToString());

  ASSERT_EQ("ColumnId(2147483647)",
            KeyEntryValue::MakeColumnId(ColumnId(numeric_limits<int32_t>::max())).ToString());
  ASSERT_EQ("ColumnId(0)",
            KeyEntryValue::MakeColumnId(ColumnId(0)).ToString());

  ASSERT_EQ("SystemColumnId(2147483647)",
            KeyEntryValue::SystemColumnId(ColumnId(numeric_limits<int32_t>::max())).ToString());
  ASSERT_EQ("SystemColumnId(0)",
            KeyEntryValue::SystemColumnId(ColumnId(0)).ToString());

#ifndef NDEBUG
  // These have DCHECK() and hence triggered only in DEBUG MODE.
  // Negative column ids are not allowed.
  EXPECT_EXIT(ColumnId(-1), ::testing::KilledBySignal(SIGABRT), "Check failed.*");

  ColumnId col;
  EXPECT_EXIT({col = static_cast<ColumnIdRep>(-1);}, ::testing::KilledBySignal(SIGABRT),
              "Check failed.*");
#endif
}

TEST(PrimitiveValueTest, TestRoundTrip) {
  for (auto primitive_value : {
      KeyEntryValue("foo"),
      KeyEntryValue(string("foo\0bar\x01", 8)),
      KeyEntryValue::Int64(123L),
      KeyEntryValue::Int32(123),
      KeyEntryValue::Int32(std::numeric_limits<int32_t>::max()),
      KeyEntryValue::Int32(std::numeric_limits<int32_t>::min()),
      KeyEntryValue(HybridTime(1000L)),
      KeyEntryValue::MakeColumnId(ColumnId(numeric_limits<ColumnIdRep>::max())),
      KeyEntryValue::MakeColumnId(ColumnId(0)),
      KeyEntryValue::SystemColumnId(ColumnId(numeric_limits<ColumnIdRep>::max())),
      KeyEntryValue::SystemColumnId(ColumnId(0)),
      KeyEntryValue::MakeBson(string("foo\0bar\x01", 8)),
  }) {
    EncodeAndDecode(primitive_value);
  }
}

void TestRoundTrip(const PrimitiveValue& primitive_value, DataType data_type) {
  QLValuePB ql_value;
  auto ql_type = QLType::Create(data_type);
  primitive_value.ToQLValuePB(ql_type, &ql_value);
  ValueBuffer buffer;
  AppendEncodedValue(ql_value, &buffer);

  QLValuePB decoded_ql_value;
  ASSERT_OK(PrimitiveValue::DecodeToQLValuePB(buffer.AsSlice(), data_type, &decoded_ql_value));

  ASSERT_EQ(QLValue(ql_value), QLValue(decoded_ql_value))
      << Format("{ expected: $0, actual: $1 }", ql_value, decoded_ql_value);
}

TEST(PrimitiveValueTest, PrimitiveValueRoundTrip) {
  TestRoundTrip(PrimitiveValue("foo"), DataType::STRING);
  TestRoundTrip(PrimitiveValue::Int64(123456789000l), DataType::INT64);
  TestRoundTrip(PrimitiveValue::Int64(-123456789000l), DataType::INT64);
  TestRoundTrip(PrimitiveValue::Int64(numeric_limits<int64_t>::max()), DataType::INT64);
  TestRoundTrip(PrimitiveValue::Int64(numeric_limits<int64_t>::min()), DataType::INT64);
  TestRoundTrip(PrimitiveValue::Int32(123456789), DataType::INT32);
  TestRoundTrip(PrimitiveValue::Int32(-123456789), DataType::INT32);
  TestRoundTrip(PrimitiveValue::Int32(numeric_limits<int32_t>::max()), DataType::INT32);
  TestRoundTrip(PrimitiveValue::Int32(numeric_limits<int32_t>::min()), DataType::INT32);
  TestRoundTrip(PrimitiveValue::UInt64(numeric_limits<uint64_t>::min()), DataType::UINT64);
  TestRoundTrip(PrimitiveValue::UInt64(numeric_limits<uint64_t>::max()), DataType::UINT64);
  TestRoundTrip(PrimitiveValue::Double(3.1415), DataType::DOUBLE);
  TestRoundTrip(PrimitiveValue::Double(100.0), DataType::DOUBLE);
  TestRoundTrip(PrimitiveValue::Double(1e-100), DataType::DOUBLE);
  TestRoundTrip(PrimitiveValue::Float(3.1415), DataType::FLOAT);
  TestRoundTrip(PrimitiveValue::Float(100.0), DataType::FLOAT);
  TestRoundTrip(PrimitiveValue::Float(1e-37), DataType::FLOAT);
  TestRoundTrip(PrimitiveValue("foo"), DataType::BSON);
}

TEST(PrimitiveValueTest, TestEncoding) {
  TestEncoding(R"#("Sfoo\x00\x00")#", KeyEntryValue("foo"));
  TestEncoding(R"#("Sfoo\x00\x01bar\x01\x00\x00")#", KeyEntryValue(string("foo\0bar\x01", 8)));
  TestEncoding(R"#("I\x80\x00\x00\x00\x00\x00\x00{")#", KeyEntryValue::Int64(123L));
  TestEncoding(R"#("I\x00\x00\x00\x00\x00\x00\x00\x00")#",
      KeyEntryValue::Int64(std::numeric_limits<int64_t>::min()));
  TestEncoding(R"#("I\xff\xff\xff\xff\xff\xff\xff\xff")#",
      KeyEntryValue::Int64(std::numeric_limits<int64_t>::max()));

  // int32_t.
  TestEncoding(R"#("H\x80\x00\x00{")#", KeyEntryValue::Int32(123));
  TestEncoding(R"#("H\x00\x00\x00\x00")#",
               KeyEntryValue::Int32(std::numeric_limits<int32_t>::min()));
  TestEncoding(R"#("H\xff\xff\xff\xff")#",
               KeyEntryValue::Int32(std::numeric_limits<int32_t>::max()));

  // HybridTime encoding --------------------------------------------------------------------------

  TestEncoding(R"#("#\x80\xff\x05S\x1e\x85.\xbb52\x7fL")#",
               KeyEntryValue(HybridTime(1234567890123L, 3456)));

  TestEncoding(R"#("#\x80\x80\x80D")#",
               KeyEntryValue(HybridTime::FromMicros(kYugaByteMicrosecondEpoch)));

  // A little lower timestamp results in a little higher value that gets sorted later.
  TestEncoding(R"#("#\x80\x81\x80D")#",
               KeyEntryValue(HybridTime::FromMicros(kYugaByteMicrosecondEpoch - 1)));

  // On the other hand, with a higher timestamp, "~" is 0x7e, which is sorted earlier than 0x80.
  TestEncoding(R"#("#\x80~\x80D")#",
               KeyEntryValue(HybridTime::FromMicros(kYugaByteMicrosecondEpoch + 1)));

  TestEncoding(R"#("#\x80\xff\x05T=\xf7)\xbc\x18\x80K")#",
               KeyEntryValue(HybridTime::FromMicros(1000)));

  TestEncoding(
      R"#("ofoo\x00\x01bar\x01\x00\x00")#", KeyEntryValue::MakeBson(string("foo\0bar\x01", 8)));
}

TEST(PrimitiveValueTest, TestCompareStringsWithEmbeddedZeros) {
  const auto zero_char = PrimitiveValue(string("\x00", 1));
  const auto two_zero_chars = PrimitiveValue(string("\x00\x00", 2));

  ASSERT_EQ(zero_char, zero_char);
  ASSERT_EQ(two_zero_chars, two_zero_chars);

  ASSERT_LT(zero_char, two_zero_chars);
  ASSERT_GT(two_zero_chars, zero_char);
  ASSERT_NE(zero_char, two_zero_chars);
  ASSERT_NE(two_zero_chars, zero_char);

  ASSERT_FALSE(zero_char < zero_char);
  ASSERT_FALSE(zero_char > zero_char);
  ASSERT_FALSE(two_zero_chars < two_zero_chars);
  ASSERT_FALSE(two_zero_chars > two_zero_chars);
  ASSERT_FALSE(two_zero_chars < zero_char);
  ASSERT_FALSE(zero_char > two_zero_chars);
}


TEST(PrimitiveValueTest, TestPrimitiveValuesAsMapKeys) {
  map<PrimitiveValue, string> m;
  const PrimitiveValue key2("key2");
  const PrimitiveValue key1("key1");
  ASSERT_TRUE(m.emplace(key2, "value2").second);
  ASSERT_EQ(1, m.count(key2));
  ASSERT_NE(m.find(key2), m.end());
  ASSERT_TRUE(m.emplace(key1, "value1").second);
  ASSERT_EQ(1, m.count(key1));
  ASSERT_NE(m.find(key1), m.end());
}

TEST(PrimitiveValueTest, TestCorruption) {
  // No column id specified.
  KeyBytes key_bytes;
  key_bytes.AppendKeyEntryType(KeyEntryType::kColumnId);
  rocksdb::Slice slice = key_bytes.AsSlice();
  KeyEntryValue decoded;
  ASSERT_TRUE(decoded.DecodeFromKey(&slice).IsCorruption());

  // Invalid varint.
  key_bytes.AppendInt64(std::numeric_limits<int64_t>::max());
  ASSERT_TRUE(decoded.DecodeFromKey(&slice).IsCorruption());

  // kObsoleteIntentType without following byte
  key_bytes.Clear();
  key_bytes.AppendKeyEntryType(KeyEntryType::kObsoleteIntentType);
  slice = key_bytes.AsSlice();
  ASSERT_TRUE(decoded.DecodeFromKey(&slice).IsCorruption());
}

TEST(PrimitiveValueTest, TestVarintStorage) {
  // Verify varint occupies the appropriate amount of bytes.
  KeyBytes key_bytes;
  key_bytes.AppendColumnId(ColumnId(63));
  ASSERT_EQ(1, key_bytes.AsSlice().size());

  // 2 bytes for > 63 (total 3 = 1 + 2)
  key_bytes.AppendColumnId(ColumnId(64));
  ASSERT_EQ(3, key_bytes.AsSlice().size());

  key_bytes.Clear();
  key_bytes.AppendColumnId(ColumnId(std::numeric_limits<ColumnIdRep>::max()));
  ASSERT_EQ(5, key_bytes.AsSlice().size());
}

TEST(PrimitiveValueTest, TestRandomComparableColumnId) {
  Random r(0);
  for (int i = 0; i < 1000; i++) {
    ColumnId column_id1(r.Next() % (std::numeric_limits<ColumnIdRep>::max()));
    ColumnId column_id2(r.Next() % (std::numeric_limits<ColumnIdRep>::max()));
    CompareSlices(KeyEntryValue::MakeColumnId(column_id1).ToKeyBytes(),
                  KeyEntryValue::MakeColumnId(column_id2).ToKeyBytes(),
                  column_id1, column_id2, column_id1.ToString(), column_id2.ToString());
  }
}

TEST(PrimitiveValueTest, TestRandomComparableInt32) {
  Random r(0);
  for (int i = 0; i < 1000; i++) {
    int32_t val1 = r.Next32();
    int32_t val2 = r.Next32();
    CompareSlices(KeyEntryValue::Int32(val1).ToKeyBytes(),
                  KeyEntryValue::Int32(val2).ToKeyBytes(),
                  val1, val2,
                  std::to_string(val1), std::to_string(val2));
  }
}

TEST(PrimitiveValueTest, TestRandomComparableUInt64) {
  Random r(0);
  for (int i = 0; i < 1000; i++) {
    uint64_t val1 = r.Next64();
    uint64_t val2 = r.Next64();
    CompareSlices(KeyEntryValue::UInt64(val1).ToKeyBytes(),
                  KeyEntryValue::UInt64(val2).ToKeyBytes(),
                  val1, val2,
                  std::to_string(val1), std::to_string(val2));
  }
}

TEST(PrimitiveValueTest, TestRandomComparableUUIDs) {
  Random r(0);
  for (int i = 0; i < 1000; i++) {
    Uuid val1 (Uuid::Generate());
    Uuid val2 (Uuid::Generate());
    CompareSlices(KeyEntryValue::MakeUuid(val1).ToKeyBytes(),
                  KeyEntryValue::MakeUuid(val2).ToKeyBytes(),
                  val1, val2,
                  val1.ToString(), val2.ToString());
  }
}

TEST(PrimitiveValueTest, TestCopy) {
  TestCopy(PrimitiveValue::Int32(1000), 1000, 1000);
  TestCopy(PrimitiveValue("a"), 1000, 1000);
  TestCopy(PrimitiveValue::Double(3.0), 1000, 1000);
  TestCopy(PrimitiveValue::Int32(1000), 1000, 1000);
  TestCopy(PrimitiveValue::UInt64(1000), 1000, 1000);
  TestCopy(PrimitiveValue(Timestamp(1000)), 1000, 1000);
  InetAddress addr(ASSERT_RESULT(ParseIpAddress("1.2.3.4")));
  TestCopy(PrimitiveValue(addr), 1000, 1000);
}

TEST(PrimitiveValueTest, TestMove) {
  TestMove(PrimitiveValue::Int32(1000), 1000, 1000);
  TestMove(PrimitiveValue("a"), 1000, 1000);
  TestMove(PrimitiveValue::Double(3.0), 1000, 1000);
  TestMove(PrimitiveValue::Int32(1000), 1000, 1000);
  TestMove(PrimitiveValue::UInt64(1000), 1000, 1000);
  TestMove(PrimitiveValue(Timestamp(1000)), 1000, 1000);
  InetAddress addr(ASSERT_RESULT(ParseIpAddress("1.2.3.4")));
  TestMove(PrimitiveValue(addr), 1000, 1000);
}

// Ensures that the serialized version of a primitive value compares the same way as the primitive
// value.
void ComparePrimitiveValues(const KeyEntryValue& v1, const KeyEntryValue& v2) {
  LOG(INFO) << "Comparing primitive values: " << v1 << ", " << v2;
  KeyBytes k1;
  KeyBytes k2;
  v1.AppendToKey(&k1);
  v2.AppendToKey(&k2);
  ASSERT_EQ(v1 < v2, k1 < k2);
}

TEST(PrimitiveValueTest, TestAllTypesComparisons) {
  ComparePrimitiveValues(KeyEntryValue(RandomHumanReadableString(10)),
                         KeyEntryValue(RandomHumanReadableString(10)));

  ComparePrimitiveValues(
      KeyEntryValue::UInt64(RandomUniformInt<uint64_t>()),
      KeyEntryValue::UInt64(RandomUniformInt<uint64_t>()));

  ComparePrimitiveValues(KeyEntryValue::MakeTimestamp(Timestamp(RandomUniformInt<uint64_t>())),
                         KeyEntryValue::MakeTimestamp(Timestamp(RandomUniformInt<uint64_t>())));

  InetAddress addr1;
  InetAddress addr2;
  ASSERT_OK(addr1.FromSlice(RandomHumanReadableString(4)));
  ASSERT_OK(addr2.FromSlice(RandomHumanReadableString(4)));
  ComparePrimitiveValues(KeyEntryValue::MakeInetAddress(addr1),
                         KeyEntryValue::MakeInetAddress(addr2));

  ComparePrimitiveValues(KeyEntryValue::MakeUuid(Uuid::Generate()),
                         KeyEntryValue::MakeUuid(Uuid::Generate()));

  ComparePrimitiveValues(KeyEntryValue(HybridTime::FromMicros(RandomUniformInt<uint64_t>())),
                         KeyEntryValue(HybridTime::FromMicros(RandomUniformInt<uint64_t>())));

  ComparePrimitiveValues(
      KeyEntryValue(DocHybridTime(HybridTime::FromMicros(RandomUniformInt<uint64_t>()),
                                  RandomUniformInt<uint32_t>())),
      KeyEntryValue(DocHybridTime(HybridTime::FromMicros(RandomUniformInt<uint64_t>()),
                                  RandomUniformInt<uint32_t>())));

  ComparePrimitiveValues(
      KeyEntryValue::MakeColumnId(ColumnId(
          RandomUniformInt(0, std::numeric_limits<int32_t>::max()))),
      KeyEntryValue::MakeColumnId(ColumnId(
          RandomUniformInt(0, std::numeric_limits<int32_t>::max()))));

  ComparePrimitiveValues(
      KeyEntryValue::Double(RandomUniformReal<double>()),
      KeyEntryValue::Double(RandomUniformReal<double>()));

  ComparePrimitiveValues(
      KeyEntryValue::Float(RandomUniformReal<float>()),
      KeyEntryValue::Float(RandomUniformReal<float>()));

  ComparePrimitiveValues(
      KeyEntryValue::Decimal(std::to_string(RandomUniformReal<double>()), SortOrder::kAscending),
      KeyEntryValue::Decimal(std::to_string(RandomUniformReal<double>()), SortOrder::kAscending));

  ComparePrimitiveValues(
      KeyEntryValue::VarInt(std::to_string(RandomUniformInt<uint64_t>()), SortOrder::kAscending),
      KeyEntryValue::VarInt(std::to_string(RandomUniformInt<uint64_t>()), SortOrder::kAscending));

  ComparePrimitiveValues(
      KeyEntryValue::Int32(RandomUniformInt<int32_t>()),
      KeyEntryValue::Int32(RandomUniformInt<int32_t>()));

  ComparePrimitiveValues(
      KeyEntryValue::MakeBson(RandomHumanReadableString(10)),
      KeyEntryValue::MakeBson(RandomHumanReadableString(10)));
}

void ValidateEncodedKeyEntryType(DataType data_type) {
  KeyEntryValue key_entry_value;
  switch (data_type) {
    case DataType::NULL_VALUE_TYPE:
      key_entry_value = KeyEntryValue::NullValue(SortingType::kAscending);
      break;
    case DataType::BOOL:
      key_entry_value = KeyEntryValue(KeyEntryType::kTrue);
      break;
    case DataType::INT8: FALLTHROUGH_INTENDED;
    case DataType::INT16: FALLTHROUGH_INTENDED;
    case DataType::INT32:
      key_entry_value = KeyEntryValue::Int32(100);
      break;
    case DataType::FLOAT:
      key_entry_value = KeyEntryValue::Float(100.5f);
      break;
    case DataType::UINT32: FALLTHROUGH_INTENDED;
    case DataType::DATE:
      key_entry_value = KeyEntryValue::UInt32(100);
      break;
    case DataType::INT64: FALLTHROUGH_INTENDED;
    case DataType::TIME:
      key_entry_value = KeyEntryValue::Int64(100);
      break;
    case DataType::DOUBLE:
      key_entry_value = KeyEntryValue::Double(100.5);
      break;
    case DataType::UINT64:
      key_entry_value = KeyEntryValue::UInt64(100);
      break;
    case DataType::TIMESTAMP:
      key_entry_value = KeyEntryValue::MakeTimestamp(Timestamp(1000));
      break;
    case DataType::VECTOR: FALLTHROUGH_INTENDED;
    case DataType::BSON: FALLTHROUGH_INTENDED;
    case DataType::UUID: FALLTHROUGH_INTENDED;
    case DataType::TIMEUUID: FALLTHROUGH_INTENDED;
    case DataType::UNKNOWN_DATA: FALLTHROUGH_INTENDED;
    case DataType::STRING: FALLTHROUGH_INTENDED;
    case DataType::BINARY: FALLTHROUGH_INTENDED;
    case DataType::DECIMAL: FALLTHROUGH_INTENDED;
    case DataType::VARINT: FALLTHROUGH_INTENDED;
    case DataType::INET: FALLTHROUGH_INTENDED;
    case DataType::LIST: FALLTHROUGH_INTENDED;
    case DataType::MAP: FALLTHROUGH_INTENDED;
    case DataType::SET: FALLTHROUGH_INTENDED;
    case DataType::TUPLE: FALLTHROUGH_INTENDED;
    case DataType::TYPEARGS: FALLTHROUGH_INTENDED;
    case DataType::USER_DEFINED_TYPE: FALLTHROUGH_INTENDED;
    case DataType::FROZEN: FALLTHROUGH_INTENDED;
    case DataType::JSONB: FALLTHROUGH_INTENDED;
    case DataType::UINT8: FALLTHROUGH_INTENDED;
    case DataType::UINT16: FALLTHROUGH_INTENDED;
    case DataType::GIN_NULL:
      EXPECT_TRUE(false);
  }

  KeyBytes key_bytes;
  key_entry_value.AppendToKey(&key_bytes);

  ASSERT_EQ(key_bytes.size(), KeyEntryValue::GetEncodedKeyEntryValueSize(data_type))
      << "DataType: " << QLType::ToCQLString(data_type)
      << ", KeyBytes hex: " << key_bytes.AsSlice().ToDebugHexString()
      << ", KeyEntry value: " << key_entry_value.ToString();
}

TEST(PrimitiveValueTest, ValidateEncodedKeyEntryTypeSize) {
  for (auto data_type : kDataTypeArray) {
    if (KeyEntryValue::GetEncodedKeyEntryValueSize(data_type)) {
      ValidateEncodedKeyEntryType(data_type);
    } else {
      // TYPEARGS, GIN_NULL and UNKNOWN_DATA are undefiened types. And Key entry value doesn't
      // support uint8 and uint16.
      if (data_type != DataType::TYPEARGS && data_type != DataType::GIN_NULL &&
          data_type != DataType::UNKNOWN_DATA && data_type != DataType::UINT8 &&
          data_type != DataType::UINT16) {
        auto type_info = GetTypeInfo(data_type);
        ASSERT_TRUE(type_info) << "DataType: " << QLType::ToCQLString(data_type);
        ASSERT_TRUE(type_info->var_length())
            << "Fixed datatype expected varlength: " << QLType::ToCQLString(data_type);
      }
    }
  }
}

TEST(PrimitiveValueTest, RandomQLValuePBToKeyEntryEncoding) {
  constexpr size_t kIterations = ReleaseVsDebugVsAsanVsTsan(100, 10, 1, 1) * 10000;
  std::mt19937_64 rng(42);
  KeyBytes key_bytes;
  Arena arena;
  for (auto i = 0; i != kIterations; ++i) {
    if (i % 1000 == 0) {
      LOG(INFO) << "Iteration: " << i;
    }
    auto value = RandomQLValuePB(rng);
    for (auto sorting_type : SortingTypeList()) {
      key_bytes.Clear();
      arena.Reset(ResetMode::kKeepFirst);
      auto key_entry_value = KeyEntryValue::FromQLValuePBForKey(value, sorting_type);
      key_entry_value.AppendToKey(&key_bytes);
      auto encoded_slice = EncodedKeyEntryValue(arena, value, sorting_type);
      ASSERT_EQ(key_bytes.AsSlice(), encoded_slice) << "Value: " << AsString(value);
    }
  }
}

}  // namespace yb::dockv
