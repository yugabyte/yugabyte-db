// Copyright (c) YugabyteDB, Inc.
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

#include "yb/vector/distance.h"

#include <cmath>

#include "yb/util/logging.h"

namespace yb::vectorindex {

namespace distance {

float DistanceL2Squared(const FloatVector& a, const FloatVector& b) {
  float sum = 0;
  CHECK_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    float diff = a[i] - b[i];
    sum += diff * diff;
  }
  return sum;
}

float DistanceCosine(const FloatVector& a, const FloatVector& b) {
  // Adapted from metric_cos_gt in index_plugins.hpp (usearch).
  CHECK_EQ(a.size(), b.size());
  float ab = 0, a2 = 0, b2 = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    float ai = a[i];
    float bi = b[i];
    ab += ai * bi;
    a2 += ai * ai;
    b2 += bi * bi;
  }

  float result_if_zero[2][2];
  result_if_zero[0][0] = 1 - ab / (std::sqrt(a2) * std::sqrt(b2));
  result_if_zero[0][1] = result_if_zero[1][0] = 1;
  result_if_zero[1][1] = 0;

  return result_if_zero[a2 == 0][b2 == 0];
}

}  // namespace distance

DistanceFunction GetDistanceImpl(VectorDistanceType distance_type) {
  switch (distance_type) {
    case VectorDistanceType::kL2Squared:
      return distance::DistanceL2Squared;
    case VectorDistanceType::kCosine:
      return distance::DistanceCosine;
  }
  FATAL_INVALID_ENUM_VALUE(VectorDistanceType, distance_type);
}

}  // namespace yb::vectorindex
