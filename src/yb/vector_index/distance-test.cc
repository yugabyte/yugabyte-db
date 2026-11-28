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

#include <cmath>
#include <vector>

#include "yb/vector_index/distance.h"

#include "yb/util/random_util.h"
#include "yb/util/test_util.h"

namespace yb::vector_index {

class DistanceTest : public YBTest {
};

// Vectors normalized by an older binary are normalized again by compaction after an upgrade, and
// the vector index check compares the stored vector with the row vector normalized once.
TEST_F(DistanceTest, NormalizeVectorTwice) {
  constexpr size_t kNumVectors = 2000;
  size_t num_changed = 0;
  for (size_t dimensions : {1, 2, 3, 8, 100, 768, 1536}) {
    for (double scale : {1e-23, 1e-3, 1.0, 1e3, 1e20}) {
      for (size_t i = 0; i != kNumVectors; ++i) {
        std::vector<float> vector(dimensions);
        for (auto& x : vector) {
          x = static_cast<float>(RandomUniformReal(-1.0, 1.0) * scale);
        }
        NormalizeVector(vector);
        const auto squared_length = SquaredLength(vector);
        if (squared_length != 0) {
          ASSERT_NEAR(squared_length, 1, 1e-6);
        }
        auto twice = vector;
        NormalizeVector(twice);
        num_changed += twice != vector;
      }
    }
  }
  ASSERT_EQ(num_changed, 0);
}

TEST_F(DistanceTest, NormalizeZeroVector) {
  std::vector<float> vector(8);
  NormalizeVector(vector);
  ASSERT_EQ(vector, std::vector<float>(8));
}

}  // namespace yb::vector_index
