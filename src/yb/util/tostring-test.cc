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

#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <vector>

#include <boost/uuid/uuid_io.hpp>
#include <gtest/gtest.h>

#include "yb/gutil/ref_counted.h"
#include "yb/gutil/strings/stringpiece.h"

#include "yb/util/monotime.h"
#include "yb/util/tostring.h"
#include "yb/util/uuid.h"

namespace yb {
// We should use namespace other than yb::ToString to check how does ToString works
// with code from other namespaces.
namespace util_test {

using yb::ToString;

namespace {

template<class T>
void CheckPlain(T t) {
  ASSERT_EQ(std::to_string(t), ToString(t));
}

template<class T>
void CheckInt(T t) {
  CheckPlain<T>(t);
  CheckPlain<T>(std::numeric_limits<T>::min());
  CheckPlain<T>(std::numeric_limits<T>::max());
}

template<TypeForToStringAsPointer T>
std::string PointerValueAsString(const T& t) {
  std::stringstream ss;
  ss << "0x" << std::setw(sizeof(void *) * 2) << std::setfill('0') << std::setbase(16)
     << reinterpret_cast<size_t>(t);
  return ss.str();
}

template<class T>
void CheckPointer(const std::string& tail, const T& t) {
  const auto expected = t ? (PointerValueAsString(&*t) + " -> " + tail) : tail;
  ASSERT_EQ(expected, ToString(t));
}

} // namespace

TEST(ToStringTest, TestNumber) {
  CheckInt<int>(1984);
  CheckInt<int16>(2349);
  CheckInt<uint32_t>(23984296);
  CheckInt<size_t>(2936429238477);
  CheckInt<ptrdiff_t>(-962394729);
  CheckInt<int8_t>(45);
  ASSERT_EQ("1.23456789", ToString(1.234567890));
  ASSERT_EQ("1", ToString(1.0));
  ASSERT_EQ("1.5", ToString(1.5f));
}

TEST(ToStringTest, TestCollection) {
  const std::string expected = "[1, 2, 3, 4, 5]";
  std::vector<int> v = {1, 2, 3, 4, 5};
  ASSERT_EQ(expected, ToString(v));
  CheckPointer(expected, &v);

  std::deque<int> d(v.begin(), v.end());
  ASSERT_EQ(expected, ToString(d));
  CheckPointer(expected, &d);

  std::list<int> l(v.begin(), v.end());
  ASSERT_EQ(expected, ToString(l));
  CheckPointer(expected, &l);
}

TEST(ToStringTest, TestMap) {
  std::map<int, std::string> m = {{1, "one"}, {2, "two"}, {3, "three"}};
  ASSERT_EQ("[{1, one}, {2, two}, {3, three}]", ToString(m));

  std::unordered_map<int, std::string> u(m.begin(), m.end());
  auto uts = ToString(u);
  std::vector<std::pair<int, std::string>> v(m.begin(), m.end());
  size_t match_count = 0;
  for (;;) {
    if (uts == ToString(v)) {
      ++match_count;
    }
    if (!std::next_permutation(v.begin(), v.end())) {
      break;
    }
  }
  ASSERT_EQ(1, match_count);
}

TEST(ToStringTest, TestPointer) {
  const char* some_text = "some text";

  ASSERT_EQ(some_text, ToString(some_text));
  int* null_int = nullptr;
  CheckPointer("<NULL>", null_int);

  std::string expected = "23";
  int number = 23;
  CheckPointer(expected, &number);

  std::unique_ptr<int> unique_ptr(new int(number));
  CheckPointer(expected, unique_ptr);

  std::shared_ptr<int> shared_ptr = std::make_shared<int>(number);
  CheckPointer(expected, shared_ptr);

  void* void_ptr = &number;
  ASSERT_EQ(PointerValueAsString(void_ptr), ToString(void_ptr));

  const void* const_void_ptr = some_text;
  ASSERT_EQ(PointerValueAsString(const_void_ptr), ToString(const_void_ptr));
}

const std::string kShortDebugString = "ShortDebugString";
const std::string kToStringable = "ToStringable";

class ToStringable : public RefCountedThreadSafe<ToStringable> {
 public:
  std::string ToString() const {
    return kToStringable;
  }
};

class ToStringableChild : public ToStringable {
};

class WithShortDebugString {
 public:
  std::string ShortDebugString() const {
    return kShortDebugString;
  }
};

class WithShortDebugStringChild : public WithShortDebugString {
};

TEST(ToStringTest, TestCustomIntrusive) {
  scoped_refptr<ToStringable> ptr(new ToStringable);
  scoped_refptr<ToStringableChild> child_ptr(new ToStringableChild);
  ASSERT_EQ(kToStringable, ToString(*ptr));
  CheckPointer(kToStringable, ptr);
  CheckPointer(kToStringable, child_ptr);
  ASSERT_EQ(kShortDebugString, ToString(WithShortDebugString()));
  ASSERT_EQ(kShortDebugString, ToString(WithShortDebugStringChild()));

  std::vector<scoped_refptr<ToStringable>> v(2);
  v[1] = ptr;
  ASSERT_EQ("[<NULL>, " + ToString(v[1]) + "]", ToString(v));

  ASSERT_EQ(kToStringable, ToString(GStringPiece(kToStringable)));
}

class ToStringableNonIntrusive {
};

std::string ToString(ToStringableNonIntrusive) {
  return "ToStringableNonIntrusive";
}

TEST(ToStringTest, TestCustomNonIntrusive) {
  std::vector<ToStringableNonIntrusive> v(2);
  ASSERT_EQ("ToStringableNonIntrusive", ToString(v[0]));
  ASSERT_EQ("[ToStringableNonIntrusive, ToStringableNonIntrusive]", ToString(v));
}

struct Outputable {
  int value;

