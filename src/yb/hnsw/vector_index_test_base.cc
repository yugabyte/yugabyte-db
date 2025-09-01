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

#include "yb/hnsw/vector_index_test_base.h"

#include "yb/util/size_literals.h"

namespace yb::hnsw {

void VectorIndexTestBase::RandomVector(Vector& out) {
  out.clear();
  out.reserve(dimensions_);
  std::uniform_real_distribution<float> dist(0.0, 1.0);
  while (out.size() < dimensions_) {
    out.push_back(dist(rng_));
  }
}

Vector VectorIndexTestBase::RandomVector() {
  Vector result;
  RandomVector(result);
  return result;
}

std::vector<Vector> VectorIndexTestBase::RandomVectors(size_t num_vectors) {
  std::vector<Vector> result(num_vectors);
  for (auto& vector : result) {
    RandomVector(vector);
  }
  return result;
}

void VectorIndexTestBase::SetUp() {
  block_cache_ = std::make_shared<BlockCache>(
      *Env::Default(),
      MemTracker::GetRootTracker()->FindOrCreateTracker(1ULL << 30, "block_cache"),
      metric_entity_,
      BlockCacheCapacity(),
      4);
}

size_t VectorIndexTestBase::BlockCacheCapacity() {
  return 8_MB;
}

} // namespace yb::hnsw
