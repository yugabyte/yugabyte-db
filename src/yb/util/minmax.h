//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_UTIL_MINMAX_H
#define YB_UTIL_MINMAX_H

template<class T>
struct MinMaxTracker {
  void operator()(T t) {
    if (!inited) {
      min = std::move(t);
      max = min;
      inited = true;
      return;
    }
    if (t < min) {
      min = std::move(t);
    } else if (max < t) {
      max = std::move(t);
    }
  }

  bool inited = false;
  T min = T();
  T max = T();
};

#endif // YB_UTIL_MINMAX_H
