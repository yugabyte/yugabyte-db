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
