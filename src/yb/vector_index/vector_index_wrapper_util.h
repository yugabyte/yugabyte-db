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

#include "yb/vector_index/vector_index_if.h"

#pragma once

namespace yb::vector_index {

// An adapter that allows us to view an index reader with one vector type as an index reader with a
// different vector type. Casts the queries to the vector type supported by the index, and then
// casts the distance type in the results to the distance type expected by the caller.
//
// Terminology:
//   - SourceVector: The vector type supported by the underlying index reader.
//   - SourceDistanceResult: The distance type supported by the underlying index reader.
//   - DestinationVector: The vector type expected by the caller.
//   - DestinationDistanceResult: The distance type expected by the caller.
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

  DestinationDistanceResult Distance(
      const DestinationVector& lhs, const DestinationVector& rhs) const override {
    return static_cast<DestinationDistanceResult>(source_reader_.Distance(
        vector_cast<SourceVector>(lhs),
        vector_cast<SourceVector>(rhs)));
  }

  std::string IndexStatsStr() const override {
    return source_reader_.IndexStatsStr();
  }

 private:
  const VectorIndexReaderIf<SourceVector, SourceDistanceResult>& source_reader_;
};


}  // namespace yb::vector_index
