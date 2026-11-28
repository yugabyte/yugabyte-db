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
#include <cstring>
#include <random>
#include <vector>

#include "yb/util/test_util.h"

#include "yb/vector_index/distance.h"
#include "yb/vector_index/distance_metric.h"

namespace yb::vector_index {

namespace {

constexpr size_t kDimensions[] = {1, 3, 16, 17, 128, 768, 1536};
constexpr size_t kVectorsPerDimension = 20;

std::vector<FloatVector> RandomVectors(size_t dimensions, size_t count, std::mt19937& rng) {
  std::uniform_real_distribution<float> distribution(-1.0f, 1.0f);
  std::vector<FloatVector> result(count, FloatVector(dimensions));
  for (auto& vector : result) {
    for (auto& coordinate : vector) {
      coordinate = distribution(rng);
    }
  }
  return result;
}

}  // namespace

class DistanceMetricTest : public YBTest {};

// SimSIMD kernels accumulate in a different order and precision than the reference functions, so
// the results only agree within a tolerance.
TEST_F(DistanceMetricTest, MatchesReference) {
  std::mt19937 rng(42);
  for (auto distance_kind : DistanceKindList()) {
    auto reference = GetDistanceFunction<FloatVector, float>(distance_kind);
    for (auto dimensions : kDimensions) {
      auto metric = DistanceMetric::Create<FloatVector>(dimensions, distance_kind);
      ASSERT_EQ(metric.dimensions(), dimensions);
      auto vectors = RandomVectors(dimensions, kVectorsPerDimension, rng);
      for (size_t i = 0; i + 1 < vectors.size(); ++i) {
        const auto& lhs = vectors[i];
        const auto& rhs = vectors[i + 1];
        auto expected = reference(lhs, rhs);
        auto actual = metric(lhs.data(), rhs.data());
        ASSERT_NEAR(actual, expected, 1e-3 * std::max(1.0f, std::abs(expected)))
            << "distance: " << distance_kind << ", dimensions: " << dimensions
            << ", isa: " << metric.isa_name();
        // Serial kernels may differ in the last bit when the arguments are swapped.
        ASSERT_NEAR(actual, metric(rhs.data(), lhs.data()), 1e-6 * std::max(1.0f, actual));
      }
    }
  }
}

TEST_F(DistanceMetricTest, InnerProductIsOneMinusDot) {
  FloatVector lhs = {1.0f, 2.0f, 3.0f};
  FloatVector rhs = {0.5f, -1.0f, 2.0f};
  auto metric = DistanceMetric::Create<FloatVector>(lhs.size(), DistanceKind::kInnerProduct);
  ASSERT_FLOAT_EQ(metric(lhs.data(), rhs.data()), 1.0f - 4.5f);
}

TEST_F(DistanceMetricTest, CosineOfZeroVectors) {
  FloatVector zero(4, 0.0f);
  FloatVector one = {1.0f, 0.0f, 0.0f, 0.0f};
  auto metric = DistanceMetric::Create<FloatVector>(zero.size(), DistanceKind::kCosine);
  ASSERT_EQ(metric(zero.data(), zero.data()), 0.0f);
  ASSERT_EQ(metric(zero.data(), one.data()), 1.0f);
  ASSERT_EQ(metric(one.data(), zero.data()), 1.0f);
  ASSERT_NEAR(metric(one.data(), one.data()), 0.0f, 1e-6);
}

// Sanitizer builds compile only SimSIMD's serial kernels; other builds on x86 and ARM pick a SIMD
// kernel.
TEST_F(DistanceMetricTest, KernelIsa) {
  auto metric = DistanceMetric::Create<FloatVector>(128, DistanceKind::kL2Squared);
  LOG(INFO) << "Kernel ISA: " << metric.isa_name();
#if defined(THREAD_SANITIZER) || defined(ADDRESS_SANITIZER)
  ASSERT_STREQ(metric.isa_name(), "serial");
#elif defined(__clang__) && (defined(__x86_64__) || defined(__aarch64__))
  ASSERT_STRNE(metric.isa_name(), "serial");
#endif
}

}  // namespace yb::vector_index
