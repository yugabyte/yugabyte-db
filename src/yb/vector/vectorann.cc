//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include <cmath>
#include <queue>
#include <stack>

#include "yb/util/memory/arena.h"

#include "yb/vector/vectorann.h"

namespace yb::vectorindex {

template <IndexableVectorType Vector>
Result<Vector> VectorANN<Vector>::GetVectorFromYSQLWire(
    const YSQLVector& ysql_vector, size_t total_len) {
  SCHECK_GE(
      total_len, sizeof(YSQLVector), InternalError,
      Format(
          "Malformed vector. Received a vector of size $0 bytes when at least $1 bytes were "
          "expected.",
          total_len, sizeof(YSQLVector)));

  auto dim = ysql_vector.dim;
  auto expected_bytes = dim * sizeof(float) + sizeof(YSQLVector);
  SCHECK_EQ(
      total_len, expected_bytes, InternalError,
      Format(
          "Malformed vector. Received a vector of size $0 bytes when $1 bytes were "
          "expected.",
          total_len, expected_bytes));

  Vector result;
  for (auto i = 0; i < dim; i++) {
    result.push_back(ysql_vector.elems[i]);
  }

  return result;
}

namespace {
// Dummy implementation of the VectorANN interface. This is used for testing.
// It stores all the given vectors in memory and does a linear scan to find the top k with
// a priority queue and a stack.
// This implementation has no MVCC capabilities.
template <IndexableVectorType Vector>
class DummyANN final : public VectorANN<Vector> {
 public:
  explicit DummyANN(uint32_t dims) : VectorANN<Vector>(dims), dims_(dims) {}

  ~DummyANN() override {}

  void Add(const Vector& vector, Slice val) override {
    vertex_ids_.push_back(vertex_ids_.size());
    vectors_.push_back(vector);

    auto my_val = arena_.DupSlice(val);
    values_.push_back(my_val);
  }

  // Gets the top k vectors from this ANN. The vectors are first returned in
  // ascending order of their distance to the query vector and then in
  // ascending order of their YBCTID.
  std::vector<DocKeyWithDistance> GetTopKVectors(
      Vector query_vec, size_t k, double lb_distance, Slice lb_key,
      bool is_lb_inclusive) const override {
    using DistanceResult = double;
    auto lower_bound = DocKeyWithDistance(lb_key, lb_distance);

    auto dist_fn = GetDistanceFunction<Vector, DistanceResult>(DistanceKind::kL2Squared);
    auto modified_dist = [this, &lower_bound, &is_lb_inclusive, dist_fn](
                             VertexId vertex_id, const Vector& v) -> float {
      auto& value = values_[vertex_id];
      auto dist = dist_fn(vectors_[vertex_id], v);
      auto current_val = DocKeyWithDistance(value, dist);
      auto cmp = current_val.Compare(lower_bound);
      if (cmp < 0 || (!is_lb_inclusive && cmp == 0)) {
        return std::numeric_limits<float>::infinity();
      }
      return dist;
    };

    auto topk = BruteForcePreciseNearestNeighbors<Vector, DistanceResult>(
        query_vec, vertex_ids_, modified_dist, k);

    std::vector<DocKeyWithDistance> out;
    for (auto vertex_id : topk) {
      if (vertex_id.distance == std::numeric_limits<double>::infinity()) {
        continue;
      }

      out.push_back(DocKeyWithDistance(values_[vertex_id.vertex_id], vertex_id.distance));
    }
    return out;
  }

 private:
  const uint32_t dims_;
  std::vector<Vector> vectors_;
  std::vector<VertexId> vertex_ids_;
  std::vector<Slice> values_;
  ThreadSafeArena arena_;
};
}  // namespace

template <IndexableVectorType Vector>
std::unique_ptr<VectorANN<Vector>> DummyANNFactory<Vector>::Create(uint32_t dims) {
  return std::make_unique<DummyANN<Vector>>(dims);
}

template class VectorANN<FloatVector>;
template class DummyANNFactory<FloatVector>;
}  // namespace yb::vectorindex
