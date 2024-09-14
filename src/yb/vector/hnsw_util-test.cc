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

#include <cstdint>
#include <random>

#include "yb/vector/hnsw_util.h"

#include "yb/util/logging.h"
#include "yb/util/random_util.h"
#include "yb/util/test_util.h"

namespace yb::vectorindex {

class HNSWUtilTest : public YBTest {
  void SetUp() override {
  }
};

std::vector<size_t> SampleLevels(double ml, size_t num_points) {
  std::vector<size_t> counts;
  for (int i = 0; i < 1000000; ++i) {
    auto level = SelectRandomLevel(ml, 50);
    if (counts.size() <= level) {
      counts.resize(level + 1);
    }
    counts[level]++;
  }
  return counts;
}

TEST_F(HNSWUtilTest, LevelSelection) {
  auto counts = SampleLevels(1 / log(2), 1000000);

  LOG(INFO) << "Geometric distribution with p = 1/2";
  for (size_t i = 0; i < counts.size(); ++i) {
    LOG(INFO) << "i=" << i << ", count=" << counts[i];
  }

  ASSERT_GT(counts[0], 480000);
  ASSERT_LT(counts[0], 520000);

  ASSERT_GT(counts[1], 240000);
  ASSERT_LT(counts[1], 260000);

  ASSERT_GT(counts[2], 120000);
  ASSERT_LT(counts[2], 130000);

  ASSERT_GT(counts[3], 60000);
  ASSERT_LT(counts[3], 65000);

  counts = SampleLevels(1 / log(3), 1000000);
  LOG(INFO) << "Geometric distribution with p = 2/3";
  for (size_t i = 0; i < counts.size(); ++i) {
    LOG(INFO) << "i=" << i << ", count=" << counts[i];
  }

  ASSERT_GT(counts[0], 660000);
  ASSERT_LT(counts[0], 680000);

  ASSERT_GT(counts[1], 215000);
  ASSERT_LT(counts[1], 230000);

  ASSERT_GT(counts[2], 72500);
  ASSERT_LT(counts[2], 77500);

  ASSERT_GT(counts[3], 22500);
  ASSERT_LT(counts[3], 27500);
}

}  // namespace yb::vectorindex
