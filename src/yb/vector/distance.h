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
#include <type_traits>

#include "yb/util/enums.h"
#include "yb/util/tostring.h"

#include "yb/common/vector_types.h"
#include "yb/vector/graph_repr_defs.h"
#include "yb/vector/coordinate_types.h"

namespace yb::vectorindex {

YB_DEFINE_ENUM(
    DistanceKind,

    // Squared Euclidean (L2) distance -- sum of squares between coordinate differences.
    (kL2Squared)

    // Inner product: 1 - (x dot product y).
    (kInnerProduct)

    // Cosine (Angular) distance. Similar to Inner Product, but it has to normalize the vectors
    // first, so it is more expensive to compute.
    (kCosine));

template<typename T>
concept ValidDistanceResultType =
    std::is_same_v<T, float> ||
    std::is_same_v<T, double> ||
    std::is_same_v<T, uint32_t> ||
    std::is_same_v<T, int32_t>;

template<IndexableVectorType Vector, DistanceKind distance_kind>
struct DistanceTraits {
  using Scalar = typename Vector::value_type;

  // The "default" distance result is float for 32-bit or smaller scalars, and double for 64-bit
  // scalars.
  using Result = typename std::conditional<(sizeof(Scalar) <= 4), float, double>::type;

  static_assert(ValidDistanceResultType<Result>);
};

#define YB_OVERRIDE_DISTANCE_RESULT_TYPE(scalar, distance_kind, distance_result_type) \
    template<> \
    struct DistanceTraits<std::vector<scalar>, distance_kind> { \
      using Result = distance_result_type; \
      static_assert(ValidDistanceResultType<Result>); \
    };

// L2 square distance for signed or unsigned byte vectors could be computed as uint32_t for vectors
// with 33025 or fewer dimensions: (2**31-1)/(255*255) > 33025. If we were to use float here, we
// might run into precision issues.
//
// TODO: temporarily using int32_t for consistency with hnswlib.
YB_OVERRIDE_DISTANCE_RESULT_TYPE(int8_t, DistanceKind::kL2Squared, int32_t)
YB_OVERRIDE_DISTANCE_RESULT_TYPE(uint8_t, DistanceKind::kL2Squared, int32_t)

// int32_t result type can support up to (2**32-1)/(255*255) > 66051 dimensions when calculating
// inner product of uint8_t vectors. We need a signed result here because we have to negate the
// result of the inner product of two vectors.
YB_OVERRIDE_DISTANCE_RESULT_TYPE(uint8_t, DistanceKind::kInnerProduct, int32_t);

// int32_t result type can support up to (2**31)/(128*128) = 131072 dimensions when calculating
// inner product of int8_t vectors.
YB_OVERRIDE_DISTANCE_RESULT_TYPE(int8_t, DistanceKind::kInnerProduct, int32_t);

namespace distance {

// Reference implementations of distance functions. These are NOT optimized for performance.

template<IndexableVectorType Vector>
inline auto DistanceL2Squared(const Vector& a, const Vector& b) {
  using DistanceResult = typename DistanceTraits<Vector, DistanceKind::kL2Squared>::Result;
  CHECK_EQ(a.size(), b.size());
  DistanceResult sum = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    // Be paranoid about subtraction underflow because DistanceResult might be unsigned.
    DistanceResult ai = a[i];
    DistanceResult bi = b[i];
    DistanceResult diff = ai > bi ? ai - bi : bi - ai;
    sum += diff * diff;
  }
  return sum;
}

template<IndexableVectorType Vector>
inline auto DistanceInnerProduct(
    const Vector& a, const Vector& b) {
  using DistanceResult = typename DistanceTraits<Vector, DistanceKind::kInnerProduct>::Result;
  CHECK_EQ(a.size(), b.size());
  DistanceResult ab = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    ab += static_cast<DistanceResult>(a[i]) * static_cast<DistanceResult>(b[i]);
  }
  return 1 - ab;
}

template<IndexableVectorType Vector>
inline auto DistanceCosine(
    const Vector& a, const Vector& b) {
  // Adapted from metric_cos_gt in index_plugins.hpp (usearch).
  using DistanceResult = typename DistanceTraits<Vector, DistanceKind::kCosine>::Result;
  CHECK_EQ(a.size(), b.size());
  DistanceResult ab = 0;
  DistanceResult a2 = 0;
  DistanceResult b2 = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    DistanceResult ai = a[i];
    DistanceResult bi = b[i];
    ab += ai * bi;
    a2 += ai * ai;
    b2 += bi * bi;
  }

  DistanceResult result_if_zero[2][2];
  result_if_zero[0][0] = 1 - ab / (std::sqrt(a2) * std::sqrt(b2));
  result_if_zero[0][1] = result_if_zero[1][0] = 1;
  result_if_zero[1][1] = 0;

  return result_if_zero[a2 == 0][b2 == 0];
}

}  // namespace distance

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
using DistanceFunction = std::function<DistanceResult(const Vector&, const Vector&)>;

// A variant of a distance function that knows how to resolve a vertex id to a vector, and then
// compute the distance.
template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
using VertexIdToVectorDistanceFunction =
    std::function<DistanceResult(VertexId vertex_id, const Vector&)>;

template<ValidDistanceResultType DistanceResult>
struct VertexWithDistance {
  VertexId vertex_id = kInvalidVertexId;
  DistanceResult distance{};

  // Constructor with the wrong order. Only delete it if DistanceResult is not uint64_t.
  template <typename T = DistanceResult,
            typename std::enable_if<!std::is_same<T, VertexId>::value, int>::type = 0>
  VertexWithDistance(DistanceResult, VertexId) = delete;

  VertexWithDistance() = default;

  // Constructor with the correct order
  VertexWithDistance(VertexId vertex_id_, DistanceResult distance_)
      : vertex_id(vertex_id_), distance(distance_) {}

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(vertex_id, distance);
  }

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

template<ValidDistanceResultType DistanceResult>
using VerticesWithDistances = std::vector<VertexWithDistance<DistanceResult>>;

template<ValidDistanceResultType DistanceResult>
std::vector<VertexId> VertexIdsOnly(
    const VerticesWithDistances<DistanceResult>& vertices_with_distances) {
  std::vector<VertexId> result;
  result.reserve(vertices_with_distances.size());
  for (const auto& v_dist : vertices_with_distances) {
    result.push_back(v_dist.vertex_id);
  }
  return result;
}

template <IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
DistanceFunction<Vector, DistanceResult> GetDistanceFunction(DistanceKind distance_kind) {
  switch (distance_kind) {
    case DistanceKind::kInnerProduct:
      return distance::DistanceInnerProduct<Vector>;
    case DistanceKind::kL2Squared:
      return distance::DistanceL2Squared<Vector>;
    case DistanceKind::kCosine:
      return distance::DistanceCosine<Vector>;
  }
  FATAL_INVALID_ENUM_VALUE(DistanceKind, distance_kind);
}

}  // namespace yb::vectorindex
