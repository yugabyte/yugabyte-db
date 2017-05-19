//
// Copyright (c) YugaByte, Inc.
//
#ifndef YB_UTIL_SIZE_LITERALS_H
#define YB_UTIL_SIZE_LITERALS_H

namespace yb {

inline constexpr size_t operator "" _KB(unsigned long long kilobytes) { // NOLINT
  return kilobytes * 1024;
}

inline constexpr size_t operator "" _MB(unsigned long long megabytes) { // NOLINT
  return megabytes * 1024 * 1024;
}

inline constexpr size_t operator "" _GB(unsigned long long gigabytes) { // NOLINT
  return gigabytes * 1024 * 1024 * 1024;
}

} // namespace yb

#endif // YB_UTIL_SIZE_LITERALS_H
