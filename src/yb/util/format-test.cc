//
// Copyright (c) YugaByte, Inc.
//

#include <deque>
#include <list>
#include <map>
#include <vector>
#include <unordered_map>

#include "yb/gutil/strings/substitute.h"

#include "yb/util/test_util.h"
#include "yb/util/format.h"

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
  ASSERT_EQ(strings::Substitute(format, util::ToString(collection)), Format(format, collection));
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

template<class... Args>
void CheckSpeed(const std::string& format, Args&&... args) {
  const size_t kCycles = 1000000;
  auto start = std::chrono::steady_clock::now();
  for (size_t i = 0; i != kCycles; ++i) {
    strings::Substitute(format, std::forward<Args>(args)...);
  }
  auto mid = std::chrono::steady_clock::now();
  for (size_t i = 0; i != kCycles; ++i) {
    Format(format, std::forward<Args>(args)...);
  }
  auto end = std::chrono::steady_clock::now();
  auto substitute_time = std::chrono::duration_cast<std::chrono::milliseconds>(mid - start);
  auto format_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - mid);
  LOG(INFO) << Format(format, std::forward<Args>(args)...)
            << ", substitute: " << substitute_time.count() << "ms, "
            << "format: " << format_time.count() << "ms" << std::endl;
  // Check that format is at least as good as substitute
  ASSERT_PERF_LE(format_time, substitute_time);
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

} // namespace yb
