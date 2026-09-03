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

std::vector<std::byte> Narrow(
    VectorStorageKind kind, float scale, const std::vector<float>& in,
    size_t* clamped = nullptr) {
  std::vector<std::byte> out(CoordinateBytes(kind, in.size()));
  NarrowCoordinates(kind, scale, in.data(), in.size(), out.data(), clamped);
  return out;
}

std::vector<float> Widen(
    VectorStorageKind kind, float scale, const std::vector<std::byte>& in, size_t dims) {
  std::vector<float> out(dims);
  WidenCoordinates(kind, scale, in.data(), dims, out.data());
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
  constexpr float kMinNormal = 6.103515625e-05f;  // fp16's smallest normal, the sweep boundary

  double worst_relative = 0;
  for (size_t i = 0; i <= kSamples; ++i) {
    const float value = static_cast<float>(
        kMinNormal * std::pow(6.5e4 / kMinNormal,
                                     static_cast<double>(i) / kSamples));
    const auto back = Widen(
        VectorStorageKind::kFloat16, Narrow(VectorStorageKind::kFloat16, {value}), 1)[0];
    worst_relative = std::max<double>(worst_relative, std::fabs((back - value) / value));
  }
  ASSERT_LE(worst_relative, kRelativeBound) << "relative error over fp16 normals";

  double worst_absolute = 0;
  for (size_t i = 0; i <= kSamples; ++i) {
    const float value = static_cast<float>(
        1e-10 * std::pow(kMinNormal / 1e-10, static_cast<double>(i) / kSamples));
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

  constexpr float kScale = 0.01f;

  for (size_t offset = 0; offset != 4; ++offset) {
    for (auto kind : {VectorStorageKind::kFloat32, VectorStorageKind::kFloat16,
                      VectorStorageKind::kInt8}) {
      std::vector<std::byte> buffer(CoordinateBytes(kind, kDims) + offset);
      NarrowCoordinates(kind, kScale, in.data(), kDims, buffer.data() + offset);

      std::vector<float> back(kDims);
      WidenCoordinates(kind, kScale, buffer.data() + offset, kDims, back.data());

      auto aligned = Widen(kind, kScale, Narrow(kind, kScale, in), kDims);
      ASSERT_EQ(aligned, back) << "kind: " << kind << ", offset: " << offset;
    }
  }
}


TEST_F(CoordinateCodecTest, Int8SizeMatchesScalarWidth) {
  ASSERT_EQ(CoordinateBytes(VectorStorageKind::kInt8, 768), size_t{768});
  ASSERT_EQ(CoordinateBytes(VectorStorageKind::kInt8, 0), size_t{0});
}

// Everything else in the design rests on this: a value that has been through the codec once must
// re-encode to the same bytes, or repeated compaction walks a vector away from where it started.
TEST_F(CoordinateCodecTest, Int8NarrowingIsIdempotent) {
  constexpr size_t kDims = 97;
  constexpr float kScale = 0.003f;
  auto in = RandomVector(kDims);

  auto once = Narrow(VectorStorageKind::kInt8, kScale, in);
  auto widened = Widen(VectorStorageKind::kInt8, kScale, once, kDims);
  auto twice = Narrow(VectorStorageKind::kInt8, kScale, widened);
  ASSERT_EQ(once, twice);

  auto widened_again = Widen(VectorStorageKind::kInt8, kScale, twice, kDims);
  ASSERT_EQ(widened, widened_again);
}

// The writer and the query path both come through here, so the same input has to produce the same
// bytes -- otherwise an indexed vector stops being at distance zero from itself.
TEST_F(CoordinateCodecTest, Int8SameInputNarrowsToSameBytes) {
  constexpr size_t kDims = 64;
  constexpr float kScale = 0.007f;
  auto in = RandomVector(kDims);
  ASSERT_EQ(
      Narrow(VectorStorageKind::kInt8, kScale, in),
      Narrow(VectorStorageKind::kInt8, kScale, in));
}

// int8's guarantee is absolute, not relative: within half a quantization step everywhere in
// range. That is the opposite shape from float16, whose bound is relative and collapses for
// subnormals, and it is why the two encodings suit different jobs.
TEST_F(CoordinateCodecTest, Int8ErrorIsWithinHalfAStep) {
  constexpr size_t kDims = 512;
  constexpr float kScale = 0.002f;

  auto in = RandomVector(kDims);
  // Include the exact boundaries and a value far below the step size.
  in[0] = 0.0f;
  in[1] = kScale;
  in[2] = -kScale;
  in[3] = kScale / 2;
  in[4] = kScale * 126.5f;
  in[5] = 1e-30f;

  auto back = Widen(
      VectorStorageKind::kInt8, kScale, Narrow(VectorStorageKind::kInt8, kScale, in), kDims);

  float worst = 0;
  for (size_t i = 0; i != kDims; ++i) {
    worst = std::max(worst, std::fabs(back[i] - in[i]));
  }
  LOG(INFO) << "worst absolute int8 error: " << worst << ", half a step: " << kScale / 2;
  ASSERT_LE(worst, kScale / 2 * 1.0001f);

  // Zero is exact at any scale, which is what makes an all-zero vector's self-distance zero.
  ASSERT_EQ(back[0], 0.0f);
  ASSERT_EQ(back[1], kScale);
  ASSERT_EQ(back[2], -kScale);
}

// float16 saturates an out-of-range magnitude to infinity; an out-of-range float -> int8_t
// conversion is undefined behaviour instead. So the clamp here is not just protecting accuracy,
// it is the only thing keeping the stored byte meaningful at all.
TEST_F(CoordinateCodecTest, Int8ClampsValuesOutsideRange) {
  constexpr float kScale = 0.01f;
  size_t clamped = 0;
  std::vector<float> in = {
      0.0f,                      // exact
      kScale * 127,              // the largest in-range value
      -kScale * 127,             // and its negation
      kScale * 128,              // one step past
      1e9f,                      // far past
      -1e9f,
      std::numeric_limits<float>::quiet_NaN(),
  };
  auto out = Narrow(VectorStorageKind::kInt8, kScale, in, &clamped);
  const auto* bytes = reinterpret_cast<const int8_t*>(out.data());

  ASSERT_EQ(bytes[0], 0);
  ASSERT_EQ(bytes[1], 127);
  ASSERT_EQ(bytes[2], -127);
  ASSERT_EQ(bytes[3], 127);
  ASSERT_EQ(bytes[4], 127);
  ASSERT_EQ(bytes[5], -127);
  // NaN becomes zero rather than an arbitrary byte.
  ASSERT_EQ(bytes[6], 0);

  // kScale * 128, +-1e9 and NaN are out of range; 0 and +-kScale * 127 are not.
  ASSERT_EQ(clamped, size_t{4});

  // Every byte is a valid int8, which is the undefined behaviour the clamp prevents.
  for (size_t i = 0; i != in.size(); ++i) {
    ASSERT_GE(bytes[i], -127) << "index " << i;
    ASSERT_LE(bytes[i], 127) << "index " << i;
  }
}

TEST_F(CoordinateCodecTest, Int8ClampCounterAccumulates) {
  constexpr float kScale = 0.01f;
  size_t clamped = 0;
  Narrow(VectorStorageKind::kInt8, kScale, {1e9f}, &clamped);
  Narrow(VectorStorageKind::kInt8, kScale, {1e9f, -1e9f}, &clamped);
  ASSERT_EQ(clamped, size_t{3});
}

// The two encodings sit back to back in one record, so writing the second must not disturb the
// first and reading either must not stray into the other.
TEST_F(CoordinateCodecTest, InterleavedEncodingsDoNotOverlap) {
  constexpr size_t kDims = 97;
  constexpr float kScale = 0.003f;
  constexpr size_t kRecordHeader = 20;   // sizeof(YbHnswVectorData)
  auto in = RandomVector(kDims);

  const auto traversal_bytes = CoordinateBytes(VectorStorageKind::kInt8, kDims);
  const auto rerank_bytes = CoordinateBytes(VectorStorageKind::kFloat16, kDims);
  std::vector<std::byte> record(kRecordHeader + traversal_bytes + rerank_bytes);

  NarrowCoordinates(
      VectorStorageKind::kInt8, kScale, in.data(), kDims, record.data() + kRecordHeader);
  NarrowCoordinates(
      VectorStorageKind::kFloat16, in.data(), kDims,
      record.data() + kRecordHeader + traversal_bytes);

  std::vector<float> traversal(kDims), rerank(kDims);
  WidenCoordinates(
      VectorStorageKind::kInt8, kScale, record.data() + kRecordHeader, kDims, traversal.data());
  WidenCoordinates(
      VectorStorageKind::kFloat16, record.data() + kRecordHeader + traversal_bytes, kDims,
      rerank.data());

  ASSERT_EQ(traversal, Widen(
      VectorStorageKind::kInt8, kScale, Narrow(VectorStorageKind::kInt8, kScale, in), kDims));
  ASSERT_EQ(rerank, Widen(
      VectorStorageKind::kFloat16, Narrow(VectorStorageKind::kFloat16, in), kDims));

  // The rerank copy has to be materially closer to the original, or it cannot correct anything.
  float traversal_worst = 0, rerank_worst = 0;
  for (size_t i = 0; i != kDims; ++i) {
    traversal_worst = std::max(traversal_worst, std::fabs(traversal[i] - in[i]));
    rerank_worst = std::max(rerank_worst, std::fabs(rerank[i] - in[i]));
  }
  LOG(INFO) << "traversal worst error " << traversal_worst << ", rerank worst error "
            << rerank_worst;
  ASSERT_LT(rerank_worst, traversal_worst);
}

TEST_F(CoordinateCodecTest, StorageKindForRerankMapsEveryTier) {
  ASSERT_EQ(StorageKindForRerank(RerankStorageKind::kFloat16), VectorStorageKind::kFloat16);
  ASSERT_EQ(StorageKindForRerank(RerankStorageKind::kFloat32), VectorStorageKind::kFloat32);
}

}  // namespace yb::vector_index