  explicit Outputable(int v) : value(v) {}
};

std::ostream& operator<<(std::ostream& out, const Outputable& outputable) {
  return out << "Outputable(" << outputable.value << ")";
}

class ToStringableAndOutputable {
 public:
  std::string ToString() const {
    return "ToString";
  }
};

std::ostream& operator<<(std::ostream& out, const ToStringableAndOutputable& outputable) {
  return out << "operator<<";
}

TEST(ToStringTest, LexicalCast) {
  std::vector<Outputable> v = { Outputable(1), Outputable(2) };

  ASSERT_EQ("Outputable(1)", ToString(v[0]));
  ASSERT_EQ("[Outputable(1), Outputable(2)]", ToString(v));
  ASSERT_EQ("ToString", ToString(ToStringableAndOutputable()));
  ASSERT_EQ("0.000s", ToString(MonoDelta::kZero));
}

TEST(ToStringTest, Uuid) {
  const auto id = Uuid::Generate();
  auto str = to_string(id.impl());
  std::vector<boost::uuids::uuid> vec = {id.impl()};

  ASSERT_EQ(ToString(id), str);
  ASSERT_EQ(ToString(vec), ToString(std::vector<std::string>{str}));
}

TEST(ToStringTest, Struct) {
  struct TestStruct {
    int a;
    std::string b;
    std::vector<int> c;

    std::string ToString() const {
      return YB_STRUCT_TO_STRING(a, b, c);
    }
  };

  TestStruct t = {
    .a = 42,
    .b = "test",
    .c = {1, 2, 3},
  };

  ASSERT_EQ(t.ToString(), "{ a: 42 b: test c: [1, 2, 3] }");

  class TestClass {
   public:
    TestClass(int a, std::string b) : a_(a), b_(std::move(b)) {}

    std::string ToString() const {
      return YB_CLASS_TO_STRING(a, b);
    }

   private:
    int a_;
    std::string b_;
  };

  ASSERT_EQ(TestClass(42, "test").ToString(), "{ a: 42 b: test }");
}

TEST(ToStringTest, Optional) {
  using Opt = std::optional<int>;
  Opt i{10};
  const auto& ci = i;

  ASSERT_EQ(ToString(i), "10");
  ASSERT_EQ(ToString(ci), "10");
  ASSERT_EQ(ToString(std::vector<Opt>{10, std::nullopt, 20}), "[10, <nullopt>, 20]");
  ASSERT_EQ(ToString(std::tuple<Opt, Opt, Opt>{10, std::nullopt, 20}), "{10, <nullopt>, 20}");
}

TEST(ToStringTest, TupleLikeType) {
  ASSERT_EQ(ToString(std::pair<int, int>{1, 2}), "{1, 2}");
  ASSERT_EQ(ToString(std::pair<const int, std::pair<int, int>>{1, {2, 3}}), "{1, {2, 3}}");
  ASSERT_EQ(ToString(std::tuple<int, int, std::string>{1, 2, "three"}), "{1, 2, three}");
  ASSERT_EQ(ToString(std::array<int, 3>{1, 2, 3}), "{1, 2, 3}");
}

TEST(ToStringTest, ReferenceWrapper) {
  {
    int i = 1;
    int j = 2;
    int k = 3;
    std::vector<std::reference_wrapper<int>> values = {i, j, k};
    ASSERT_EQ(ToString(values), "[1, 2, 3]");
  }

  {
    std::optional<int> value;
    std::reference_wrapper<std::optional<int>> ref{value};
    ASSERT_EQ(ToString(ref), ToString(value));
  }

  {
    std::vector<int> values{1, 2, 3};
    std::reference_wrapper<std::vector<int>> ref{values};
    ASSERT_EQ(ToString(ref), ToString(values));
  }
}

} // namespace util_test
} // namespace yb
