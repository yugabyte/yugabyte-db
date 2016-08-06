// Copyright (c) YugaByte, Inc.

#ifndef YB_UTIL_ENUMS_H
#define YB_UTIL_ENUMS_H

namespace yb {
namespace util {

template <typename E>
constexpr typename std::underlying_type<E>::type to_underlying(E e) {
  return static_cast<typename std::underlying_type<E>::type>(e);
}

}
}
#endif  // YB_UTIL_ENUMS_H
