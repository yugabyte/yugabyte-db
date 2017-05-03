// Copyright (c) YugaByte, Inc.

#ifndef YB_UTIL_COMPARE_UTIL_H
#define YB_UTIL_COMPARE_UTIL_H

namespace yb {
namespace util {

template<typename T>
int CompareUsingLessThan(const T& a, const T& b) {
  if (a < b) return -1;
  if (b < a) return 1;
  return 0;
}

template<typename T>
int CompareVectors(const std::vector<T>& a, const std::vector<T>& b) {
  auto a_iter = a.begin();
  auto b_iter = b.begin();
  while (a_iter != a.end() && b_iter != b.end()) {
    int result = a_iter->CompareTo(*b_iter);
    if (result != 0) {
      return result;
    }
    ++a_iter;
    ++b_iter;
  }
  if (a_iter == a.end()) {
    return b_iter == b.end() ? 0 : -1;
  }
  DCHECK(b_iter == b.end());  // This follows from the while loop condition.
  return 1;
}

// http://stackoverflow.com/questions/1903954/is-there-a-standard-sign-function-signum-sgn-in-c-c
template <typename T>
inline int sgn(T val) {
  return (T(0) < val) - (val < T(0));
}

}  // namespace util
}  // namespace yb

#endif  // YB_UTIL_COMPARE_UTIL_H
