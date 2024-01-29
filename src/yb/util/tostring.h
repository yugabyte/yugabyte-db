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

#pragma once

#include <float.h>

#include <chrono>
#include <concepts>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <boost/preprocessor/facilities/apply.hpp>
#include <boost/preprocessor/if.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>

#include "yb/gutil/strings/numbers.h"

#include "yb/util/type_traits.h"

// We should use separate namespace for some checkers.
// Because there could be cases when operator<< is available in yb namespace, but
// unavailable to boost::lexical_cast.
// For instance MonoDelta is implicitly constructible from std::chrono::duration
// and has operator<<.
// So when we are trying to convert std::chrono::duration to string, SupportsOutputToStream
// reports true, but boost::lexical_cast cannot output std::chrono::duration to stream.
// Because such operator<< could not be found using ADL.
namespace yb_tostring {

template <class T>
concept TypeSupportsOutputToStream = requires (const T& t) {
  (*static_cast<std::ostream*>(nullptr)) << t;
}; // NOLINT

template <class T>
concept TypeWithFree_to_string = requires (const T& t) {
  to_string(t);
}; // NOLINT

} // namespace yb_tostring

namespace yb {

// If class has ToString member function - use it.
template <class T>
concept TypeWith_ToString = requires (const T& t) {
  t.ToString();
}; // NOLINT

template <class T>
concept TypeWith_to_string = requires (const T& t) {
    t.to_string();
}; // NOLINT

template <TypeWith_ToString T>
decltype(auto) ToString(const T& value) {
  return value.ToString();
}

template <TypeWith_to_string T>
decltype(auto) ToString(const T& value) {
  return value.to_string();
}

// If class has ShortDebugString member function - use it. For protobuf classes mostly.
template <class T>
concept TypeWith_ShortDebugString = requires (const T& t) {
  t.ShortDebugString();
}; // NOLINT

template <TypeWith_ShortDebugString T>
decltype(auto) ToString(const T& value) {
  return value.ShortDebugString();
}

// Various variants of integer types.
template <class Int>
requires(std::is_signed_v<Int>)
decltype(auto) IntToBuffer(Int value, char* buffer) {
  if constexpr (sizeof(Int) > 4) {
    return FastInt64ToBufferLeft(value, buffer);
  } else {
    return FastInt32ToBufferLeft(value, buffer);
  }
}

template <class UInt>
requires(!std::is_signed_v<UInt>)
decltype(auto) IntToBuffer(UInt value, char* buffer) {
  if constexpr (sizeof(UInt) > 4) {
    return FastUInt64ToBufferLeft(value, buffer);
  } else {
    return FastUInt32ToBufferLeft(value, buffer);
  }
}

template <class Int>
requires(std::is_integral_v<Int>)
std::string ToString(const Int& value) {
  char buffer[kFastToBufferSize];
  auto end = IntToBuffer(value, buffer);
  return {buffer, end};
}

template <class Float>
requires(std::is_floating_point_v<Float>)
std::string ToString(const Float& value) {
  char buffer[DBL_DIG + 10];
  snprintf(buffer, sizeof (buffer), "%.*g", DBL_DIG, value);
  return buffer;
}

template <class Pointer>
struct PointerToString {
  template<class P>
  static std::string Apply(const P& ptr);
};

template <>
struct PointerToString<const void*> {
  static std::string Apply(const void* ptr) {
    if (ptr) {
      char buffer[kFastToBufferSize]; // kFastToBufferSize has enough extra capacity for 0x
      buffer[0] = '0';
      buffer[1] = 'x';
      FastHex64ToBuffer(reinterpret_cast<size_t>(ptr), buffer + 2);
      return buffer;
    } else {
      return "<NULL>";
    }
  }
};

template <>
struct PointerToString<void*> {
  static std::string Apply(const void* ptr) {
    return PointerToString<const void*>::Apply(ptr);
  }
};

template <class T>
concept TypeWithStronglyDefinedToString =
    TypeWith_ToString<T> || TypeWith_to_string<T> || yb_tostring::TypeWithFree_to_string<T> ||
    TypeWith_ShortDebugString<T> || OptionalType<T> ||
    std::is_integral_v<T> || std::is_floating_point_v<T>;

template <class T>
concept TypeForToStringAsPointer = IsPointerLike<T>::value && !TypeWithStronglyDefinedToString<T>;

template <class T>
concept TypeForToStringAsCollection = IsCollection<T>::value && !TypeWithStronglyDefinedToString<T>;

template <TypeForToStringAsPointer T>
decltype(auto) ToString(const T& value) {
  return PointerToString<std::remove_cv_t<T>>::Apply(value);
}

template <class Value>
decltype(auto) ToString(std::reference_wrapper<Value> value) {
  return ToString(value.get());
}

inline std::string_view ToString(std::string_view str) { return str; }
inline const std::string& ToString(const std::string& str) { return str; }
inline std::string ToString(const char* str) { return str; }

template <class First, class Second>
std::string ToString(const std::pair<First, Second>& pair);

template <class Collection, class Transform>
std::string CollectionToString(const Collection& collection, const Transform& transform);

struct Identity {
  template <class T>
  const T& operator()(const T& t) const {
    return t;
  }
};

template <class Collection>
decltype(auto) CollectionToString(const Collection& collection) {
  return CollectionToString(collection, Identity());
}

std::string CStringArrayToString(char** elements, size_t length);

template <yb_tostring::TypeWithFree_to_string T>
decltype(auto) ToString(const T& value) {
  return to_string(value);
}

template <TypeForToStringAsCollection T>
decltype(auto) ToString(const T& value) {
  return CollectionToString(value);
}

template <TypeForToStringAsCollection T, class Transform>
decltype(auto) ToString(const T& value, const Transform& transform) {
  return CollectionToString(value, transform);
}

template <yb_tostring::TypeSupportsOutputToStream T>
requires(!(TypeWithStronglyDefinedToString<T> ||
           TypeForToStringAsCollection<T> ||
           TypeForToStringAsPointer<T>))
decltype(auto) ToString(const T& value) {
  std::ostringstream out;
  out << value;
  return out.str();
}

// Definition of functions that use ToString chaining should be declared after all declarations.
template <class Pointer>
template <class P>
std::string PointerToString<Pointer>::Apply(const P& ptr) {
  if (ptr) {
    char buffer[kFastToBufferSize]; // kFastToBufferSize has enough extra capacity for 0x and ->
    buffer[0] = '0';
    buffer[1] = 'x';
    FastHex64ToBuffer(reinterpret_cast<size_t>(&*ptr), buffer + 2);
    char* end = buffer + strlen(buffer);
    memcpy(end, " -> ", 5);
    return buffer + ToString(*ptr);
  } else {
    return "<NULL>";
  }
}

template <class First, class Second>
std::string ToString(const std::pair<First, Second>& pair) {
  return "{" + ToString(pair.first) + ", " + ToString(pair.second) + "}";
}

template <class Tuple, size_t index, bool exist>
struct TupleToString {
  static void Apply(const Tuple& tuple, std::string* out) {
    if (index) {
      *out += ", ";
    }
    *out += ToString(std::get<index>(tuple));
    TupleToString<Tuple, index + 1, (index + 1 < std::tuple_size_v<Tuple>)>::Apply(tuple, out);
  }
};

template <class Tuple, size_t index>
struct TupleToString<Tuple, index, false> {
  static void Apply(const Tuple& tuple, std::string* out) {}
};

template <class... Args>
std::string ToString(const std::tuple<Args...>& tuple) {
  using Tuple = std::tuple<Args...>;
  std::string result = "{";
  TupleToString<Tuple, 0, (0 < std::tuple_size_v<Tuple>)>::Apply(tuple, &result);
  result += "}";
  return result;
}

std::string MillisecondsToString(int64_t milliseconds);

template <class Rep, class Period>
decltype(auto) ToString(const std::chrono::duration<Rep, Period>& duration) {
  return MillisecondsToString(
      std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
}

std::string ToString(const std::chrono::steady_clock::time_point& time_point);
std::string ToString(const std::chrono::system_clock::time_point& time_point);

template <OptionalType T>
std::string ToString(const T& t) {
  if (!t) {
    return "<nullopt>";
  }
  return ToString(*t);
}

template <class Collection, class Transform>
std::string CollectionToString(const Collection& collection, const Transform& transform) {
  std::string result = "[";
  auto first = true;

// Range loop analysis flags copying of objects in a range loop by suggesting the use of
// references. It however prevents the use of references for trivial entities like 'bool'. Given
// that this function is templatized, we have both the cases happening in the following loop.
// Ignore the range-loop-analysis in this part of the code.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wrange-loop-analysis"
#endif
  for (const auto& item : collection) {
    if (first) {
      first = false;
    } else {
      result += ", ";
    }
    result += ToString(transform(item));
  }
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  result += "]";
  return result;
}

template <class... T>
decltype(auto) AsString(const T&... t) {
  return ToString(t...);
}

} // namespace yb

#if BOOST_PP_VARIADICS

#define YB_FIELD_TO_STRING_NAME(elem) \
    BOOST_PP_IF(BOOST_PP_IS_BEGIN_PARENS(elem), BOOST_PP_TUPLE_ELEM(2, 0, elem), elem)

#define YB_FIELD_TO_STRING_VALUE(elem, data)     \
    BOOST_PP_IF(BOOST_PP_IS_BEGIN_PARENS(elem),  \
                (BOOST_PP_TUPLE_ELEM(2, 1, elem)), \
                ::yb::AsString(BOOST_PP_CAT(elem, BOOST_PP_APPLY(data))))

#define YB_FIELD_TO_STRING(r, data, elem) \
    " " BOOST_PP_STRINGIZE(YB_FIELD_TO_STRING_NAME(elem)) ": " + \
    YB_FIELD_TO_STRING_VALUE(elem, data) +
#define YB_FIELDS_TO_STRING(data, ...) \
    BOOST_PP_SEQ_FOR_EACH(YB_FIELD_TO_STRING, data(), BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))

// This can be used to simplify ToString function implementations in structs where field names do
// not end with an underscore. Suppose we have a struct with fields a and b. If we implement
// ToString as
//
// std::string ToString() const {
//   return YB_STRUCT_TO_STRING(a, b);
// }
//
// we will get ToString return values of the form "{ a: value_for_a b: value_for_b }".
#define YB_STRUCT_TO_STRING(...) \
    "{" YB_FIELDS_TO_STRING(BOOST_PP_NIL, __VA_ARGS__) " }"

// This can be used to simplify ToString function implementations in classes where field names end
// with an underscore. Suppose we have a class with fields a_ and b_. If we implement ToString as
//
// std::string ToString() const {
//   return YB_CLASS_TO_STRING(a, b);
// }
//
// we will get ToString return values of the form "{ a: value_for_a b: value_for_b }".
#define YB_CLASS_TO_STRING(...) \
    "{" YB_FIELDS_TO_STRING((BOOST_PP_IDENTITY(_)), __VA_ARGS__) " }"

#else
#error "Compiler not supported -- BOOST_PP_VARIADICS is not set. See https://bit.ly/2ZF7rTu."
#endif
