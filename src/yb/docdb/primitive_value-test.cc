// Copyright (c) YugaByte, Inc.

#include "yb/docdb/primitive_value.h"

#include <limits>

#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/bytes_formatter.h"
#include "yb/gutil/strings/substitute.h"

using std::string;
using std::numeric_limits;
using strings::Substitute;

namespace yb {
namespace docdb {

namespace {

void EncodeAndDecode(const PrimitiveValue& primitive_value) {
  KeyBytes key_bytes = primitive_value.ToKeyBytes();
  PrimitiveValue decoded;
  rocksdb::Slice slice = key_bytes.AsSlice();
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
  ASSERT_EQ(primitive_value.ToString(), decoded.ToString());
}

void TestEncoding(const char* expected_str, const PrimitiveValue& primitive_value) {
  ASSERT_STR_EQ_VERBOSE_TRIMMED(expected_str, primitive_value.ToKeyBytes().ToString());
}

}  // unnamed namespace

TEST(PrimitiveValueTest, TestToString) {
  ASSERT_EQ("\"foo\"", PrimitiveValue("foo").ToString());
  ASSERT_EQ("\"foo\\\"\\x00\\x01\\x02\\\"bar\"",
      PrimitiveValue(string("foo\"\x00\x01\x02\"bar", 11)).ToString());

  ASSERT_EQ("123456789000", PrimitiveValue(123456789000l).ToString());
  ASSERT_EQ("-123456789000", PrimitiveValue(-123456789000l).ToString());
  ASSERT_EQ("9223372036854775807",
      PrimitiveValue(numeric_limits<int64_t>::max()).ToString());
  ASSERT_EQ("-9223372036854775808",
      PrimitiveValue(numeric_limits<int64_t>::min()).ToString());

  ASSERT_EQ("3.1415", PrimitiveValue::Double(3.1415).ToString());
  ASSERT_EQ("100.0", PrimitiveValue::Double(100.0).ToString());
  ASSERT_EQ("1.000000E-100", PrimitiveValue::Double(1e-100).ToString());

  ASSERT_EQ("ArrayIndex(123)", PrimitiveValue::ArrayIndex(123).ToString());
  ASSERT_EQ("ArrayIndex(-123)", PrimitiveValue::ArrayIndex(-123).ToString());

  ASSERT_EQ("TS(1002003004005006007)",
      PrimitiveValue(Timestamp(1002003004005006007L)).ToString());

  // Timestamps use an unsigned 64-bit integer as an internal representation.
  ASSERT_EQ("TS(0)", PrimitiveValue(Timestamp(0)).ToString());
  ASSERT_EQ("TS(18446744073709551615)",
      PrimitiveValue(Timestamp(numeric_limits<uint64_t>::max())).ToString());
  ASSERT_EQ("TS(18446744073709551615)",
    PrimitiveValue(Timestamp(-1)).ToString());

  ASSERT_EQ("UInt32Hash(4294967295)",
      PrimitiveValue::UInt32Hash(numeric_limits<uint32_t>::max()).ToString());
  ASSERT_EQ("UInt32Hash(4294967295)", PrimitiveValue::UInt32Hash(-1).ToString());
  ASSERT_EQ("UInt32Hash(0)", PrimitiveValue::UInt32Hash(0).ToString());
}

TEST(PrimitiveValueTest, TestRoundTrip) {
  for (auto primitive_value : {
      PrimitiveValue("foo"),
      PrimitiveValue(string("foo\0bar\x01", 8)),
      PrimitiveValue(123L)
  }) {
    EncodeAndDecode(primitive_value);
  }
}

TEST(PrimitiveValueTest, TestEncoding) {
  TestEncoding(R"#("\x04foo\x00\x00")#", PrimitiveValue("foo"));
  TestEncoding(R"#("\x04foo\x00\x01bar\x01\x00\x00")#", PrimitiveValue(string("foo\0bar\x01", 8)));
  TestEncoding(R"#("\x05\x80\x00\x00\x00\x00\x00\x00{")#", PrimitiveValue(123L));
  TestEncoding(R"#("\x05\x00\x00\x00\x00\x00\x00\x00\x00")#",
      PrimitiveValue(std::numeric_limits<int64_t>::min()));
  TestEncoding(R"#("\x05\xff\xff\xff\xff\xff\xff\xff\xff")#",
      PrimitiveValue(std::numeric_limits<int64_t>::max()));
}

}
}
