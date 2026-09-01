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

#include "yb/vector_index/coordinate_codec.h"

#include <cmath>
#include <cstring>

#include "fp16/fp16.h"

#include "yb/util/logging.h"

namespace yb::vector_index {

namespace {

// Records are packed at whatever offset the surrounding layout puts them (YbHnsw coordinates
// start 20 bytes into a record whose stride is not a multiple of 4), so every access here goes
// through memcpy rather than a typed load.
uint16_t LoadU16(const void* src, size_t index) {
  uint16_t result;
  memcpy(&result, static_cast<const std::byte*>(src) + index * sizeof(uint16_t), sizeof(result));
  return result;
}

void StoreU16(void* dst, size_t index, uint16_t value) {
  memcpy(static_cast<std::byte*>(dst) + index * sizeof(uint16_t), &value, sizeof(value));
}

}  // namespace

size_t CoordinateBytes(VectorStorageKind kind, size_t dimensions) {
  switch (kind) {
    case VectorStorageKind::kFloat32:
      return dimensions * sizeof(float);
    case VectorStorageKind::kFloat16:
      return dimensions * sizeof(uint16_t);
  }
  FATAL_INVALID_ENUM_VALUE(VectorStorageKind, kind);
}

void NarrowCoordinates(
    VectorStorageKind kind, const float* src, size_t dimensions, void* dst,
    size_t* num_clamped) {
  switch (kind) {
    case VectorStorageKind::kFloat32:
      memcpy(dst, src, dimensions * sizeof(float));
      return;
    case VectorStorageKind::kFloat16: {
      size_t clamped = 0;
      for (size_t i = 0; i != dimensions; ++i) {
        auto value = src[i];
        // NaN fails both comparisons, so it takes the same branch as an out-of-range magnitude.
        // TODO(vector_index): fp16_ieee_from_fp32_value is ~15 branch-free scalar ops per
        // coordinate, so narrowing a query costs ~8us at 1536 dimensions and is paid once per
        // chunk per search. _mm256_cvtps_ph does 8 coordinates per instruction; adding it needs
        // a target-attribute + __builtin_cpu_supports pattern that does not exist anywhere in
        // src/yb yet, so it is deliberately left out of the first version.
        if (PREDICT_FALSE(!(value >= -kMaxFloat16 && value <= kMaxFloat16))) {
          ++clamped;
          value = std::isnan(value) ? 0.0f : std::copysign(kMaxFloat16, value);
        }
        StoreU16(dst, i, fp16_ieee_from_fp32_value(value));
      }
      if (num_clamped) {
        *num_clamped += clamped;
      }
      return;
    }
  }
  FATAL_INVALID_ENUM_VALUE(VectorStorageKind, kind);
}

void WidenCoordinates(
    VectorStorageKind kind, const void* src, size_t dimensions, float* dst) {
  switch (kind) {
    case VectorStorageKind::kFloat32:
      memcpy(dst, src, dimensions * sizeof(float));
      return;
    case VectorStorageKind::kFloat16:
      for (size_t i = 0; i != dimensions; ++i) {
        dst[i] = fp16_ieee_to_fp32_value(LoadU16(src, i));
      }
      return;
  }
  FATAL_INVALID_ENUM_VALUE(VectorStorageKind, kind);
}

}  // namespace yb::vector_index
