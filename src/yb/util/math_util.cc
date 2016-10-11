// Copyright (c) YugaByte, Inc.

#include "yb/util/math_util.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

using std::vector;

namespace yb {

  double standard_deviation(vector<double> data) {
    if (data.empty()) {
      return 0.0;
    }
    double mean = std::accumulate(data.begin(), data.end(), 0.0) / data.size();
    vector<double> deltas(data.size());
    std::transform(data.begin(), data.end(), deltas.begin(), [mean](double x) {
      return x - mean;
    });
    double inner_product = std::inner_product(deltas.begin(), deltas.end(), deltas.begin(), 0.0);
    double stdev = std::sqrt(inner_product / data.size());
    return stdev;
  }
}  // namespace yb
