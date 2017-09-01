// Copyright (c) YugaByte, Inc.

#ifndef YB_UTIL_TSAN_UTIL_H
#define YB_UTIL_TSAN_UTIL_H

namespace yb {

template <class T>
constexpr T NonTsanVsTsan(T value_not_in_tsan, T value_in_tsan) {
#if THREAD_SANITIZER
  return value_in_tsan;
#else
  return value_not_in_tsan;
#endif
}

}  // namespace yb

#endif  // YB_UTIL_TSAN_UTIL_H
