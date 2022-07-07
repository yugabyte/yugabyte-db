//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
#include "yb/rocksdb/util/histogram.h"

#include <string>
#include <gtest/gtest.h>

#include "yb/rocksdb/util/testutil.h"

namespace rocksdb {

class HistogramTest : public RocksDBTest {};

TEST_F(HistogramTest, BasicOperation) {
  HistogramImpl histogram;
  for (uint64_t i = 1; i <= 100; i++) {
    histogram.Add(i);
  }

  {
    double median = histogram.Median();
    // ASSERT_LE(median, 50);
    ASSERT_GT(median, 0);
  }

  {
    double percentile100 = histogram.Percentile(100.0);
    ASSERT_LE(percentile100, 100.0);
    ASSERT_GT(percentile100, 0.0);
    double percentile99 = histogram.Percentile(99.0);
    double percentile85 = histogram.Percentile(85.0);
    ASSERT_LE(percentile99, 99.0);
    ASSERT_TRUE(percentile99 >= percentile85);
  }

  ASSERT_EQ(histogram.Average(), 50.5); // avg is acurately calculated.
}

TEST_F(HistogramTest, EmptyHistogram) {
  HistogramImpl histogram;
  ASSERT_EQ(histogram.Median(), 0.0);
  ASSERT_EQ(histogram.Percentile(85.0), 0.0);
  ASSERT_EQ(histogram.Average(), 0.0);
}

TEST_F(HistogramTest, ClearHistogram) {
  HistogramImpl histogram;
  for (uint64_t i = 1; i <= 100; i++) {
    histogram.Add(i);
  }
  histogram.Clear();
  ASSERT_EQ(histogram.Median(), 0);
  ASSERT_EQ(histogram.Percentile(85.0), 0);
  ASSERT_EQ(histogram.Average(), 0);
}

TEST_F(HistogramTest, BigValues) {
  double values[] = {0, 0, 1e19, 1e19};
  HistogramImpl histogram;
  for (auto v : values) {
    histogram.Add(v);
  }
  ASSERT_EQ(histogram.Average(), 5e18);
  ASSERT_GT(histogram.Median(), 0);
  ASSERT_LT(histogram.Median(), 1e19);
  ASSERT_EQ(histogram.StandardDeviation(), 5e18);
}

}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
