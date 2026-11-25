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

#include <cstddef>
#include <cstdint>

#include "yb/vector_index/coordinate_types.h"
#include "yb/vector_index/distance.h"

namespace yb::vector_index {

// Distance between two raw coordinate arrays of a fixed size, computed by the SimSIMD kernel that
// fits the CPU. YbHnsw search, hnswlib's cosine space and the tools all use it, so a vector gets
// the same distance wherever it is compared. Copyable; holds no state besides the kernel choice.
class DistanceMetric {
 public:
  // Same signature as simsimd_metric_dense_punned_t, checked in distance_metric.cc, so that only
  // that file includes SimSIMD. SimSIMD's size type is unsigned long long, not uint64_t.
  using Kernel = void (*)(
      const void* lhs, const void* rhs, unsigned long long dimensions,  // NOLINT(runtime/int)
      double* result);

  DistanceMetric(size_t dimensions, DistanceKind distance_kind, CoordinateKind coordinate_kind);

  template <class Vector>
  static DistanceMetric Create(size_t dimensions, DistanceKind distance_kind) {
    return DistanceMetric(
        dimensions, distance_kind, CoordinateTypeTraits<typename Vector::value_type>::kKind);
  }

  float operator()(const void* lhs, const void* rhs) const {
    double result;
    kernel_(lhs, rhs, dimensions_, &result);
    // SimSIMD's dot kernel returns the dot product, not a distance.
    return reverse_ ? 1 - static_cast<float>(result) : static_cast<float>(result);
  }

  size_t dimensions() const {
    return dimensions_;
  }

  // The instruction set of the chosen kernel, e.g. "skylake", "neon" or "serial".
  const char* isa_name() const {
    return isa_name_;
  }

 private:
  Kernel kernel_;
  size_t dimensions_;
  bool reverse_;
  const char* isa_name_;
};

}  // namespace yb::vector_index
