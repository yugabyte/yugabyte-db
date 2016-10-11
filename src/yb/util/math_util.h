// Copyright (c) YugaByte, Inc.

#ifndef YB_UTIL_MATH_UTIL_H_
#define YB_UTIL_MATH_UTIL_H_

#include <vector>

namespace yb {

  // Returns the standard deviation of a bunch of numbers.
  double standard_deviation(std::vector<double> data);

}  // namespace yb

#endif // YB_UTIL_MATH_UTIL_H_
