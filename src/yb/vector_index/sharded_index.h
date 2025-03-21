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

#include "yb/vector_index/coordinate_types.h"
#include "yb/vector_index/distance.h"
#include "yb/vector_index/vector_index_if.h"

namespace yb::vector_index {

// Allows creating multiple instances of the vector index so we can saturate the capacity of the
// test system.
template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class ShardedVectorIndex : public VectorIndexIf<Vector, DistanceResult> {
 public:
  using Base = VectorIndexIf<Vector, DistanceResult>;

  ShardedVectorIndex(const VectorIndexFactory<Vector, DistanceResult>& factory,
                     size_t num_shards)
      : indexes_(num_shards), round_robin_counter_(0) {
    for (auto& index : indexes_) {
      index = factory();
    }
  }

  // Reserve capacity across all shards (each shard gets an equal portion, rounded up).
  Status Reserve(
      size_t num_vectors, size_t max_concurrent_inserts, size_t max_concurrent_reads) override {
    size_t capacity_per_shard = (num_vectors + indexes_.size() - 1) / indexes_.size();  // Round up
    for (auto& index : indexes_) {
      RETURN_NOT_OK(index->Reserve(
          capacity_per_shard, max_concurrent_inserts, max_concurrent_reads));
    }
    return Status::OK();
  }

  size_t Size() const override {
    return std::accumulate(
        indexes_.begin(), indexes_.end(), static_cast<size_t>(0),
        [](size_t sum, const auto& index) { return sum + index->Size(); });
  }

  size_t Capacity() const override {
    return std::accumulate(
        indexes_.begin(), indexes_.end(), static_cast<size_t>(0),
        [](size_t sum, const auto& index) { return sum + index->Capacity(); });
  }

  // Insert a vector into the current shard using round-robin.
  Status Insert(VectorId vector_id, const Vector& vector) override {
    size_t current_index = round_robin_counter_.fetch_add(1) % indexes_.size();
    return indexes_[current_index]->Insert(vector_id, vector);
  }

  // Retrieve a vector from any shard.
  Result<Vector> GetVector(VectorId vector_id) const override {
    for (const auto& index : indexes_) {
      auto v = VERIFY_RESULT(index->GetVector(vector_id));
      if (!v.empty()) {
        return v;
      }
    }
    return Vector();  // Return an empty vector if not found.
  }

  // TODO(vector_index): define begin and end methods to iterate over all shareded indexes.
  std::unique_ptr<AbstractIterator<std::pair<VectorId, Vector>>> BeginImpl() const override {
    CHECK(!indexes_.empty());
    return indexes_[0]->BeginImpl();
  }

  std::unique_ptr<AbstractIterator<std::pair<VectorId, Vector>>> EndImpl() const override {
    CHECK(!indexes_.empty());
    return indexes_[0]->EndImpl();
  }

  // Search for the closest vectors across all shards.
  Result<typename Base::SearchResult> Search(
      const Vector& query_vector, const SearchOptions& options) const override {
    std::vector<VectorWithDistance<DistanceResult>> all_results;
    for (const auto& index : indexes_) {
      auto results = VERIFY_RESULT(index->Search(query_vector, options));
      all_results.insert(all_results.end(), results.begin(), results.end());
    }

    // Sort all_results by distance and keep the top max_num_results.
    std::sort(all_results.begin(), all_results.end(), [](const auto& a, const auto& b) {
      return a.distance < b.distance;
    });

    if (all_results.size() > options.max_num_results) {
      all_results.resize(options.max_num_results);
    }

    return all_results;
  }

  Status SaveToFile(const std::string& path) override {
    return STATUS(NotSupported, "Saving to file is not implemented for ShardedVectorIndex");
  }

  Status LoadFromFile(const std::string& path, size_t) override {
    return STATUS(NotSupported, "Loading from file is not implemented for ShardedVectorIndex");
  }

  std::shared_ptr<void> Attach(std::shared_ptr<void> obj) override {
    CHECK(!indexes_.empty());
    return indexes_[0]->Attach(std::move(obj));
  }

  DistanceResult Distance(const Vector& lhs, const Vector& rhs) const override {
    CHECK(!indexes_.empty());
    return indexes_[0]->Distance(lhs, rhs);
  }

  std::string IndexStatsStr() const override {
    std::ostringstream output;
    for (size_t i = 0; i < indexes_.size(); ++i) {
      output << "Index shard #" << i << ":" << std::endl;
      output << indexes_[i]->IndexStatsStr() << std::endl;
    }
    return output.str();
  }

 private:
  std::vector<VectorIndexIfPtr<Vector, DistanceResult>> indexes_;
  std::atomic<size_t> round_robin_counter_;  // Atomic counter for thread-safe round-robin insertion
};

}  // namespace yb::vector_index
