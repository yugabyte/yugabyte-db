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

#include "yb/vector_index/vectorann.h"

namespace yb::vector_index {

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

template <IndexableVectorType Vector>
Result<Vector> VectorANN<Vector>::GetVectorFromYSQLWire(Slice binary_vector) {
  const auto& ysql_vector = *pointer_cast<const vector_index::YSQLVector*>(binary_vector.data());
  return GetVectorFromYSQLWire(ysql_vector, binary_vector.size());
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

  bool Add(VectorId vertex_id, Vector&& vector, Slice val) override {
    if (vectors_.count(vertex_id)) {
      return false;
    }

    auto val_dup = arena_.DupSlice(val);
    vectors_.emplace(vertex_id, std::make_tuple(std::move(vector), val_dup));
    return true;
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
        VectorId vertex_id, const Vector& other_vector) -> float {
      auto it = vectors_.find(vertex_id);
      if (it == vectors_.end()) {
        DCHECK(false) << "It is expected the vector " << vertex_id.ToString() << " exists";
        return std::numeric_limits<float>::infinity();
      }
      auto dist = dist_fn(std::get<Vector>(it->second), other_vector);
      auto current_val = DocKeyWithDistance(std::get<Slice>(it->second), dist);
      auto cmp = current_val.Compare(lower_bound);
      if (cmp < 0 || (!is_lb_inclusive && cmp == 0)) {
        return std::numeric_limits<float>::infinity();
      }
      return dist;
    };

    // TODO(vector-index): maybe replace vector_ with std::ranges::views::keys or boost multi_index
    // or implement an iterator object over map to iterate keys only.
    std::vector<VectorId> vertex_ids;
    vertex_ids.reserve(vectors_.size());
    std::transform(vectors_.begin(), vectors_.end(), std::back_inserter(vertex_ids),
                   [](const auto& item){ return item.first; });
    auto topk = BruteForcePreciseNearestNeighbors<Vector, DistanceResult>(
        query_vec, vertex_ids, modified_dist, k);

    std::vector<DocKeyWithDistance> out;
    for (auto vd : topk) {
      if (vd.distance == std::numeric_limits<double>::infinity()) {
        continue;
      }

      auto it = vectors_.find(vd.vector_id);
      CHECK(it != vectors_.end()); // Sanity check, it is expected the vector exists.

      out.push_back(DocKeyWithDistance(std::get<Slice>(it->second), vd.distance));
    }
    return out;
  }

 private:
  const uint32_t dims_;
  std::unordered_map<VectorId, std::tuple<Vector, Slice>> vectors_;
  ThreadSafeArena arena_;
};

}  // namespace

template <IndexableVectorType Vector>
std::unique_ptr<VectorANN<Vector>> DummyANNFactory<Vector>::Create(uint32_t dims) {
  return std::make_unique<DummyANN<Vector>>(dims);
}

template class VectorANN<FloatVector>;
template class DummyANNFactory<FloatVector>;

}  // namespace yb::vector_index
