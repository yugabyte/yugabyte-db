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

#pragma once

#include <cmath>

#include "yb/util/enums.h"

#include "yb/common/vector_types.h"
#include "yb/vector/graph_repr_defs.h"
#include "yb/vector/coordinate_types.h"

namespace yb::vectorindex {

namespace distance {

template<IndexableVectorType Vector1Type, IndexableVectorType Vector2Type>
inline float DistanceL2Squared(const Vector1Type& a, const Vector2Type& b) {
  float sum = 0;
  CHECK_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    float diff = a[i] - b[i];
    sum += diff * diff;
  }
  return sum;
}

template<IndexableVectorType Vector>
inline float DistanceCosine(const Vector& a, const Vector& b) {
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

YB_DEFINE_ENUM(
    VectorDistanceType,
    (kL2Squared)
    (kCosine));

template<IndexableVectorType Vector>
using DistanceFunction = std::function<float(const Vector&, const Vector&)>;

// A variant of a distance function that knows how to resolve a vertex id to a vector, and then
// compute the distance.
template<IndexableVectorType Vector>
using VertexIdToVectorDistanceFunction =
    std::function<float(VertexId vertex_id, const Vector&)>;

template<IndexableVectorType Vector>
DistanceFunction<Vector> GetDistanceImpl(VectorDistanceType distance_type) {
  switch (distance_type) {
    case VectorDistanceType::kL2Squared:
      return distance::DistanceL2Squared<Vector, Vector>;
    case VectorDistanceType::kCosine:
      return distance::DistanceCosine<Vector>;
  }
  FATAL_INVALID_ENUM_VALUE(VectorDistanceType, distance_type);
}

struct VertexWithDistance {
  VertexId vertex_id = 0;
  float distance = 0.0f;

  // Deleted constructor to prevent wrong initialization order
  VertexWithDistance(float, VertexId) = delete;

  VertexWithDistance() = default;

  // Constructor with the correct order
  VertexWithDistance(VertexId vertex_id_, float distance_)
      : vertex_id(vertex_id_), distance(distance_) {}

  std::string ToString() const;

  bool operator ==(const VertexWithDistance& other) const {
    return vertex_id == other.vertex_id && distance == other.distance;
  }

  bool operator !=(const VertexWithDistance& other) const {
    return !(*this == other);
  }

  // Sort in lexicographical order of (distance, vertex_id).
  bool operator <(const VertexWithDistance& other) const {
    return distance < other.distance ||
           (distance == other.distance && vertex_id < other.vertex_id);
  }

  bool operator>(const VertexWithDistance& other) const {
    return distance > other.distance ||
           (distance == other.distance && vertex_id > other.vertex_id);
  }
};

using VerticesWithDistances = std::vector<VertexWithDistance>;

std::vector<VertexId> VertexIdsOnly(const VerticesWithDistances& vertices_with_distances);

}  // namespace yb::vectorindex
