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
#include <limits>
#include <random>
#include <vector>

#include "yb/vector_index/coordinate_codec.h"

#include "yb/util/logging.h"
#include "yb/util/test_util.h"

namespace yb::vector_index {

namespace {

std::vector<std::byte> Narrow(
    VectorStorageKind kind, const std::vector<float>& in, size_t* clamped = nullptr) {
  std::vector<std::byte> out(CoordinateBytes(kind, in.size()));
  NarrowCoordinates(kind, in.data(), in.size(), out.data(), clamped);
  return out;
}

std::vector<float> Widen(VectorStorageKind kind, const std::vector<std::byte>& in, size_t dims) {
  std::vector<float> out(dims);
  WidenCoordinates(kind, in.data(), dims, out.data());
  return out;
}

std::vector<float> RandomVector(size_t dimensions) {
  std::mt19937_64 rng(42);
  std::uniform_real_distribution<float> dis(-1.0f, 1.0f);
  std::vector<float> result(dimensions);
  for (auto& value : result) {
    value = dis(rng);
  }
  return result;
}

}  // namespace

class CoordinateCodecTest : public YBTest {
};

TEST_F(CoordinateCodecTest, SizesMatchScalarWidths) {
  for (size_t dims : {1, 8, 128, 384, 768, 1536}) {
    ASSERT_EQ(CoordinateBytes(VectorStorageKind::kFloat32, dims), dims * 4);
    ASSERT_EQ(CoordinateBytes(VectorStorageKind::kFloat16, dims), dims * 2);
  }
}

TEST_F(CoordinateCodecTest, Float32IsIdentity) {
  std::vector<float> in{-1.5f, 0.0f, 3.25f, 1e20f, -1e-20f};
  auto widened = Widen(VectorStorageKind::kFloat32, Narrow(VectorStorageKind::kFloat32, in),
                       in.size());
  ASSERT_EQ(in, widened);
}

// Repeated compaction reads chunks back through WidenCoordinates and writes them out through
// NarrowCoordinates again. If that were not idempotent, precision would drift a little on every
// compaction until the index no longer resembled the vectors it was built from.
TEST_F(CoordinateCodecTest, NarrowingIsIdempotent) {
  constexpr size_t kDims = 4096;
  std::vector<float> in(kDims);
  for (size_t i = 0; i != kDims; ++i) {
    // Sweep sign and eleven orders of magnitude, including the subnormal range.
    in[i] = static_cast<float>(
        std::sin(static_cast<double>(i) * 0.37) * std::pow(10.0, (i % 11) - 6));
  }

  auto once = Narrow(VectorStorageKind::kFloat16, in);
  auto twice = Narrow(
      VectorStorageKind::kFloat16, Widen(VectorStorageKind::kFloat16, once, kDims));
  ASSERT_EQ(once, twice);
}

// The writer and the query path share this function precisely so that a vector is still at
// distance zero from itself after narrowing.
TEST_F(CoordinateCodecTest, SameInputNarrowsToSameBytes) {
  auto in = RandomVector(1536);
  ASSERT_EQ(Narrow(VectorStorageKind::kFloat16, in), Narrow(VectorStorageKind::kFloat16, in));
}

// Two separate bounds, because only the first one is a relative bound. Coordinates below fp16's
// smallest normal lose relative precision entirely, but their absolute error stays tiny, which
// is all a squared distance is sensitive to.
TEST_F(CoordinateCodecTest, Float16ErrorBounds) {
  constexpr size_t kSamples = 20000;
  constexpr float kRelativeBound = 4.883e-4f;  // 2^-11
  constexpr float kAbsoluteBound = 3e-8f;

  double worst_relative = 0;
  for (size_t i = 0; i <= kSamples; ++i) {
    const float value = static_cast<float>(
        kFloat16MinNormal * std::pow(6.5e4 / kFloat16MinNormal,
                                     static_cast<double>(i) / kSamples));
    const auto back = Widen(
        VectorStorageKind::kFloat16, Narrow(VectorStorageKind::kFloat16, {value}), 1)[0];
    worst_relative = std::max<double>(worst_relative, std::fabs((back - value) / value));
  }
  ASSERT_LE(worst_relative, kRelativeBound) << "relative error over fp16 normals";

  double worst_absolute = 0;
  for (size_t i = 0; i <= kSamples; ++i) {
    const float value = static_cast<float>(
        1e-10 * std::pow(kFloat16MinNormal / 1e-10, static_cast<double>(i) / kSamples));
    const auto back = Widen(
        VectorStorageKind::kFloat16, Narrow(VectorStorageKind::kFloat16, {value}), 1)[0];
    worst_absolute = std::max<double>(worst_absolute, std::fabs(back - value));
  }
  ASSERT_LE(worst_absolute, kAbsoluteBound) << "absolute error below fp16's smallest normal";
}

// Without clamping, fp16_ieee_from_fp32_value saturates to infinity, and inf - inf makes every
// distance to the vector NaN -- which corrupts the ordering of the search heaps silently rather
// than failing. Same for a NaN coordinate arriving from the table.
TEST_F(CoordinateCodecTest, ClampsValuesOutsideFloat16Range) {
  const std::vector<float> in{1e30f, -1e30f, std::numeric_limits<float>::quiet_NaN(),
                              kMaxFloat16, 65520.0f, 1.0f};
  size_t clamped = 0;
  auto back = Widen(
      VectorStorageKind::kFloat16, Narrow(VectorStorageKind::kFloat16, in, &clamped), in.size());

  for (size_t i = 0; i != back.size(); ++i) {
    ASSERT_TRUE(std::isfinite(back[i])) << "coordinate " << i << " = " << back[i];
  }
  ASSERT_EQ(back[0], kMaxFloat16);
  ASSERT_EQ(back[1], -kMaxFloat16);
  ASSERT_EQ(back[2], 0.0f);
  ASSERT_EQ(back[3], kMaxFloat16);
  ASSERT_EQ(back[4], kMaxFloat16);
  ASSERT_EQ(back[5], 1.0f);

  // 1e30, -1e30, NaN and 65520 are all outside the representable range; 65504 and 1.0 are not.
  ASSERT_EQ(clamped, size_t{4});

  size_t none = 0;
  Narrow(VectorStorageKind::kFloat16, {0.0f, 1.0f, -1.0f, kMaxFloat16}, &none);
  ASSERT_EQ(none, size_t{0});
}

// The clamp counter accumulates so a caller can total it across a whole index build.
TEST_F(CoordinateCodecTest, ClampCounterAccumulates) {
  size_t clamped = 0;
  Narrow(VectorStorageKind::kFloat16, {1e30f}, &clamped);
  Narrow(VectorStorageKind::kFloat16, {1e30f, 1e30f}, &clamped);
  ASSERT_EQ(clamped, size_t{3});
}

// Coordinates land at unaligned offsets inside a YbHnsw record (20 bytes in, with a stride that
// is not a multiple of 4), so neither side of the codec may assume alignment.
TEST_F(CoordinateCodecTest, HandlesUnalignedBuffers) {
  constexpr size_t kDims = 37;
  auto in = RandomVector(kDims);

  for (size_t offset = 0; offset != 4; ++offset) {
    for (auto kind : {VectorStorageKind::kFloat32, VectorStorageKind::kFloat16}) {
      std::vector<std::byte> buffer(CoordinateBytes(kind, kDims) + offset);
      NarrowCoordinates(kind, in.data(), kDims, buffer.data() + offset);

      std::vector<float> back(kDims);
      WidenCoordinates(kind, buffer.data() + offset, kDims, back.data());

      auto aligned = Widen(kind, Narrow(kind, in), kDims);
      ASSERT_EQ(aligned, back) << "kind: " << kind << ", offset: " << offset;
    }
  }
}

}  // namespace yb::vector_index
