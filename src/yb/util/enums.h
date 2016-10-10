// Copyright (c) YugaByte, Inc.

#ifndef YB_UTIL_ENUMS_H_
#define YB_UTIL_ENUMS_H_

namespace yb {
namespace util {

// Convert a strongly typed enum to its underlying type.
// Based on an answer to this StackOverflow question: https://goo.gl/zv2Wg3
template <typename E>
constexpr typename std::underlying_type<E>::type to_underlying(E e) {
  return static_cast<typename std::underlying_type<E>::type>(e);
}

}  // namespace util
}  // namespace yb

#endif  // YB_UTIL_ENUMS_H_
