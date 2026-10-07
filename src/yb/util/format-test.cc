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

#include <cctype>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
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

template<class T>
std::string ArgToString(const T& arg) {
  if constexpr (std::is_integral_v<T>) {
    return std::to_string(arg);
  } else if constexpr (std::is_convertible_v<const T&, std::string_view>) {
    return std::string(std::string_view(arg));
  } else {
    return ToString(arg);
  }
}

// Straightforward implementation of the $N placeholder syntax, used as the expected result.
std::string ReferenceFormat(const std::string& format, const std::vector<std::string>& args) {
  std::string result;
  for (size_t i = 0; i < format.size(); ++i) {
    if (format[i] != '$' || i + 1 == format.size()) {
      result += format[i];
    } else if (format[i + 1] == '$') {
      result += '$';
      ++i;
    } else if (isdigit(format[i + 1])) {
      result += args.at(format[i + 1] - '0');
      ++i;
    }
  }
  return result;
}

template<class... Args>
std::string ReferenceFormat(const std::string& format, const Args&... args) {
  return ReferenceFormat(format, std::vector<std::string>{ArgToString(args)...});
}

template<class... Args>
void CheckPlain(const std::string& format, Args&&... args) {
  ASSERT_EQ(ReferenceFormat(format, args...), Format(format, std::forward<Args>(args)...));
}

template<class T>
void CheckInt(const std::string& format, const T& t) {
  CheckPlain(format, t);
  CheckPlain(format, std::numeric_limits<T>::min());
  CheckPlain(format, std::numeric_limits<T>::max());
}

template<class Collection>
void CheckCollection(const std::string& format, const Collection& collection) {
  ASSERT_EQ(ReferenceFormat(format, ToString(collection)), Format(format, collection));
}

std::vector<std::string> kFormats = { "Is it $0?", "Yes, it is $0", "$0 == $0, right?"};
std::string kLongFormat = "We have format of $0 and $1, may be also $2";

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

double ClocksToMs(std::clock_t clocks) {
  return 1000.0 * clocks / CLOCKS_PER_SEC;
}

template<class... Args>
void CheckSpeed(const std::string& format, Args&&... args) {
#ifdef THREAD_SANITIZER
  const size_t kCycles = 5000;
#else
  const size_t kCycles = 500000;
#endif
  const size_t kMeasurements = 10;
  std::vector<std::clock_t> reference_times, format_times;
  reference_times.reserve(kMeasurements);
  format_times.reserve(kMeasurements);
  for (size_t m = 0; m != kMeasurements; ++m) {
    const auto start = std::clock();
    for (size_t i = 0; i != kCycles; ++i) {
      ReferenceFormat(format, args...);
    }
    const auto mid = std::clock();
    for (size_t i = 0; i != kCycles; ++i) {
      Format(format, std::forward<Args>(args)...);
    }
    const auto stop = std::clock();
    reference_times.push_back(mid - start);
    format_times.push_back(stop - mid);
  }
  std::sort(reference_times.begin(), reference_times.end());
  std::sort(format_times.begin(), format_times.end());
  std::clock_t reference_time = 0;
  std::clock_t format_time = 0;
  size_t count = 0;
  for (size_t i = kMeasurements / 4; i != kMeasurements * 3 / 4; ++i) {
    reference_time += reference_times[i];
    format_time += format_times[i];
    ++count;
  }
  reference_time /= count;
  format_time /= count;
  if (format_time > reference_time) {
    LOG(INFO) << Format("Format times: $0, reference times: $1", format_times, reference_times);
  }
  LOG(INFO) << "Performance results for [[ "
            << Format(format, std::forward<Args>(args)...) << " ]]: "
            << "reference: " << ClocksToMs(reference_time) << "ms, "
            << "format: " << ClocksToMs(format_time) << "ms";

  // Format should not be much slower than the naive reference implementation.
  ASSERT_PERF_LE(format_time, reference_time * 3);
}

} // namespace

TEST(FormatTest, Number) {
  for (const auto& format : kFormats) {
    CheckInt<int>(format, 1984);
    CheckInt<int16>(format, 2349);
    CheckInt<uint32_t>(format, 23984296);
    CheckInt<size_t>(format, 2936429238477);
    CheckInt<ptrdiff_t>(format, -962394729);
    CheckInt<int8_t>(format, 45);
  }
}

TEST(FormatTest, String) {
  for (const auto& format : kFormats) {
    CheckPlain(format, "YugaByte");
    const char* pointer = "Pointer";
    CheckPlain(format, pointer);
    char array[] = "Array";
    CheckPlain(format, &array[0]);
    std::string string = "String";
    CheckPlain(format, string);
    CheckPlain(format, "TempString"s);
  }
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
  for (const auto& format : kFormats) {
    CheckCollection<std::vector<int>>(format, {1, 2, 3});
    CheckCollection<std::unordered_map<int, std::string>>(format,
                                                          {{1, "one"}, {2, "two"}, {3, "three"}});
  }
}

TEST(FormatTest, MultiArgs) {
  CheckPlain(kLongFormat, 5, "String", "zero\0zero"s);
}

TEST(FormatTest, MultiArgsTwoDigit) {
  ASSERT_EQ(
      Format("$0 $1 $2 $3 $4 $5 $6 $7 $8 $9 $10 $11", 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, "A", "B"),
      "0 1 2 3 4 5 6 7 8 9 A B");
}

TEST(FormatTest, Custom) {
  for (const auto& format : kFormats) {
    Custom value(42);
    ASSERT_EQ(ReferenceFormat(format, value.ToString()), Format(format, value));
  }
}

TEST(FormatTest, Performance) {
  CheckSpeed(kLongFormat, 1, 2, 3);
  CheckSpeed(kLongFormat, 5, "String", "zero\0zero"s);
  CheckSpeed("Connection ($0) $1 $2 => $3",
             static_cast<void*>(this),
             "client",
             "127.0.0.1:12345"s,
             "127.0.0.1:9042"s);
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
