//
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
//

#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "yb/gutil/macros.h"

#include "yb/util/monotime.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"

using namespace std::literals;

namespace yb {

namespace {

template<class... Args>
void CheckPlain(const std::string& format, Args&&... args) {
  ASSERT_EQ(strings::Substitute(format, std::forward<Args>(args)...),
            Format(format, std::forward<Args>(args)...));
}

template<class T>
void CheckInt(const std::string& format, const T& t) {
  CheckPlain(format, t);
  CheckPlain(format, std::numeric_limits<T>::min());
  CheckPlain(format, std::numeric_limits<T>::max());
}

template<class Collection>
void CheckCollection(const std::string& format, const Collection& collection) {
  ASSERT_EQ(strings::Substitute(format, ToString(collection)), Format(format, collection));
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
  std::vector<std::clock_t> substitute_times, format_times;
  substitute_times.reserve(kMeasurements);
  format_times.reserve(kMeasurements);
  for (size_t m = 0; m != kMeasurements; ++m) {
    const auto start = std::clock();
    for (size_t i = 0; i != kCycles; ++i) {
      strings::Substitute(format, std::forward<Args>(args)...);
    }
    const auto mid = std::clock();
    for (size_t i = 0; i != kCycles; ++i) {
      Format(format, std::forward<Args>(args)...);
    }
    const auto stop = std::clock();
    substitute_times.push_back(mid - start);
    format_times.push_back(stop - mid);
  }
  std::sort(substitute_times.begin(), substitute_times.end());
  std::sort(format_times.begin(), format_times.end());
  std::clock_t substitute_time = 0;
  std::clock_t format_time = 0;
  size_t count = 0;
  for (size_t i = kMeasurements / 4; i != kMeasurements * 3 / 4; ++i) {
    substitute_time += substitute_times[i];
    format_time += format_times[i];
    ++count;
  }
  substitute_time /= count;
  format_time /= count;
  if (format_time > substitute_time) {
    LOG(INFO) << Format("Format times: $0, substitute times: $1", format_times, substitute_times);
  }
  LOG(INFO) << "Performance results for [[ "
            << Format(format, std::forward<Args>(args)...) << " ]]: "
            << "substitute: " << ClocksToMs(substitute_time) << "ms, "
            << "format: " << ClocksToMs(format_time) << "ms";

  // Check that Format and Substitute differ by a factor within a certain range.
  ASSERT_PERF_LE(format_time, substitute_time * 3);
  ASSERT_PERF_LE(substitute_time, format_time * 10);
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

// strings::Substitute ignores actual size of array.
// That is why CheckPlain helper can't be used to check Format with array argument without '\0'.
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
    ASSERT_EQ(strings::Substitute(format, value.ToString()), Format(format, value));
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

} // namespace yb
