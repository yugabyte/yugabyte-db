//
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
//

#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "yb/gutil/macros.h"

#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"

using namespace std::literals;

namespace yb {

namespace {

class Custom {
 public:
  explicit Custom(int v) : value_(v) {}

  Custom(const Custom&) = delete;
  void operator=(const Custom&) = delete;

  std::string ToString() const {
    return Format("{ Custom: $0 }", value_);
  }
 private:
  int value_;
};

} // namespace

TEST(FormatTest, Number) {
  ASSERT_EQ("Is it 1984?", Format("Is it $0?", 1984));
  ASSERT_EQ("-2147483648 2147483647",
            Format("$0 $1", std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
  ASSERT_EQ("-32768 32767",
            Format("$0 $1", std::numeric_limits<int16_t>::min(),
                   std::numeric_limits<int16_t>::max()));
  ASSERT_EQ("0 4294967295", Format("$0 $1", uint32_t{0}, std::numeric_limits<uint32_t>::max()));
  ASSERT_EQ("18446744073709551615", Format("$0", std::numeric_limits<size_t>::max()));
  ASSERT_EQ("-962394729", Format("$0", ptrdiff_t{-962394729}));
  ASSERT_EQ("-128 127", Format("$0 $1", int8_t{-128}, int8_t{127}));
}

TEST(FormatTest, Placeholders) {
  ASSERT_EQ("7 == 7, right?", Format("$0 == $0, right?", 7));
  ASSERT_EQ("b a", Format("$1 $0", "a", "b"));
  ASSERT_EQ("$0 costs $5", Format("$$0 costs $$$0", 5));
  ASSERT_EQ("no placeholders", Format("no placeholders", 1));
}

TEST(FormatTest, String) {
  const char* pointer = "Pointer";
  char array[] = "Array";
  std::string string = "String";
  ASSERT_EQ("Literal Pointer Array String Temp View",
            Format("$0 $1 $2 $3 $4 $5", "Literal", pointer, &array[0], string, "Temp"s,
                   std::string_view("View")));
  ASSERT_EQ("[zero\0zero]"s, Format("[$0]", "zero\0zero"s));
  ASSERT_EQ("[Embedded\0zero]"s, Format("[$0]", std::string_view("Embedded\0zero", 13)));
  ASSERT_EQ("[][]", Format("[$0][$1]", std::string_view(), std::string()));
}

// Format respects the actual size of an array that has no terminating '\0'.
TEST(FormatTest, Array) {
  union {
    char data[10] = "head-tail";
    char head[4];
  } sub_array_accesor;
  ASSERT_EQ("This should be head only",
            Format("This should be $0 only", sub_array_accesor.head));
}

TEST(FormatTest, Collections) {
  ASSERT_EQ("Is it [1, 2, 3]?", Format("Is it $0?", std::vector<int>{1, 2, 3}));
  ASSERT_EQ("[{1, one}, {2, two}]",
            Format("$0", std::map<int, std::string>{{1, "one"}, {2, "two"}}));
}

TEST(FormatTest, MultiArgsTwoDigit) {
  ASSERT_EQ(
      Format("$0 $1 $2 $3 $4 $5 $6 $7 $8 $9 $10 $11", 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, "A", "B"),
      "0 1 2 3 4 5 6 7 8 9 A B");
}

TEST(FormatTest, Custom) {
  Custom value(42);
  ASSERT_EQ("Value is { Custom: 42 }", Format("Value is $0", value));
}

TEST(FormatTest, Time) {
  ASSERT_EQ("Time: 10.000s", Format("Time: $0", 10s));
  ASSERT_EQ("Time: 0.001s", Format("Time: $0", 1ms));
  std::ostringstream out;
  // libc++ that comes with LLVM 17 defines stream output operators for std::duration, so we
  // convert the duration to MonoDelta for consistency.
  out << MonoDelta(15s);
  ASSERT_EQ("15.000s", out.str());
}

namespace {

std::string Printf(const char* format, ...) __attribute__((format(printf, 1, 2)));

std::string Printf(const char* format, ...) {
  char buffer[128];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  return buffer;
}

} // namespace

TEST(FormatTest, FixedPoint) {
  for (double value : {0.0, -0.0, 1.0, 0.5, 2.675, -3.14159, 1e-9, 123456789.987654321}) {
    for (int precision : {0, 1, 3, 10}) {
      ASSERT_EQ(Printf("%.*f", precision, value), FixedPoint(value, precision));
    }
  }
  ASSERT_EQ("Elapsed 1.500 s", Format("Elapsed $0 s", FixedPoint(1.5, 3)));
}

TEST(FormatTest, Scientific) {
  for (double value : {0.0, 1.0, -2.5e-12, 6.02214076e23}) {
    ASSERT_EQ(Printf("%E", value), Scientific(value));
    ASSERT_EQ(Printf("%.2E", value), Scientific(value, 2));
  }
}

TEST(FormatTest, Pad) {
  ASSERT_EQ("   ab", PadLeft("ab", 5));
  ASSERT_EQ("ab   ", PadRight("ab", 5));
  ASSERT_EQ("**ab", PadLeft("ab", 4, '*'));
  ASSERT_EQ("abcdef", PadLeft("abcdef", 3));
  ASSERT_EQ("abcdef", PadRight("abcdef", 3));
  ASSERT_EQ("", PadLeft("", 0));
  ASSERT_EQ(Printf("%6zu", size_t{42}), PadLeft(ToString(42), 6));
  ASSERT_EQ(Printf("%-8s|", "xy"), PadRight("xy", 8) + "|");
}

TEST(FormatTest, ZeroPadded) {
  for (int value : {0, 7, -7, 42, -42, 123456, std::numeric_limits<int>::min(),
                    std::numeric_limits<int>::max()}) {
    for (int width : {0, 1, 3, 6, 12}) {
      ASSERT_EQ(Printf("%0*d", width, value), ZeroPadded(value, width));
    }
  }
  ASSERT_EQ(Printf("%09" PRIu64, std::numeric_limits<uint64_t>::max()),
            ZeroPadded(std::numeric_limits<uint64_t>::max(), 9));
  ASSERT_EQ(Printf("%06" PRId64, int64_t{-12}), ZeroPadded(int64_t{-12}, 6));
}

TEST(FormatTest, HexString) {
  for (unsigned value : {0u, 1u, 0xau, 0xffu, 0x1234u, 0xdeadbeefu}) {
    for (int width : {0, 2, 4, 10}) {
      ASSERT_EQ(Printf("%0*x", width, value), HexString(value, width));
    }
  }
  ASSERT_EQ(Printf("%x", -1), HexString(-1));
  ASSERT_EQ(Printf("%" PRIx64, std::numeric_limits<uint64_t>::max()),
            HexString(std::numeric_limits<uint64_t>::max()));
  ASSERT_EQ("ff", HexString(int8_t{-1}));
  ASSERT_EQ("0x0042", Format("0x$0", HexString(0x42, 4)));
}

} // namespace yb
