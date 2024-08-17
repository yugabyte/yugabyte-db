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

#include <cstdint>
#include <queue>
#include <variant>

#include "yb/util/random_util.h"

#include "yb/vector/distance.h"
#include "yb/vector/graph_repr_defs.h"
#include "yb/common/vector_types.h"

namespace yb::vectorindex {

// Generates a random value as floor(-log(uniform_random) * ml), but clipped at max_level.
// Ignoring the clipping, this corresponds to be a geometric distribution with probability of
// success p = 1 - exp(-1/ml). The average level selected by this function will be 1/p.
//
// Example values:
// 1. If ml = 1/log(2)  (~1.4427), then p = 0.5, and the expected level is 2.
// 2. If ml = 1/log(3)  (~0.9102), then p = 1 - 1/3  (~0.6667), and the expected level is 1.5.
// 3. If ml = 1/log(4)  (~0.7213), then p = 1 - 1/4  (~0.75), and the expected level is ~1.333.
VectorIndexLevel SelectRandomLevel(double ml, VectorIndexLevel max_level);

struct CompareDistanceForMinHeap {
  bool operator()(const VertexWithDistance& a, const VertexWithDistance& b) {
    return a.distance > b.distance;
  }
};

struct CompareDistanceForMaxHeap {
  bool operator()(const VertexWithDistance& a, const VertexWithDistance& b) {
    return a.distance < b.distance;
  }
};

using MinDistanceQueue =
    std::priority_queue<VertexWithDistance,
                        std::vector<VertexWithDistance>,
                        CompareDistanceForMinHeap>;

using MaxDistanceQueue =
    std::priority_queue<VertexWithDistance,
                        std::vector<VertexWithDistance>,
                        CompareDistanceForMaxHeap>;

using ConstFloatVectorRef = std::reference_wrapper<const FloatVector>;
using VertexIdOrVectorRef = std::variant<VertexId, ConstFloatVectorRef>;

// Drain a max-queue of (vertex, distance) pairs and return a list of VertexWithDistance instances
// ordered by increasing distance.
std::vector<VertexWithDistance> DrainMaxQueueToIncreasingDistanceList(MaxDistanceQueue& queue);

}  // namespace yb::vectorindex
