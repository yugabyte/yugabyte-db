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

}  // namespace distance

std::vector<VertexId> VertexIdsOnly(const VerticesWithDistances& vertices_with_distances) {
  std::vector<VertexId> result;
  result.reserve(vertices_with_distances.size());
  for (const auto& v_dist : vertices_with_distances) {
    result.push_back(v_dist.vertex_id);
  }
  return result;

}
}  // namespace yb::vectorindex
