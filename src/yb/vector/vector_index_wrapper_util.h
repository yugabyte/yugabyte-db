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

#include "yb/vector/vector_index_if.h"

#pragma once

namespace yb::vectorindex {

// A base class for vector index implementations implementing the pointer-to-implementation idiom.
template <typename Impl, typename Vector, typename DistanceResult>
class VectorIndexBase : public VectorIndexIf<Vector, DistanceResult> {
 public:
  explicit VectorIndexBase(std::unique_ptr<Impl> impl)
      : impl_(std::move(impl)) {}

  ~VectorIndexBase() override = default;

  // Implementations for the VectorIndexReaderIf interface
  std::vector<VertexWithDistance<DistanceResult>> Search(
      const Vector& query_vector, size_t max_num_results) const override {
    return impl_->Search(query_vector, max_num_results);
  }

  // Implementations for the VectorIndexWriterIf interface
  Status Reserve(size_t num_vectors) override {
    return impl_->Reserve(num_vectors);
  }

  Status Insert(VertexId vertex_id, const Vector& vector) override {
    return impl_->Insert(vertex_id, vector);
  }

  Result<Vector> GetVector(VertexId vertex_id) const override {
    return impl_->GetVector(vertex_id);
  }

 protected:
  std::unique_ptr<Impl> impl_;
};

// An adapter that allows us to view an index reader with one vector type as an index reader with a
// different vector type. Casts the queries to the vector type supported by the index, and then
// casts the distance type in the results to the distance type expected by the caller.
template<
  IndexableVectorType SourceVector,
  ValidDistanceResultType SourceDistanceResult,
  IndexableVectorType DestinationVector,
  ValidDistanceResultType DestinationDistanceResult
>
class VectorIndexReaderAdapter
    : public VectorIndexReaderIf<DestinationVector, DestinationDistanceResult> {
 public:
  // Constructor takes the underlying vector index reader
  explicit VectorIndexReaderAdapter(
      const VectorIndexReaderIf<SourceVector, SourceDistanceResult>& source_reader)
      : source_reader_(source_reader) {}

  // Implementation of the Search function
  std::vector<VertexWithDistance<DestinationDistanceResult>> Search(
      const DestinationVector& query_vector, size_t max_num_results) const override {
    // Cast the query_vector to the SourceVector type
    SourceVector cast_query_vector = vector_cast<SourceVector>(query_vector);

    // Perform the search using the underlying source_reader
    auto source_results = source_reader_.Search(cast_query_vector, max_num_results);

    // Prepare to convert results to the DestinationDistanceResult type
    std::vector<VertexWithDistance<DestinationDistanceResult>> destination_results;
    destination_results.reserve(source_results.size());

    for (const auto& source_result : source_results) {
      DestinationDistanceResult cast_distance = static_cast<DestinationDistanceResult>(
          source_result.distance);
      destination_results.emplace_back(source_result.vertex_id, cast_distance);
    }

    return destination_results;
  }

 private:
  const VectorIndexReaderIf<SourceVector, SourceDistanceResult>& source_reader_;
};


}  // namespace yb::vectorindex
