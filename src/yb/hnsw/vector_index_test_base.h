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

#include "yb/hnsw/hnsw_block_cache.h"

#include "yb/util/metrics.h"
#include "yb/util/random_util.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

namespace yb::hnsw {

using Vector = std::vector<float>;

class VectorIndexTestBase : public YBTest {
 protected:
  void SetUp() override;

  virtual size_t BlockCacheCapacity();

  void RandomVector(Vector& out);
  Vector RandomVector();
  std::vector<Vector> RandomVectors(size_t num_vectors);

  size_t dimensions_ = 8;
  size_t max_vectors_ = 65536;
  std::mt19937_64 rng_{42};
  std::unique_ptr<MetricRegistry> metric_registry_ = std::make_unique<MetricRegistry>();
  MetricEntityPtr metric_entity_ = METRIC_ENTITY_server.Instantiate(metric_registry_.get(), "test");
  BlockCachePtr block_cache_;
};

} // namespace yb::hnsw
